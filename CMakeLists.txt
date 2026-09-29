cmake_minimum_required(VERSION 3.24)

project(SampleSnagger VERSION 1.4.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

# ------------------------------------------------------------------------------------------------
# Options
# ------------------------------------------------------------------------------------------------
option(SNAGGER_WEB_BROWSER "Build the in-plugin web browser (needs WebKit on Linux, WebView2 on Windows)" ON)
option(SNAGGER_BUILD_AAX   "Also build an AAX plug-in for Pro Tools (needs PACE signing for retail Pro Tools)" OFF)
option(SNAGGER_BUILD_TESTS "Build the headless test + screenshot runner" ON)
option(SNAGGER_BUILTIN_AI  "Build the built-in AI stem engine (Demucs v4 in C++, no Python needed)" ON)

# ------------------------------------------------------------------------------------------------
# Dependencies (fetched automatically; point FETCHCONTENT_SOURCE_DIR_<NAME> at local copies to skip)
# ------------------------------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# Windows: the built-in browser uses Microsoft Edge WebView2. Fetch its SDK automatically
# (a NuGet package) so a plain `cmake --build` works without any manual setup.
if(WIN32 AND SNAGGER_WEB_BROWSER AND NOT JUCE_WEBVIEW2_PACKAGE_LOCATION)
    set(SNAGGER_WEBVIEW2_VERSION 1.0.1901.177)
    set(SNAGGER_NUGET_DIR "${CMAKE_BINARY_DIR}/nuget")
    set(SNAGGER_WEBVIEW2_DIR "${SNAGGER_NUGET_DIR}/Microsoft.Web.WebView2.${SNAGGER_WEBVIEW2_VERSION}")
    if(NOT EXISTS "${SNAGGER_WEBVIEW2_DIR}/build/native/include/WebView2.h")
        message(STATUS "Downloading the WebView2 SDK ${SNAGGER_WEBVIEW2_VERSION}")
        file(DOWNLOAD "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${SNAGGER_WEBVIEW2_VERSION}"
             "${SNAGGER_NUGET_DIR}/webview2.zip" STATUS dl_status TLS_VERIFY ON)
        list(GET dl_status 0 dl_code)
        if(NOT dl_code EQUAL 0)
            message(FATAL_ERROR "Couldn't download the WebView2 SDK (${dl_status}). Configure with -DSNAGGER_WEB_BROWSER=OFF to build without the built-in browser.")
        endif()
        file(ARCHIVE_EXTRACT INPUT "${SNAGGER_NUGET_DIR}/webview2.zip" DESTINATION "${SNAGGER_WEBVIEW2_DIR}")
    endif()
    set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${SNAGGER_NUGET_DIR}")
endif()

FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(juce)

FetchContent_Declare(signalsmith_linear
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/linear.git
    GIT_TAG        0.6.4
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(signalsmith_linear)

FetchContent_Declare(signalsmith_stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG        1.4.0
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(signalsmith_stretch)

if(SNAGGER_BUILTIN_AI)
    # Eigen 3.4.0 (GitHub mirror of the official tag, commit 3147391d) - header only
    FetchContent_Declare(eigen
        GIT_REPOSITORY https://github.com/ROCm/eigen.git
        GIT_TAG        3.4.0
        GIT_SHALLOW    ON
        SOURCE_SUBDIR  do-not-add-subdirectory)
    # demucs.cpp - C++17 inference for Demucs v4 hybrid transformer (MIT, by Sevag H.)
    FetchContent_Declare(demucs_cpp
        GIT_REPOSITORY https://github.com/sevagh/demucs.cpp.git
        GIT_TAG        f1206e9adeea103aef4a636b9e62297cf1f8e34e
        GIT_SUBMODULES ""
        SOURCE_SUBDIR  do-not-add-subdirectory)
    FetchContent_MakeAvailable(eigen demucs_cpp)
endif()

# ------------------------------------------------------------------------------------------------
# Plug-in formats
# ------------------------------------------------------------------------------------------------
set(SNAGGER_FORMATS Standalone VST3)
if(APPLE)
    list(APPEND SNAGGER_FORMATS AU)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND SNAGGER_FORMATS LV2)
endif()
if(SNAGGER_BUILD_AAX AND (APPLE OR WIN32))
    list(APPEND SNAGGER_FORMATS AAX)
endif()

set(SNAGGER_EXTRA_ARGS)
if(SNAGGER_WEB_BROWSER)
    if(WIN32)
        list(APPEND SNAGGER_EXTRA_ARGS NEEDS_WEBVIEW2 TRUE)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND SNAGGER_EXTRA_ARGS NEEDS_WEB_BROWSER TRUE)
    endif()
endif()

juce_add_plugin(SampleSnagger
    COMPANY_NAME                 "Snagger Audio"
    COMPANY_WEBSITE              "https://github.com/"
    PRODUCT_NAME                 "Sample Snagger"
    BUNDLE_ID                    com.snaggeraudio.samplesnagger
    PLUGIN_MANUFACTURER_CODE     Sngr
    PLUGIN_CODE                  SnG1
    FORMATS                      ${SNAGGER_FORMATS}
    IS_SYNTH                     TRUE
    NEEDS_MIDI_INPUT             TRUE
    NEEDS_MIDI_OUTPUT            FALSE
    IS_MIDI_EFFECT               FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS  TRUE
    COPY_PLUGIN_AFTER_BUILD      FALSE
    NEEDS_CURL                   TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT   "Sample Snagger records audio from your input device when you press REC INPUT."
    VST3_CATEGORIES              Instrument Sampler
    AU_MAIN_TYPE                 kAudioUnitType_MusicDevice
    AAX_CATEGORY                 AAX_ePlugInCategory_SWGenerators
    LV2URI                       "https://snaggeraudio.com/plugins/sample-snagger"
    ICON_BIG                     "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
    ${SNAGGER_EXTRA_ARGS})

juce_add_binary_data(SnaggerBinaryData
    HEADER_NAME SnaggerBinaryData.h
    NAMESPACE   SnaggerBinary
    SOURCES
        Resources/Fonts/Cinzel-SemiBold.ttf
        Resources/Fonts/Cinzel-ExtraBold.ttf
        Resources/Fonts/Montserrat-Medium.ttf
        Resources/Fonts/Montserrat-Bold.ttf
        Resources/Scripts/snagger_ai.py
        Resources/Scripts/webtap.js
        Resources/logo_256.png
        Resources/glitter_glass.jpg
        Resources/gold_smooth.jpg
        Resources/glitter_text.jpg)

set(SNAGGER_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Actions.cpp
    Source/StandaloneBridge.cpp
    Source/core/AudioFileIO.cpp
    Source/core/EditOps.cpp
    Source/core/QuickSplit.cpp
    Source/core/Session.cpp
    Source/core/Settings.cpp
    Source/core/Tools.cpp
    Source/core/Jobs.cpp
    Source/core/WebCapture.cpp
    Source/core/ProcessAudioCapture.cpp
    Source/core/AiStems.cpp
    Source/core/KeyDetect.cpp
    Source/core/PadFx.cpp
    Source/core/FxRack.cpp
    Source/ui/Theme.cpp
    Source/ui/Widgets.cpp
    Source/ui/WaveformView.cpp
    Source/ui/BrowsePage.cpp
    Source/ui/StudioPage.cpp
    Source/ui/StemsPage.cpp
    Source/ui/SeparationAnimation.cpp
    Source/ui/LibraryPage.cpp
    Source/ui/SettingsPanel.cpp
    Source/ui/ClipTray.cpp)

# Everything that the plug-in and the test runner share
add_library(SnaggerShared INTERFACE)
target_include_directories(SnaggerShared INTERFACE Source)
target_link_libraries(SnaggerShared INTERFACE
    SnaggerBinaryData
    signalsmith-stretch
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

set(SNAGGER_DEFS
    JUCE_WEB_BROWSER=$<IF:$<BOOL:${SNAGGER_WEB_BROWSER}>,1,0>
    JUCE_USE_CURL=1
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_USE_WINDOWS_MEDIA_FORMAT=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

if(WIN32 AND SNAGGER_WEB_BROWSER)
    list(APPEND SNAGGER_DEFS JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Allow headless / minimal Linux build machines (these are optional niceties on Linux)
    option(SNAGGER_LINUX_MINIMAL "Disable ALSA/JACK/Xrandr/Xinerama/Xcursor (CI / headless builds)" OFF)
    if(SNAGGER_LINUX_MINIMAL)
        list(APPEND SNAGGER_DEFS JUCE_ALSA=0 JUCE_JACK=0 JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0)
    endif()
endif()

target_compile_definitions(SnaggerShared INTERFACE ${SNAGGER_DEFS})

if(SNAGGER_BUILTIN_AI)
    file(GLOB SNAGGER_DEMUCS_SOURCES "${demucs_cpp_SOURCE_DIR}/src/*.cpp")
    add_library(snagger_demucs STATIC ${SNAGGER_DEMUCS_SOURCES} Source/ai/DemucsBridge.cpp)
    target_include_directories(snagger_demucs SYSTEM PRIVATE "${eigen_SOURCE_DIR}" "${demucs_cpp_SOURCE_DIR}/src")
    target_include_directories(snagger_demucs PUBLIC Source/ai)
    set_target_properties(snagger_demucs PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)
    # Eigen 3.4.0 mistakes clang and MSVC for gcc < 4.8 and turns off the Tensor SFINAE overloads
    # demucs.cpp relies on (fixed in later Eigen). Same empty value as Eigen's own #define.
    target_compile_definitions(snagger_demucs PRIVATE NDEBUG EIGEN_NO_DEBUG "EIGEN_HAS_SFINAE=")
    # Baseline instruction set only (SSE2 on x86-64, NEON on Apple Silicon). Building this library
    # with AVX2 would be faster, but the linker can then pick AVX2 copies of shared inline code
    # (std::vector etc.) for the whole plug-in, which would crash hosts on older CPUs.
    if(MSVC)
        target_compile_options(snagger_demucs PRIVATE /bigobj /EHsc /W0 "$<$<NOT:$<CONFIG:Debug>>:/O2;/Ob2;/fp:fast>")
    else()
        target_compile_options(snagger_demucs PRIVATE -O3 -fno-math-errno -w)
    endif()
    target_compile_definitions(SnaggerShared INTERFACE SNAGGER_BUILTIN_AI=1)
    target_link_libraries(SnaggerShared INTERFACE snagger_demucs)
endif()

if(MSVC)
    target_compile_options(SnaggerShared INTERFACE /bigobj /utf-8)
endif()

target_sources(SampleSnagger PRIVATE ${SNAGGER_SOURCES})
target_link_libraries(SampleSnagger PRIVATE SnaggerShared)
target_compile_definitions(SampleSnagger PUBLIC JUCE_MODAL_LOOPS_PERMITTED=0)

# ------------------------------------------------------------------------------------------------
# Headless test runner (unit tests for the DSP + renders UI screenshots)
# ------------------------------------------------------------------------------------------------
if(SNAGGER_BUILD_TESTS)
    juce_add_console_app(SnaggerTests PRODUCT_NAME "SnaggerTests" NEEDS_CURL TRUE ${SNAGGER_EXTRA_ARGS})
    target_sources(SnaggerTests PRIVATE Tests/TestMain.cpp ${SNAGGER_SOURCES})
    target_compile_definitions(SnaggerTests PRIVATE SNAGGER_TEST_RUNNER=1 JUCE_MODAL_LOOPS_PERMITTED=1
        SNAGGER_TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Tests")
    target_link_libraries(SnaggerTests PRIVATE SnaggerShared)
endif()
