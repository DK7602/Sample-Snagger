#include "Process.h"

#include <algorithm>
#include <vector>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <tlhelp32.h>
#else
 #include <signal.h>
 #include <unistd.h>
 #if JUCE_MAC
  #include <sys/sysctl.h>
 #endif
#endif

namespace snag::proctree
{

namespace
{
    struct Proc { int pid = 0, parent = 0; };

    /** Every process on the system with its parent, as of now. */
    std::vector<Proc> listProcesses()
    {
        std::vector<Proc> all;

       #if JUCE_WINDOWS
        HANDLE snapshot = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return all;
        PROCESSENTRY32W entry {};
        entry.dwSize = sizeof (entry);
        for (BOOL ok = Process32FirstW (snapshot, &entry); ok; ok = Process32NextW (snapshot, &entry))
            all.push_back ({ (int) entry.th32ProcessID, (int) entry.th32ParentProcessID });
        CloseHandle (snapshot);

       #elif JUCE_MAC
        int name[] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };
        for (int attempt = 0; attempt < 4; ++attempt)   // (the list can grow between asking for its size and reading it)
        {
            size_t bytes = 0;
            if (sysctl (name, 4, nullptr, &bytes, nullptr, 0) != 0 || bytes == 0)
                break;
            bytes += bytes / 8 + 16 * sizeof (kinfo_proc);
            std::vector<kinfo_proc> list (bytes / sizeof (kinfo_proc) + 1);
            bytes = list.size() * sizeof (kinfo_proc);
            if (sysctl (name, 4, list.data(), &bytes, nullptr, 0) != 0)
                continue;
            const size_t count = bytes / sizeof (kinfo_proc);
            for (size_t i = 0; i < count; ++i)
                all.push_back ({ (int) list[i].kp_proc.p_pid, (int) list[i].kp_eproc.e_ppid });
            break;
        }

       #else
        for (const auto& entry : juce::RangedDirectoryIterator (juce::File ("/proc"), false, "*", juce::File::findDirectories))
        {
            const auto dir = entry.getFile();
            const auto name = dir.getFileName();
            if (! name.containsOnly ("0123456789"))
                continue;
            // "<pid> (<name, which may hold spaces and brackets>) <state> <parent> ..."
            const auto stat = dir.getChildFile ("stat").loadFileAsString();
            const auto rest = stat.fromLastOccurrenceOf (")", false, false).trim();
            const auto fields = juce::StringArray::fromTokens (rest, " ", {});
            if (fields.size() >= 2)
                all.push_back ({ name.getIntValue(), fields[1].getIntValue() });
        }
       #endif

        return all;
    }

    void end (int pid)
    {
        if (pid <= 0 || pid == currentProcess())
            return;
       #if JUCE_WINDOWS
        if (HANDLE h = OpenProcess (PROCESS_TERMINATE, FALSE, (DWORD) pid))
        {
            TerminateProcess (h, 1);
            CloseHandle (h);
        }
       #else
        ::kill ((pid_t) pid, SIGKILL);
       #endif
    }
}

int currentProcess()
{
   #if JUCE_WINDOWS
    return (int) GetCurrentProcessId();
   #else
    return (int) getpid();
   #endif
}

juce::uint64 startTimeOf (int pid)
{
   #if JUCE_WINDOWS
    juce::uint64 t = 0;
    if (HANDLE h = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD) pid))
    {
        FILETIME created {}, exited {}, kernel {}, user {};
        if (GetProcessTimes (h, &created, &exited, &kernel, &user))
            t = ((juce::uint64) created.dwHighDateTime << 32) | (juce::uint64) created.dwLowDateTime;
        CloseHandle (h);
    }
    return t;
   #else
    juce::ignoreUnused (pid);
    return 0;   // (not needed here: a process whose parent ended is handed to another parent at once)
   #endif
}

std::vector<int> childrenOfCurrentProcess()
{
    const int self = currentProcess();
    std::vector<int> children;
    for (auto& p : listProcesses())
        if (p.parent == self && p.pid != self)
            children.push_back (p.pid);
    return children;
}

std::vector<int> descendantsOf (int root, juce::uint64 rootStarted)
{
    std::vector<int> found;
    if (root <= 0)
        return found;

    const auto all = listProcesses();
    struct Known { int pid; juce::uint64 started; };
    std::vector<Known> frontier { { root, rootStarted } };

    while (! frontier.empty())
    {
        const auto parent = frontier.back();
        frontier.pop_back();
        for (auto& p : all)
        {
            if (p.parent != parent.pid || p.pid == root || p.pid <= 0 || p.pid == currentProcess())
                continue;
            if (std::find (found.begin(), found.end(), p.pid) != found.end())
                continue;

            // Windows keeps naming a parent that has ended, and gives its number to a new
            // process later. A "child" that is older than its "parent" isn't one.
            const auto started = startTimeOf (p.pid);
            if (parent.started != 0 && started != 0 && started < parent.started)
                continue;

            found.push_back (p.pid);
            frontier.push_back ({ p.pid, started });
        }
    }
    return found;
}

int killDescendants (int root, juce::uint64 rootStarted)
{
    auto below = descendantsOf (root, rootStarted);
    std::reverse (below.begin(), below.end());   // the youngest first
    for (auto pid : below)
        end (pid);
    return (int) below.size();
}

void freeze (int pid)
{
   #if JUCE_WINDOWS
    juce::ignoreUnused (pid);
   #else
    if (pid > 0 && pid != currentProcess())
        ::kill ((pid_t) pid, SIGSTOP);   // (so it can't start anything new while its tree is being ended)
   #endif
}

} // namespace snag::proctree
