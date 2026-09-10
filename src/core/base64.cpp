#include "core/base64.hpp"

#include <array>
#include <cstdint>

namespace forge::core {

namespace {

constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string encode(std::string_view bytes, bool pad) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);

    std::size_t i = 0;
    while (i + 3 <= bytes.size()) {
        const std::uint32_t chunk = (static_cast<std::uint8_t>(bytes[i]) << 16) |
                                     (static_cast<std::uint8_t>(bytes[i + 1]) << 8) | static_cast<std::uint8_t>(bytes[i + 2]);
        out.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 6) & 0x3F]);
        out.push_back(kAlphabet[chunk & 0x3F]);
        i += 3;
    }

    const std::size_t remaining = bytes.size() - i;
    if (remaining == 1) {
        const std::uint32_t chunk = static_cast<std::uint8_t>(bytes[i]) << 16;
        out.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        if (pad) {
            out += "==";
        }
    } else if (remaining == 2) {
        const std::uint32_t chunk =
            (static_cast<std::uint8_t>(bytes[i]) << 16) | (static_cast<std::uint8_t>(bytes[i + 1]) << 8);
        out.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 6) & 0x3F]);
        if (pad) {
            out.push_back('=');
        }
    }

    return out;
}

// Inverse lookup, built once: kAlphabet[i] -> i, everything else -> -1.
const std::array<int, 256>& decode_table() {
    static const std::array<int, 256> table = [] {
        std::array<int, 256> t{};
        t.fill(-1);
        for (std::size_t i = 0; i < kAlphabet.size(); ++i) {
            t[static_cast<unsigned char>(kAlphabet[i])] = static_cast<int>(i);
        }
        return t;
    }();
    return table;
}

} // namespace

std::string base64_encode(std::string_view bytes) { return encode(bytes, /*pad=*/true); }

std::string base64_encode_unpadded(std::string_view bytes) { return encode(bytes, /*pad=*/false); }

std::optional<std::string> base64_decode(std::string_view text) {
    // Padding is optional on the way in (accepts both encode() forms);
    // if present, it must be well-formed (0-2 trailing '=', on a
    // 4-aligned length).
    std::string_view trimmed = text;
    while (!trimmed.empty() && trimmed.back() == '=') {
        trimmed.remove_suffix(1);
    }
    if (text.size() - trimmed.size() > 2) {
        return std::nullopt;
    }

    const std::array<int, 256>& table = decode_table();
    std::string out;
    out.reserve(trimmed.size() / 4 * 3 + 3);

    std::uint32_t buffer = 0;
    int bits = 0;
    for (const char c : trimmed) {
        const int value = table[static_cast<unsigned char>(c)];
        if (value < 0) {
            return std::nullopt;
        }
        buffer = (buffer << 6) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }
    // Any leftover bits must be zero padding, not encoded data —
    // otherwise this wasn't a valid multiple-of-4-characters encoding.
    if (bits >= 6 || (buffer & ((1u << bits) - 1)) != 0) {
        return std::nullopt;
    }

    return out;
}

} // namespace forge::core
