#pragma once

#include <filesystem>

namespace loom::platform {

// Returns the per-user writable directory for Loom's persistent state
// (pipeline cache, future user settings). Resolved as:
//   Linux:   $XDG_DATA_HOME/loom        (fallback $HOME/.local/share/loom)
//   macOS:   ~/Library/Caches/loom
//   Windows: %LOCALAPPDATA%/loom
//
// The directory is created lazily on first call. Throws if the OS user-data
// root cannot be resolved (no $HOME, no %LOCALAPPDATA%, etc.). A returned
// path that is not actually writable surfaces later via filesystem errors at
// the call site — the helper does not probe write permission.
[[nodiscard]] std::filesystem::path userDataDir();

}  // namespace loom::platform
