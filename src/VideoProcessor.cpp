#include "VideoProcessor.h"

#include <string>
#include <algorithm>

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <vector>
#include <cstring>
#include <filesystem>



namespace {

std::wstring GetExecutablePath()
{
    wchar_t buffer[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);

    return std::filesystem::path(buffer)
        .parent_path()
        .wstring();
}

} // namespace

Gdiplus::Bitmap* ExtractFrame(
    const std::wstring& filename,
    double timeSeconds)
{
    std::wstring ffmpeg =
        GetExecutablePath() + L"\\bin\\ffmpeg.exe";

    std::wstring command =
        L"\"" + ffmpeg + L"\""
        L" -ss " + std::to_wstring(timeSeconds) +
        L" -i \"" + filename + L"\""
        L" -frames:v 1 -f image2pipe -vcodec bmp pipe:1";

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;

    if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
        return nullptr;

    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};

    BOOL created = CreateProcessW(
        nullptr,
        command.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    CloseHandle(writePipe);

    if (!created)
    {
        CloseHandle(readPipe);
        return nullptr;
    }

    std::vector<BYTE> data;
    BYTE buffer[8192];
    DWORD bytesRead = 0;

    while (ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr)
           && bytesRead > 0)
    {
        data.insert(data.end(), buffer, buffer + bytesRead);
    }

    CloseHandle(readPipe);

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (data.empty())
        return nullptr;

    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, data.size());

    if (!memory)
        return nullptr;

    void* pointer = GlobalLock(memory);
    memcpy(pointer, data.data(), data.size());
    GlobalUnlock(memory);

    IStream* stream = nullptr;

    if (CreateStreamOnHGlobal(memory, TRUE, &stream) != S_OK)
    {
        GlobalFree(memory);
        return nullptr;
    }

    Gdiplus::Bitmap* bitmap =
        Gdiplus::Bitmap::FromStream(stream);

    stream->Release();

    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok)
    {
        delete bitmap;
        return nullptr;
    }

    return bitmap;
}

void RunCutProcess(
    const std::wstring& command,
    const std::wstring& ffmpeg,
    HWND hwnd,
    double duration)
{
    std::vector<wchar_t> cmd(command.begin(), command.end());
    cmd.push_back(L'\0');

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;

    if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
    {
        PostMessageW(hwnd, WM_CUT_FINISHED, 2, 0);
        return;
    }

    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};

    if (!CreateProcessW(
        ffmpeg.c_str(),
        cmd.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi))
    {
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        PostMessageW(hwnd, WM_CUT_FINISHED, 2, 0);
        return;
    }

    CloseHandle(writePipe);

    std::string buffer;
    char chunk[4096];
    DWORD bytesRead = 0;

    while (ReadFile(readPipe, chunk, sizeof(chunk), &bytesRead, nullptr)
           && bytesRead > 0)
    {
        buffer.append(chunk, bytesRead);

        size_t pos;

        while ((pos = buffer.find('\n')) != std::string::npos)
        {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);

            if (line.rfind("out_time_ms=", 0) == 0)
            {
                try
                {
                    long long microseconds =
                        std::stoll(line.substr(12));

                    int percent = static_cast<int>(
                        microseconds / (duration * 10000.0)
                    );

                    percent = (std::min)(percent, 99);
                    percent = (std::max)(percent, 0);

                    PostMessageW(
                        hwnd,
                        WM_CUT_PROGRESS,
                        percent,
                        0
                    );
                }
                catch (...)
                {
                }
            }
        }
    }

    CloseHandle(readPipe);

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    PostMessageW(hwnd, WM_CUT_FINISHED, exitCode, 0);
}