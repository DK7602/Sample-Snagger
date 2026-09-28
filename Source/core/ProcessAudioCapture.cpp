#include "ProcessAudioCapture.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <future>
#include <mutex>
#include <thread>
#include <vector>
#if ! JUCE_WINDOWS
 #include <unistd.h>
#endif

#if JUCE_WINDOWS && defined (__has_include)
 #if __has_include (<audioclientactivationparams.h>)
  #define SNAGGER_PROCESS_LOOPBACK 1
 #endif
#endif
#ifndef SNAGGER_PROCESS_LOOPBACK
 #define SNAGGER_PROCESS_LOOPBACK 0
#endif

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <wchar.h>
#endif

#if SNAGGER_PROCESS_LOOPBACK
 #include <objidl.h>
 #include <mmdeviceapi.h>
 #include <audioclient.h>
 #include <audioclientactivationparams.h>
 #include <tlhelp32.h>
 #pragma comment (lib, "mmdevapi.lib")
 #pragma comment (lib, "ole32.lib")
#endif

namespace snag
{

#if SNAGGER_PROCESS_LOOPBACK
namespace
{
    template <typename T> void releaseAndNull (T*& p) { if (p != nullptr) { p->Release(); p = nullptr; } }

    /** Receives the result of ActivateAudioInterfaceAsync. Must be agile (callable from any thread). */
    struct ActivationHandler final : public IActivateAudioInterfaceCompletionHandler, public IAgileObject
    {
        ActivationHandler()   { done = CreateEventW (nullptr, TRUE, FALSE, nullptr); }
        ~ActivationHandler()  { releaseAndNull (client); if (done != nullptr) CloseHandle (done); }

        HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, void** out) override
        {
            if (out == nullptr) return E_POINTER;
            if (riid == __uuidof (IUnknown) || riid == __uuidof (IActivateAudioInterfaceCompletionHandler))
                *out = static_cast<IActivateAudioInterfaceCompletionHandler*> (this);
            else if (riid == __uuidof (IAgileObject))
                *out = static_cast<IAgileObject*> (this);
            else
            {
                *out = nullptr;
                return E_NOINTERFACE;
            }
            AddRef();
            return S_OK;
        }
        ULONG STDMETHODCALLTYPE AddRef() override   { return (ULONG) InterlockedIncrement (&refs); }
        ULONG STDMETHODCALLTYPE Release() override
        {
            const auto r = InterlockedDecrement (&refs);
            if (r == 0) delete this;
            return (ULONG) r;
        }

        HRESULT STDMETHODCALLTYPE ActivateCompleted (IActivateAudioInterfaceAsyncOperation* op) override
        {
            HRESULT activateResult = E_FAIL;
            IUnknown* unknown = nullptr;
            HRESULT hr = op->GetActivateResult (&activateResult, &unknown);
            if (SUCCEEDED (hr))
                hr = activateResult;
            if (SUCCEEDED (hr) && unknown != nullptr)
                hr = unknown->QueryInterface (__uuidof (IAudioClient), reinterpret_cast<void**> (&client));
            releaseAndNull (unknown);
            result = hr;
            SetEvent (done);
            return S_OK;
        }

        volatile LONG refs = 1;
        HANDLE done = nullptr;
        HRESULT result = E_FAIL;
        IAudioClient* client = nullptr;
    };

    juce::String hex (HRESULT hr)   { return "0x" + juce::String::toHexString ((juce::uint32) hr).paddedLeft ('0', 8); }

    juce::String commandLineOf (DWORD pid)
    {
        using NtQueryInformationProcessFn = LONG (WINAPI*) (HANDLE, ULONG, PVOID, ULONG, PULONG);
        static const auto query = reinterpret_cast<NtQueryInformationProcessFn> (
            reinterpret_cast<void*> (GetProcAddress (GetModuleHandleW (L"ntdll.dll"), "NtQueryInformationProcess")));
        if (query == nullptr)
            return {};

        HANDLE h = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (h == nullptr)
            return {};

        juce::String result;
        constexpr ULONG processCommandLineInformation = 60;   // Windows 8.1+
        ULONG size = 0;
        query (h, processCommandLineInformation, nullptr, 0, &size);
        if (size > 0 && size < (1u << 20))
        {
            std::vector<unsigned char> buffer (size);
            if (query (h, processCommandLineInformation, buffer.data(), size, &size) >= 0)
            {
                struct UnicodeString { USHORT length, maximumLength; PWSTR buffer; };
                const auto* us = reinterpret_cast<const UnicodeString*> (buffer.data());
                if (us->buffer != nullptr)
                    result = juce::String (us->buffer, (size_t) us->length / sizeof (wchar_t));
            }
        }
        CloseHandle (h);
        return result;
    }
}
#endif

//==============================================================================
struct ProcessAudioCapture::Impl
{
    std::thread thread;
    std::atomic<bool> running { false }, stopRequested { false };
    mutable std::mutex errorLock;
    juce::String error;

    void setError (const juce::String& e)
    {
        const std::lock_guard<std::mutex> lock (errorLock);
        error = e;
    }

   #if SNAGGER_PROCESS_LOOPBACK
    void run (DWORD pid, Callback callback, std::shared_ptr<std::promise<bool>> started)
    {
        const HRESULT coInit = CoInitializeEx (nullptr, COINIT_MULTITHREADED);
        bool reported = false;
        auto report = [&] (bool ok, const juce::String& what)
        {
            if (! ok) setError (what);
            if (! reported) { reported = true; started->set_value (ok); }
        };

        IAudioClient* client = nullptr;
        IAudioCaptureClient* capture = nullptr;
        HANDLE ready = nullptr;

        auto cleanUp = [&]
        {
            if (client != nullptr) client->Stop();
            releaseAndNull (capture);
            releaseAndNull (client);
            if (ready != nullptr) CloseHandle (ready);
            running = false;
            if (SUCCEEDED (coInit)) CoUninitialize();
        };

        // ---- activate a loopback client for the process tree ----
        AUDIOCLIENT_ACTIVATION_PARAMS params {};
        params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        params.ProcessLoopbackParams.TargetProcessId = pid;
        params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

        PROPVARIANT activation {};
        activation.vt = VT_BLOB;
        activation.blob.cbSize = sizeof (params);
        activation.blob.pBlobData = reinterpret_cast<BYTE*> (&params);

        auto* handler = new ActivationHandler();
        IActivateAudioInterfaceAsyncOperation* op = nullptr;
        HRESULT hr = ActivateAudioInterfaceAsync (VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof (IAudioClient),
                                                  &activation, handler, &op);
        if (SUCCEEDED (hr))
        {
            if (WaitForSingleObject (handler->done, 5000) == WAIT_OBJECT_0)
            {
                hr = handler->result;
                client = handler->client;
                handler->client = nullptr;
            }
            else
                hr = HRESULT_FROM_WIN32 (ERROR_TIMEOUT);
        }
        releaseAndNull (op);
        handler->Release();

        if (FAILED (hr) || client == nullptr)
        {
            report (false, "This version of Windows can't record the browser's audio directly (" + hex (hr) + ")");
            cleanUp();
            return;
        }

        // ---- 48 kHz stereo float (the audio engine converts for us) ----
        WAVEFORMATEX format {};
        format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        format.nChannels = 2;
        format.nSamplesPerSec = 48000;
        format.wBitsPerSample = 32;
        format.nBlockAlign = (WORD) (format.nChannels * format.wBitsPerSample / 8);
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

        const DWORD flags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK
                          | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        bool isFloat = true;
        hr = client->Initialize (AUDCLNT_SHAREMODE_SHARED, flags, 200000 /* 20 ms */, 0, &format, nullptr);
        if (FAILED (hr))
        {
            format.wFormatTag = WAVE_FORMAT_PCM;
            format.wBitsPerSample = 16;
            format.nBlockAlign = (WORD) (format.nChannels * format.wBitsPerSample / 8);
            format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
            isFloat = false;
            hr = client->Initialize (AUDCLNT_SHAREMODE_SHARED, flags, 200000, 0, &format, nullptr);
        }
        if (SUCCEEDED (hr))
        {
            ready = CreateEventW (nullptr, FALSE, FALSE, nullptr);
            hr = client->SetEventHandle (ready);
        }
        if (SUCCEEDED (hr))
            hr = client->GetService (__uuidof (IAudioCaptureClient), reinterpret_cast<void**> (&capture));
        if (SUCCEEDED (hr))
            hr = client->Start();

        if (FAILED (hr))
        {
            report (false, "Couldn't start recording the browser's audio (" + hex (hr) + ")");
            cleanUp();
            return;
        }

        running = true;
        report (true, {});

        // ---- capture loop ----
        constexpr double sampleRate = 48000.0;
        LARGE_INTEGER frequency {}, clockStart {};
        QueryPerformanceFrequency (&frequency);
        QueryPerformanceCounter (&clockStart);
        double delivered = 0.0;
        std::vector<float> frames;

        while (! stopRequested)
        {
            WaitForSingleObject (ready, 50);

            UINT32 packet = 0;
            while (SUCCEEDED (hr = capture->GetNextPacketSize (&packet)) && packet > 0)
            {
                BYTE* data = nullptr;
                UINT32 count = 0;
                DWORD bufferFlags = 0;
                hr = capture->GetBuffer (&data, &count, &bufferFlags, nullptr, nullptr);
                if (FAILED (hr))
                    break;

                frames.resize ((size_t) count * 2);
                if ((bufferFlags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || data == nullptr)
                    std::fill (frames.begin(), frames.end(), 0.0f);
                else if (isFloat)
                    std::memcpy (frames.data(), data, frames.size() * sizeof (float));
                else
                {
                    const auto* s16 = reinterpret_cast<const short*> (data);
                    for (size_t i = 0; i < frames.size(); ++i)
                        frames[i] = (float) s16[i] / 32768.0f;
                }
                capture->ReleaseBuffer (count);

                if (count > 0)
                {
                    callback (frames.data(), (int) count);
                    delivered += count;
                }
            }

            if (FAILED (hr))
            {
                // e.g. the browser process closed - WebCapture finds the new one and starts again
                setError ("Recording the browser's audio stopped (" + hex (hr) + ")");
                break;
            }

            // Nothing arrives while the page is silent: fill in silence so recordings keep real time.
            LARGE_INTEGER now {};
            QueryPerformanceCounter (&now);
            const double expected = (double) (now.QuadPart - clockStart.QuadPart) / (double) frequency.QuadPart * sampleRate;
            const double behind = expected - delivered - sampleRate * 0.1;   // allow 100 ms of jitter
            if (behind > sampleRate * 0.05)
            {
                const int n = (int) behind;
                frames.assign ((size_t) n * 2, 0.0f);
                callback (frames.data(), n);
                delivered += n;
            }
            else if (delivered > expected + sampleRate * 0.5)
            {
                clockStart = now;   // the audio clock ran ahead of ours: start counting again
                delivered = 0.0;
            }
        }

        cleanUp();
    }
   #endif
};

ProcessAudioCapture::ProcessAudioCapture() : impl (std::make_unique<Impl>()) {}
ProcessAudioCapture::~ProcessAudioCapture()   { stop(); }

bool ProcessAudioCapture::isSupported()
{
    return SNAGGER_PROCESS_LOOPBACK != 0;
}

bool ProcessAudioCapture::start (juce::uint32 processId, Callback callback)
{
    stop();
   #if SNAGGER_PROCESS_LOOPBACK
    if (processId == 0 || ! callback)
    {
        impl->setError ("No browser process to record");
        return false;
    }
    impl->stopRequested = false;
    auto started = std::make_shared<std::promise<bool>>();
    auto result = started->get_future();
    impl->thread = std::thread ([this, processId, cb = std::move (callback), started]() mutable
    {
        impl->run ((DWORD) processId, std::move (cb), started);
    });

    if (result.wait_for (std::chrono::seconds (8)) != std::future_status::ready)
    {
        impl->setError ("Timed out opening the browser's audio");
        stop();
        return false;
    }
    if (! result.get())
    {
        stop();
        return false;
    }
    return true;
   #else
    juce::ignoreUnused (processId, callback);
    impl->setError ("Only available on Windows");
    return false;
   #endif
}

void ProcessAudioCapture::stop()
{
    impl->stopRequested = true;
    if (impl->thread.joinable())
        impl->thread.join();
    impl->running = false;
}

bool ProcessAudioCapture::isRunning() const   { return impl->running.load(); }

juce::String ProcessAudioCapture::getLastError() const
{
    const std::lock_guard<std::mutex> lock (impl->errorLock);
    return impl->error;
}

juce::uint32 ProcessAudioCapture::findWebViewBrowserProcess (const juce::File& userDataFolder)
{
   #if SNAGGER_PROCESS_LOOPBACK
    HANDLE snapshot = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    const auto folder = userDataFolder.getFullPathName().toLowerCase();
    const DWORD self = GetCurrentProcessId();
    DWORD byFolder = 0, childOfUs = 0;

    PROCESSENTRY32W entry {};
    entry.dwSize = sizeof (entry);
    for (BOOL ok = Process32FirstW (snapshot, &entry); ok; ok = Process32NextW (snapshot, &entry))
    {
        if (_wcsicmp (entry.szExeFile, L"msedgewebview2.exe") != 0)
            continue;

        const auto cmd = commandLineOf (entry.th32ProcessID).toLowerCase();
        const bool isBrowserProcess = ! cmd.contains ("--type=");   // renderers, GPU, audio... all have a --type
        if (cmd.isNotEmpty() && isBrowserProcess && cmd.contains (folder))
        {
            byFolder = entry.th32ProcessID;
            break;
        }
        if (entry.th32ParentProcessID == self && (cmd.isEmpty() || isBrowserProcess) && childOfUs == 0)
            childOfUs = entry.th32ProcessID;
    }
    CloseHandle (snapshot);
    return byFolder != 0 ? byFolder : childOfUs;
   #else
    juce::ignoreUnused (userDataFolder);
    return 0;
   #endif
}

juce::uint32 ProcessAudioCapture::currentProcessId()
{
   #if JUCE_WINDOWS
    return (juce::uint32) GetCurrentProcessId();
   #else
    return (juce::uint32) getpid();
   #endif
}

} // namespace snag
