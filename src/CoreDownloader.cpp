#include "app/CoreDownloader.hpp"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace app {
namespace {

constexpr wchar_t CoreUrl[] =
    L"https://buildbot.libretro.com/nightly/windows/x86_64/latest/opera_libretro.dll.zip";
constexpr std::uint64_t MaxArchiveBytes = 128ULL * 1024ULL * 1024ULL;

class InternetHandle final {
public:
    explicit InternetHandle(HINTERNET handle = nullptr) : m_handle(handle) {}
    ~InternetHandle() { if (m_handle) WinHttpCloseHandle(m_handle); }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
    HINTERNET get() const noexcept { return m_handle; }
private:
    HINTERNET m_handle{};
};

class TemporaryFiles final {
public:
    explicit TemporaryFiles(std::filesystem::path directory) : m_directory(std::move(directory)) {}
    ~TemporaryFiles() {
        std::error_code ignored;
        std::filesystem::remove_all(m_directory, ignored);
    }
private:
    std::filesystem::path m_directory;
};

std::wstring quotePowerShell(const std::filesystem::path& path) {
    std::wstring value = path.wstring();
    std::wstring quoted = L"'";
    for (const wchar_t character : value) {
        quoted += character;
        if (character == L'\'') quoted += L'\'';
    }
    quoted += L'\'';
    return quoted;
}

std::wstring encodeCommand(const std::wstring& command) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(command.data());
    constexpr wchar_t alphabet[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::wstring output;
    output.reserve((command.size() * sizeof(wchar_t) + 2) / 3 * 4);
    const std::size_t length = command.size() * sizeof(wchar_t);
    for (std::size_t index = 0; index < length; index += 3) {
        const unsigned a = bytes[index];
        const unsigned b = index + 1 < length ? bytes[index + 1] : 0U;
        const unsigned c = index + 2 < length ? bytes[index + 2] : 0U;
        const unsigned value = (a << 16U) | (b << 8U) | c;
        output += alphabet[(value >> 18U) & 63U];
        output += alphabet[(value >> 12U) & 63U];
        output += index + 1 < length ? alphabet[(value >> 6U) & 63U] : L'=';
        output += index + 2 < length ? alphabet[value & 63U] : L'=';
    }
    return output;
}

void downloadZip(const std::filesystem::path& outputFile) {
    InternetHandle session(WinHttpOpen(L"Opera SDL3 Frontend/0.1",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.get()) throw std::runtime_error("Unable to initialize the Windows HTTPS client.");
    WinHttpSetTimeouts(session.get(), 15000, 15000, 30000, 30000);

    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(CoreUrl, 0, 0, &parts)) {
        throw std::runtime_error("Unable to parse the Opera core download URL.");
    }
    InternetHandle connection(WinHttpConnect(session.get(), parts.lpszHostName, parts.nPort, 0));
    if (!connection.get()) throw std::runtime_error("Unable to connect to the libretro build server.");
    std::wstring resource(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength > 0) resource.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    const DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    InternetHandle request(WinHttpOpenRequest(connection.get(), L"GET", resource.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request.get() || !WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.get(), nullptr)) {
        throw std::runtime_error("The Opera core download request failed (Windows error " +
                                 std::to_string(GetLastError()) + ").");
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) || status != 200) {
        throw std::runtime_error("The libretro build server did not return the Opera core archive (HTTP " +
                                 std::to_string(status) + ").");
    }

    std::ofstream output(outputFile, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Unable to create the temporary Opera core archive.");
    std::array<char, 64 * 1024> buffer{};
    std::uint64_t total = 0;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available)) {
            throw std::runtime_error("Unable to read the Opera core download response.");
        }
        if (available == 0) break;
        DWORD read = 0;
        if (!WinHttpReadData(request.get(), buffer.data(),
                static_cast<DWORD>(std::min<std::size_t>(buffer.size(), available)), &read)) {
            throw std::runtime_error("The Opera core download was interrupted.");
        }
        total += read;
        if (total > MaxArchiveBytes) throw std::runtime_error("The Opera core archive exceeded the size limit.");
        output.write(buffer.data(), static_cast<std::streamsize>(read));
        if (!output) throw std::runtime_error("Unable to save the Opera core archive.");
    }
    if (total == 0) throw std::runtime_error("The Opera core download was empty.");
}

void extractZip(const std::filesystem::path& archive, const std::filesystem::path& destination) {
    std::wstring windowsDirectory(32768, L'\0');
    const UINT length = GetWindowsDirectoryW(windowsDirectory.data(), static_cast<UINT>(windowsDirectory.size()));
    if (length == 0 || length >= windowsDirectory.size()) {
        throw std::runtime_error("Unable to locate Windows PowerShell for archive extraction.");
    }
    windowsDirectory.resize(length);
    const std::filesystem::path powershell =
        std::filesystem::path(windowsDirectory) / L"System32\\WindowsPowerShell\\v1.0\\powershell.exe";
    const std::wstring script = L"$ErrorActionPreference='Stop'; Expand-Archive -LiteralPath " +
        quotePowerShell(archive) + L" -DestinationPath " + quotePowerShell(destination) + L" -Force";
    std::wstring commandLine = L"\"" + powershell.wstring() +
        L"\" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand " + encodeCommand(script);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(powershell.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        throw std::runtime_error("Unable to start Windows PowerShell to extract the Opera core archive.");
    }
    CloseHandle(process.hThread);
    const DWORD waitResult = WaitForSingleObject(process.hProcess, 120000);
    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, INFINITE);
        CloseHandle(process.hProcess);
        throw std::runtime_error("Windows PowerShell timed out while extracting the Opera core archive.");
    }
    if (waitResult != WAIT_OBJECT_0) {
        const DWORD error = GetLastError();
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, INFINITE);
        CloseHandle(process.hProcess);
        throw std::runtime_error("Unable to wait for Windows PowerShell (Windows error " +
                                 std::to_string(error) + ").");
    }
    DWORD exitCode = 1;
    if (!GetExitCodeProcess(process.hProcess, &exitCode)) {
        const DWORD error = GetLastError();
        CloseHandle(process.hProcess);
        throw std::runtime_error("Unable to read the extraction result (Windows error " +
                                 std::to_string(error) + ").");
    }
    CloseHandle(process.hProcess);
    if (exitCode != 0) {
        throw std::runtime_error("Windows PowerShell could not extract the Opera core archive.");
    }
}

} // namespace

std::filesystem::path CoreDownloader::downloadAndExtract(const std::filesystem::path& coreDirectory) const {
    std::filesystem::create_directories(coreDirectory);
    std::filesystem::path temporaryDirectory;
    const std::string prefix = "opera_core_download_" + std::to_string(GetCurrentProcessId()) +
                               "_" + std::to_string(GetTickCount64()) + "_";
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        const auto candidate = coreDirectory / (prefix + std::to_string(attempt));
        std::error_code error;
        if (std::filesystem::create_directory(candidate, error)) {
            temporaryDirectory = candidate;
            break;
        }
        if (error) {
            throw std::filesystem::filesystem_error("Unable to create the temporary core download directory",
                                                    candidate, error);
        }
    }
    if (temporaryDirectory.empty()) {
        throw std::runtime_error("Unable to allocate a unique temporary core download directory.");
    }
    const TemporaryFiles cleanup(temporaryDirectory);
    const auto archive = temporaryDirectory / L"opera_libretro.dll.zip";
    const auto extracted = temporaryDirectory / L"extracted";
    std::filesystem::create_directory(extracted);

    downloadZip(archive);
    extractZip(archive, extracted);

    std::filesystem::path extractedCore;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(extracted)) {
        if (entry.is_regular_file() && entry.path().filename() == L"opera_libretro.dll") {
            extractedCore = entry.path();
            break;
        }
    }
    if (extractedCore.empty() || std::filesystem::file_size(extractedCore) == 0) {
        throw std::runtime_error("The downloaded archive did not contain opera_libretro.dll.");
    }

    const auto target = coreDirectory / L"opera_libretro.dll";
    const auto stagedCore = temporaryDirectory / L"opera_libretro.dll.new";
    std::filesystem::copy_file(extractedCore, stagedCore, std::filesystem::copy_options::overwrite_existing);
    if (!MoveFileExW(stagedCore.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw std::runtime_error("Unable to install opera_libretro.dll (Windows error " +
                                 std::to_string(GetLastError()) + ").");
    }
    return target;
}

} // namespace app
