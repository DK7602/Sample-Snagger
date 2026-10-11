// Force-included into every source of the built-in AI engine on Windows (see CMakeLists.txt).
//
// demucs.cpp opens its model files with fopen(). On Windows that reads the path in the system's
// old "ANSI" code page, while Sample Snagger hands over UTF-8 - so with any letter outside ASCII
// in the path (the models live under C:\Users\<your name>\AppData) the file was "not found", and
// the built-in AI could never be used. This sends those calls to a version that understands UTF-8.
#pragma once

#if defined (_WIN32) && defined (__cplusplus)
 #include <cstdio>

 namespace snagai { std::FILE* fopenUtf8 (const char* utf8Path, const char* mode); }

 #define fopen(path, mode) ::snagai::fopenUtf8 ((path), (mode))
#endif
