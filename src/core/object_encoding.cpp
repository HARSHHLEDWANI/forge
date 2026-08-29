#include "core/object_encoding.hpp"

#include <charconv>

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

std::optional<DecodedObject> decode_canonical_object(std::string_view canonical_bytes) {
    const std::size_t space_pos = canonical_bytes.find(' ');
    if (space_pos == std::string_view::npos) {
        return std::nullopt;
    }

    const std::size_t nul_pos = canonical_bytes.find('\0', space_pos + 1);
    if (nul_pos == std::string_view::npos) {
        return std::nullopt;
    }

    const std::string_view type = canonical_bytes.substr(0, space_pos);
    const std::string_view size_field =
        canonical_bytes.substr(space_pos + 1, nul_pos - space_pos - 1);

    std::size_t declared_size = 0;
    const auto parse_result = std::from_chars(
        size_field.data(), size_field.data() + size_field.size(), declared_size);
    if (parse_result.ec != std::errc() || parse_result.ptr != size_field.data() + size_field.size()) {
        return std::nullopt;
    }

    const std::string_view payload = canonical_bytes.substr(nul_pos + 1);
    if (payload.size() != declared_size) {
        return std::nullopt;
    }

    return DecodedObject{std::string(type), std::string(payload)};
}

} // namespace forge::core
