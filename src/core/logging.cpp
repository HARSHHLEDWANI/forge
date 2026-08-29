#include "core/logging.hpp"

namespace forge::core {

std::string_view to_string(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "UNKNOWN";
}

Logger::Logger(std::ostream& sink, LogLevel level) noexcept
    : sink_(sink), level_(level) {}

void Logger::set_level(LogLevel level) noexcept { level_ = level; }

LogLevel Logger::level() const noexcept { return level_; }

void Logger::log(LogLevel level, std::string_view message) const {
    if (level < level_) {
        return;
    }
    sink_ << '[' << to_string(level) << "] " << message << '\n';
}

void Logger::debug(std::string_view message) const { log(LogLevel::Debug, message); }
void Logger::info(std::string_view message) const { log(LogLevel::Info, message); }
void Logger::warn(std::string_view message) const { log(LogLevel::Warn, message); }
void Logger::error(std::string_view message) const { log(LogLevel::Error, message); }

} // namespace forge::core
