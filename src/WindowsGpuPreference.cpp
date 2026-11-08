#include "app/WindowsGpuPreference.hpp"

#include <windows.h>

#include <array>
#include <cwchar>
#include <stdexcept>
#include <string>
#include <system_error>

namespace app {
namespace {

class RegistryKey final {
public:
    explicit RegistryKey(HKEY key = nullptr) noexcept : m_key(key) {}
    ~RegistryKey() {
        if (m_key) RegCloseKey(m_key);
    }

    RegistryKey(const RegistryKey&) = delete;
    RegistryKey& operator=(const RegistryKey&) = delete;

    HKEY get() const noexcept { return m_key; }

private:
    HKEY m_key;
};

std::wstring currentExecutablePath() {
    std::array<wchar_t, 32768> path{};
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "Unable to determine the frontend executable path");
    }
    if (length >= path.size()) {
        throw std::runtime_error("The frontend executable path exceeds the Windows path limit.");
    }
    return std::wstring(path.data(), length);
}

} // namespace

void setWindowsGpuPreference(const std::string& preference) {
    if (preference != "system" && preference != "power_saving" &&
        preference != "high_performance") {
        throw std::invalid_argument("Unsupported Windows GPU preference: " + preference);
    }

    constexpr wchar_t preferenceKey[] = L"Software\\Microsoft\\DirectX\\UserGpuPreferences";
    HKEY rawKey = nullptr;
    const LSTATUS openStatus = RegCreateKeyExW(HKEY_CURRENT_USER, preferenceKey, 0, nullptr,
                                               REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                                               &rawKey, nullptr);
    if (openStatus != ERROR_SUCCESS) {
        throw std::system_error(static_cast<int>(openStatus), std::system_category(),
                                "Unable to open Windows GPU preferences");
    }
    const RegistryKey key(rawKey);
    const std::wstring executable = currentExecutablePath();

    if (preference == "system") {
        const LSTATUS deleteStatus = RegDeleteValueW(key.get(), executable.c_str());
        if (deleteStatus != ERROR_SUCCESS && deleteStatus != ERROR_FILE_NOT_FOUND) {
            throw std::system_error(static_cast<int>(deleteStatus), std::system_category(),
                                    "Unable to remove the frontend GPU preference");
        }
        return;
    }

    const wchar_t* gpuPreference = preference == "power_saving"
        ? L"GpuPreference=1;" : L"GpuPreference=2;";
    const DWORD byteCount = static_cast<DWORD>((std::wcslen(gpuPreference) + 1) * sizeof(wchar_t));
    const LSTATUS setStatus = RegSetValueExW(key.get(), executable.c_str(), 0, REG_SZ,
        reinterpret_cast<const BYTE*>(gpuPreference), byteCount);
    if (setStatus != ERROR_SUCCESS) {
        throw std::system_error(static_cast<int>(setStatus), std::system_category(),
                                "Unable to set the frontend GPU preference");
    }
}

} // namespace app
