#include <sstream>

#include "core/logging.hpp"
#include "support/test_framework.hpp"

using forge::core::LogLevel;
using forge::core::Logger;

FORGE_TEST_CASE(logger_writes_message_at_configured_level) {
    std::ostringstream out;
    const Logger logger(out, LogLevel::Info);
    logger.info("hello");
    FORGE_CHECK(out.str() == "[INFO] hello\n");
}

FORGE_TEST_CASE(logger_suppresses_messages_below_level) {
    std::ostringstream out;
    const Logger logger(out, LogLevel::Warn);
    logger.info("should not appear");
    FORGE_CHECK(out.str().empty());
}

FORGE_TEST_CASE(logger_allows_messages_above_level) {
    std::ostringstream out;
    const Logger logger(out, LogLevel::Warn);
    logger.error("should appear");
    FORGE_CHECK(out.str() == "[ERROR] should appear\n");
}

FORGE_TEST_CASE(logger_set_level_changes_threshold) {
    std::ostringstream out;
    Logger logger(out, LogLevel::Error);
    logger.set_level(LogLevel::Debug);
    FORGE_CHECK(logger.level() == LogLevel::Debug);
    logger.debug("now visible");
    FORGE_CHECK(out.str() == "[DEBUG] now visible\n");
}
