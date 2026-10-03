#pragma once
#include "protocol.hpp"
#include <windows.h>

namespace rep {
inline std::filesystem::path executableDirectory(){
    wchar_t name[32768]{};
    if(!GetModuleFileNameW(nullptr,name,32768))throw Error("Cannot find executable directory");
    return std::filesystem::path(name).parent_path();
}
inline std::filesystem::path applicationDirectory(){
    auto directory=executableDirectory();
    // Source builds retain the existing project-local settings and test outputs.
    if(directory.filename()==L"build"&&std::filesystem::is_regular_file(directory.parent_path()/L"build.ps1"))
        return directory.parent_path();
    return directory;
}
inline std::filesystem::path ffmpegExecutable(){
    auto directory=executableDirectory(),bundled=directory/L"resources"/L"ffmpeg.exe";
    return std::filesystem::is_regular_file(bundled)?bundled:directory/L"ffmpeg.exe";
}
}
