#include "VideoInfo.h"
#include "Utils.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <vector>
#include <windows.h>

namespace {

std::wstring GetProbeValue(const std::wstring &output,
                           const std::wstring &key) {
  size_t pos = output.find(key + L"=");

  if (pos == std::wstring::npos)
    return L"";

  pos += key.length() + 1;

  size_t end = output.find(L'\n', pos);

  std::wstring value =
      output.substr(pos, end == std::wstring::npos ? end : end - pos);

  while (!value.empty() && (value.back() == L'\r' || value.back() == L'"')) {
    value.pop_back();
  }

  if (!value.empty() && value.front() == L'"')
    value.erase(value.begin());

  return value;
}

double ParseFps(const std::wstring &value) {
  size_t slash = value.find(L'/');

  try {
    if (slash != std::wstring::npos) {
      double numerator = std::stod(value.substr(0, slash));
      double denominator = std::stod(value.substr(slash + 1));

      if (denominator != 0)
        return numerator / denominator;
    }

    return std::stod(value);
  } catch (...) {
    return 25.0;
  }
}


std::wstring GetStreamProbeValue(
    const std::wstring& output,
    const std::wstring& streamType,
    const std::wstring& key)
{
    const std::wstring typeMarker =
        L".codec_type=\"" + streamType + L"\"";

    size_t typePos = output.find(typeMarker);

    if (typePos == std::wstring::npos)
        return L"";

    size_t lineStart = output.rfind(L'\n', typePos);

    if (lineStart == std::wstring::npos)
        lineStart = 0;
    else
        ++lineStart;

    size_t prefixEnd = output.find(L".codec_type=", lineStart);

    if (prefixEnd == std::wstring::npos)
        return L"";

    std::wstring prefix = output.substr(
        lineStart,
        prefixEnd - lineStart
    );

    return GetProbeValue(output, prefix + L"." + key);
}


std::wstring GetDisplayFormat(const std::wstring &filename,
                              const std::wstring &originalFormat) {
  std::wstring extension =
      std::filesystem::path(filename).extension().wstring();

  if (!extension.empty() && extension.front() == L'.')
    extension.erase(extension.begin());

  std::transform(extension.begin(), extension.end(), extension.begin(),
                 towupper);

  if (!extension.empty())
    return extension;

  return originalFormat;
}

bool RunFFprobe(const std::wstring &filename, std::wstring &output) {
  std::filesystem::path probePath =
      std::filesystem::path(GetExecutablePath()) / L"bin" / L"ffprobe.exe";

  if (!std::filesystem::exists(probePath))
    return false;

  std::wstring command =
      L"\"" + probePath.wstring() +
      L"\" -v error "
      L"-show_entries "
      L"format=duration,format_name:stream=codec_type,codec_name,width,height,"
      L"avg_frame_rate,bit_rate,sample_rate,channels,channel_layout "
      L"-of flat \"" +
      filename + L"\"";

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  HANDLE readPipe = nullptr;
  HANDLE writePipe = nullptr;

  if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
    return false;

  SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

  HANDLE nullHandle = OpenNullDevice(&sa);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = writePipe;
  si.hStdError = nullHandle;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

  PROCESS_INFORMATION pi{};

  std::vector<wchar_t> cmd(command.begin(), command.end());
  cmd.push_back(L'\0');

  BOOL created = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                                CREATE_NO_WINDOW, nullptr,
                                GetExecutablePath().c_str(), &si, &pi);

  CloseHandle(writePipe);
  if (nullHandle != INVALID_HANDLE_VALUE)
    CloseHandle(nullHandle);

  if (!created) {
    CloseHandle(readPipe);
    return false;
  }

  std::vector<char> data;
  char buffer[4096];
  DWORD bytesRead = 0;

  while (ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr) &&
         bytesRead > 0) {
    data.insert(data.end(), buffer, buffer + bytesRead);
  }

  CloseHandle(readPipe);

  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD exitCode = 1;
  GetExitCodeProcess(pi.hProcess, &exitCode);

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  if (exitCode != 0)
    return false;

  int size = MultiByteToWideChar(CP_UTF8, 0, data.data(),
                                 static_cast<int>(data.size()), nullptr, 0);

  if (size > 0) {
    output.resize(size);

    MultiByteToWideChar(CP_UTF8, 0, data.data(), static_cast<int>(data.size()),
                        output.data(), size);
  }

  return true;
}

} // namespace



bool ProbeVideo(const std::wstring &filename, VideoInfo &info) {
  std::wstring output;

  if (!RunFFprobe(filename, output))
    return false;

  try {
    VideoInfo result;

    result.duration = std::stod(GetProbeValue(output, L"format.duration"));

    result.width =
        std::stoi(GetStreamProbeValue(output, L"video", L"width"));

    result.height =
        std::stoi(GetStreamProbeValue(output, L"video", L"height"));

    result.fps =
        ParseFps(GetStreamProbeValue(output, L"video", L"avg_frame_rate"));

    result.format = GetDisplayFormat(
        filename, GetProbeValue(output, L"format.format_name"));

    // Audio information
    result.audioCodec =
        GetStreamProbeValue(output, L"audio", L"codec_name");

    result.hasAudio = !result.audioCodec.empty();

    if (result.hasAudio) {
      result.audioBitrate =
          GetStreamProbeValue(output, L"audio", L"bit_rate");

      result.audioSampleRate =
          GetStreamProbeValue(output, L"audio", L"sample_rate");

      result.audioChannels =
          GetStreamProbeValue(output, L"audio", L"channel_layout");

      if (result.audioChannels.empty()) {
        result.audioChannels =
            GetStreamProbeValue(output, L"audio", L"channels");
      }
    }

    if (result.duration <= 0 || result.width <= 0 || result.height <= 0) {
      return false;
    }

    info = result;
    return true;
  } catch (...) {
    return false;
  }
}