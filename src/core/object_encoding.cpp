#include "core/object_encoding.hpp"

namespace forge::core {

std::string encode_canonical_object(std::string_view type, std::string_view payload) {
    std::string result;
    result.reserve(type.size() + 1 + 20 + 1 + payload.size());
    result.append(type);
    result.push_back(' ');
    result.append(std::to_string(payload.size()));
    result.push_back('\0');
    result.append(payload);
    return result;
}

} // namespace forge::core
