#include "platform/UserDataDir.hpp"

#include <cstdlib>
#include <stdexcept>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <shlobj.h>
#include <windows.h>
#endif

namespace loom::platform {

namespace {

namespace fs = std::filesystem;

fs::path resolveBase() {
#if defined(_WIN32)
    PWSTR widePath = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &widePath);
    if (FAILED(hr) || widePath == nullptr) {
        if (widePath) CoTaskMemFree(widePath);
        throw std::runtime_error("Failed to resolve %LOCALAPPDATA%");
    }
    fs::path path(widePath);
    CoTaskMemFree(widePath);
    return path;
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home == nullptr) {
        throw std::runtime_error("Failed to resolve HOME");
    }
    return fs::path(home) / "Library" / "Caches";
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr && *xdg != '\0') {
        return fs::path(xdg);
    }
    const char* home = std::getenv("HOME");
    if (home == nullptr) {
        throw std::runtime_error("Failed to resolve XDG_DATA_HOME or HOME");
    }
    return fs::path(home) / ".local" / "share";
#endif
}

}  // namespace

fs::path userDataDir() {
    fs::path path = resolveBase() / "loom";
    std::error_code ec;
    fs::create_directories(path, ec);
    // create_directories failure is silent here — callers that actually need
    // to write will surface the error at write time.
    return path;
}

}  // namespace loom::platform
