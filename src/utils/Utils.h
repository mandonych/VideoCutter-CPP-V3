#pragma once

#include <windows.h>
#include <string>
#include <filesystem>

inline std::wstring GetExecutablePath()
{
    wchar_t buffer[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);

    return std::filesystem::path(buffer)
        .parent_path()
        .wstring();
}

inline HANDLE OpenNullDevice(SECURITY_ATTRIBUTES* sa)
{
    return CreateFileW(
        L"NUL",
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        sa,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
}
