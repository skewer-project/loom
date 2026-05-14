#include "core/Log.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace loom::log {

namespace {

std::mutex& logMutex() {
    static std::mutex m;
    return m;
}

const char* severityTag(Severity s) {
    switch (s) {
        case Severity::Trace:
            return "TRACE";
        case Severity::Debug:
            return "DEBUG";
        case Severity::Info:
            return "INFO ";
        case Severity::Warn:
            return "WARN ";
        case Severity::Error:
            return "ERROR";
    }
    return "?????";
}

std::ostream& streamFor(Severity s) {
    return (s == Severity::Warn || s == Severity::Error) ? std::cerr : std::cout;
}

std::string isoTimestamp() {
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const auto t = clock::to_time_t(now);
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm, "%H:%M:%S") << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

}  // namespace

void logMessage(Severity severity, std::string_view message) {
    std::lock_guard<std::mutex> lock(logMutex());
    streamFor(severity) << '[' << isoTimestamp() << "][" << severityTag(severity) << "] " << message
                        << '\n';
}

}  // namespace loom::log
