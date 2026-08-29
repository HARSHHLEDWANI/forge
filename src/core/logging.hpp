#pragma once

#include <ostream>
#include <string_view>

namespace forge::core {

enum class LogLevel { Debug, Info, Warn, Error };

std::string_view to_string(LogLevel level) noexcept;

// An explicit, injectable logger: no global mutable state, no singleton.
// Callers own an instance and pass it where it is needed.
class Logger {
public:
    explicit Logger(std::ostream& sink, LogLevel level = LogLevel::Info) noexcept;

    void set_level(LogLevel level) noexcept;
    LogLevel level() const noexcept;

    void log(LogLevel level, std::string_view message) const;
    void debug(std::string_view message) const;
    void info(std::string_view message) const;
    void warn(std::string_view message) const;
    void error(std::string_view message) const;

private:
    std::ostream& sink_;
    LogLevel level_;
};

} // namespace forge::core
