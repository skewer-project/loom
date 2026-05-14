#pragma once

#include <sstream>
#include <string>
#include <string_view>
#include <utility>

// Loom logger façade.
//
// All shippable code should route output through `loom::log::*` rather than
// raw std::cout / std::cerr. v1 forwards to iostream with severity + ISO
// timestamp prefix; the implementation is the swappable surface — Tracy,
// spdlog, structured JSON, etc. can replace `Log.cpp` without touching call
// sites.
//
// Thread-safe (internal mutex). Cost is O(arg count) string construction; do
// not call inside the hottest loops without an enclosing severity filter.

namespace loom::log {

enum class Severity : uint8_t { Trace, Debug, Info, Warn, Error };

// Backend entry point: take an already-formatted string. The variadic
// templates below stream args through std::ostringstream and forward here.
void logMessage(Severity severity, std::string_view message);

namespace detail {

template <typename... Args>
inline std::string formatArgs(Args&&... args) {
    std::ostringstream oss;
    ((oss << std::forward<Args>(args)), ...);
    return oss.str();
}

}  // namespace detail

template <typename... Args>
inline void trace(Args&&... args) {
    logMessage(Severity::Trace, detail::formatArgs(std::forward<Args>(args)...));
}
template <typename... Args>
inline void debug(Args&&... args) {
    logMessage(Severity::Debug, detail::formatArgs(std::forward<Args>(args)...));
}
template <typename... Args>
inline void info(Args&&... args) {
    logMessage(Severity::Info, detail::formatArgs(std::forward<Args>(args)...));
}
template <typename... Args>
inline void warn(Args&&... args) {
    logMessage(Severity::Warn, detail::formatArgs(std::forward<Args>(args)...));
}
template <typename... Args>
inline void error(Args&&... args) {
    logMessage(Severity::Error, detail::formatArgs(std::forward<Args>(args)...));
}

}  // namespace loom::log
