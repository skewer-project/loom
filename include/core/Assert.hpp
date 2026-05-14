#pragma once

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

// Loom assertion helpers. These are always active — they survive NDEBUG. Use
// LOOM_ASSERT for safety-critical invariants whose violation indicates a
// programming bug (the process must terminate to prevent corruption). Use
// LOOM_VK_CHECK to wrap VkResult-returning Vulkan calls; non-VK_SUCCESS results
// throw loom::core::VulkanError with the call site and a translated result code.

namespace loom::core {

class VulkanError : public std::runtime_error {
  public:
    VulkanError(const std::string& what, int resultCode) noexcept
        : std::runtime_error(what), m_result(resultCode) {}

    int result() const noexcept { return m_result; }

  private:
    int m_result;
};

[[noreturn]] inline void loomAssertFail(const char* expr, const char* msg, const char* file,
                                        int line) noexcept {
    std::fprintf(stderr, "[loom][FATAL] %s:%d: LOOM_ASSERT(%s) failed: %s\n", file, line, expr,
                 msg ? msg : "(no message)");
    std::fflush(stderr);
    std::abort();
}

const char* vkResultToString(int result) noexcept;

}  // namespace loom::core

#define LOOM_ASSERT(cond, msg)                                              \
    do {                                                                    \
        if (!(cond)) {                                                      \
            ::loom::core::loomAssertFail(#cond, (msg), __FILE__, __LINE__); \
        }                                                                   \
    } while (0)

#define LOOM_VK_CHECK(expr)                                                                   \
    do {                                                                                      \
        VkResult _loom_vk_result = (expr);                                                    \
        if (_loom_vk_result != VK_SUCCESS) {                                                  \
            std::string _loom_vk_msg = std::string(#expr) + " failed at " __FILE__ ":" +      \
                                       std::to_string(__LINE__) + " (" +                      \
                                       ::loom::core::vkResultToString(_loom_vk_result) + ")"; \
            throw ::loom::core::VulkanError(_loom_vk_msg, static_cast<int>(_loom_vk_result)); \
        }                                                                                     \
    } while (0)
