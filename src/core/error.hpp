#pragma once

#include <stdexcept>

namespace forge::core {

// Thrown for internal invariant violations and unrecoverable failures
// (corrupt storage, I/O failures, broken preconditions). Expected,
// recoverable user-facing failures — such as bad CLI usage — are reported
// through return values instead, so callers are not forced to use
// exceptions for ordinary control flow.
class ForgeError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

} // namespace forge::core
