#pragma once

#include <string>

namespace forge::core {

// A blob is exactly its raw content — no additional structure. See
// data-model.md.
struct Blob {
    std::string content;
};

} // namespace forge::core
