// Pure Unicode boundary helpers. Keeps encoding/decoding separate from
// platform APIs so consumers can share one tested implementation.

export module cppx.unicode;
import std;

export namespace cppx::unicode {

enum class unicode_error {
    invalid_utf8,
    invalid_utf16,
    invalid_code_point,
    truncated_sequence,
};

inline constexpr auto to_string(unicode_error error) -> std::string_view {
    switch (error) {
    case unicode_error::invalid_utf8:
        return "invalid_utf8";
    case unicode_error::invalid_utf16:
        return "invalid_utf16";
    case unicode_error::invalid_code_point:
        return "invalid_code_point";
    case unicode_error::truncated_sequence:
        return "truncated_sequence";
    }
    return "invalid_code_point";
}

struct utf8_range {
    std::size_t start = 0;
    std::size_t end = 0;
};

namespace detail {

inline bool is_continuation(unsigned char byte) {
    return (byte & 0xC0) == 0x80;
}

inline auto utf8_sequence_length(unsigned char byte) -> std::size_t {
    if (byte < 0x80)
        return 1;
    if (byte >= 0xC2 && byte <= 0xDF)
        return 2;
    if (byte >= 0xE0 && byte <= 0xEF)
        return 3;
    if (byte >= 0xF0 && byte <= 0xF4)
        return 4;
    return 1;
}

inline auto utf8_sequence_length(
        std::string_view value,
        std::size_t offset) -> std::size_t {
    if (offset >= value.size())
        return 0;
    auto const remaining = value.size() - offset;
    auto const width = utf8_sequence_length(
        static_cast<unsigned char>(value[offset]));
    return std::min(width, remaining);
}

inline auto utf16_units_for_utf8_lead(unsigned char byte) -> std::size_t {
    return (byte >= 0xF0 && byte <= 0xF4) ? 2u : 1u;
}

inline auto decode_utf8_code_point(
        std::string_view value,
        std::size_t& offset) -> std::expected<std::uint32_t, unicode_error> {
    auto const first = static_cast<unsigned char>(value[offset]);
    if (first < 0x80) {
        ++offset;
        return first;
    }

    std::size_t length = 0;
    std::uint32_t code_point = 0;
    std::uint32_t minimum = 0;
    if (first >= 0xC2 && first <= 0xDF) {
        length = 2;
        code_point = first & 0x1F;
        minimum = 0x80;
    } else if (first >= 0xE0 && first <= 0xEF) {
        length = 3;
        code_point = first & 0x0F;
        minimum = 0x800;
    } else if (first >= 0xF0 && first <= 0xF4) {
        length = 4;
        code_point = first & 0x07;
        minimum = 0x10000;
    } else {
        return std::unexpected{unicode_error::invalid_utf8};
    }

    if (offset + length > value.size())
        return std::unexpected{unicode_error::truncated_sequence};

    for (std::size_t i = 1; i < length; ++i) {
        auto const next = static_cast<unsigned char>(value[offset + i]);
        if (!is_continuation(next))
            return std::unexpected{unicode_error::invalid_utf8};
        code_point = (code_point << 6) | (next & 0x3F);
    }
    offset += length;

    if (code_point < minimum
        || code_point > 0x10FFFF
        || (code_point >= 0xD800 && code_point <= 0xDFFF)) {
        return std::unexpected{unicode_error::invalid_utf8};
    }
    return code_point;
}

inline auto append_utf16(
        std::uint32_t code_point,
        std::u16string& out) -> std::expected<void, unicode_error> {
    if (code_point > 0x10FFFF
        || (code_point >= 0xD800 && code_point <= 0xDFFF)) {
        return std::unexpected{unicode_error::invalid_code_point};
    }
    if (code_point <= 0xFFFF) {
        out.push_back(static_cast<char16_t>(code_point));
        return {};
    }

    code_point -= 0x10000;
    out.push_back(static_cast<char16_t>(0xD800 + (code_point >> 10)));
    out.push_back(static_cast<char16_t>(0xDC00 + (code_point & 0x3FF)));
    return {};
}

inline auto append_utf8(
        std::uint32_t code_point,
        std::string& out) -> std::expected<void, unicode_error> {
    if (code_point > 0x10FFFF
        || (code_point >= 0xD800 && code_point <= 0xDFFF)) {
        return std::unexpected{unicode_error::invalid_code_point};
    }

    if (code_point <= 0x7F) {
        out.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
    return {};
}

} // namespace detail

inline auto clamp_utf8_boundary(
        std::string_view text,
        std::size_t pos) -> std::size_t {
    pos = std::min(pos, text.size());
    while (pos > 0 && pos < text.size()
           && detail::is_continuation(static_cast<unsigned char>(text[pos]))) {
        --pos;
    }
    return pos;
}

inline auto prev_utf8_boundary(
        std::string_view text,
        std::size_t pos) -> std::size_t {
    pos = clamp_utf8_boundary(text, pos);
    if (pos == 0)
        return 0;
    --pos;
    while (pos > 0
           && detail::is_continuation(static_cast<unsigned char>(text[pos]))) {
        --pos;
    }
    return pos;
}

inline auto next_utf8_boundary(
        std::string_view text,
        std::size_t pos) -> std::size_t {
    pos = clamp_utf8_boundary(text, pos);
    if (pos >= text.size())
        return text.size();
    ++pos;
    while (pos < text.size()
           && detail::is_continuation(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
    return pos;
}

inline auto utf16_length(std::string_view utf8) -> std::size_t {
    std::size_t units = 0;
    for (std::size_t offset = 0; offset < utf8.size();) {
        auto const lead = static_cast<unsigned char>(utf8[offset]);
        units += detail::utf16_units_for_utf8_lead(lead);
        offset += detail::utf8_sequence_length(utf8, offset);
    }
    return units;
}

inline auto utf16_offset_to_utf8(
        std::string_view utf8,
        std::size_t utf16_units) -> std::size_t {
    if (utf8.empty() || utf16_units == 0)
        return 0;

    std::size_t offset = 0;
    std::size_t units = 0;
    while (offset < utf8.size()) {
        auto const lead = static_cast<unsigned char>(utf8[offset]);
        auto const next_units = units + detail::utf16_units_for_utf8_lead(lead);
        if (next_units > utf16_units)
            return offset;
        units = next_units;
        offset += detail::utf8_sequence_length(utf8, offset);
    }
    return utf8.size();
}

inline auto utf16_range_to_utf8(
        std::string_view utf8,
        std::size_t location,
        std::size_t length) -> utf8_range {
    auto const start = utf16_offset_to_utf8(utf8, location);
    auto end_units = location;
    if (std::numeric_limits<std::size_t>::max() - end_units < length)
        end_units = std::numeric_limits<std::size_t>::max();
    else
        end_units += length;

    auto end = utf16_offset_to_utf8(utf8, end_units);
    if (end < start)
        end = start;
    return {start, end};
}

inline auto utf8_to_codepoints(
        std::string_view value) -> std::expected<std::u32string, unicode_error> {
    auto out = std::u32string{};
    out.reserve(value.size());
    std::size_t offset = 0;
    while (offset < value.size()) {
        auto code_point = detail::decode_utf8_code_point(value, offset);
        if (!code_point)
            return std::unexpected{code_point.error()};
        out.push_back(static_cast<char32_t>(*code_point));
    }
    return out;
}

inline auto utf8_to_utf16(
        std::string_view value) -> std::expected<std::u16string, unicode_error> {
    auto code_points = utf8_to_codepoints(value);
    if (!code_points)
        return std::unexpected{code_points.error()};

    auto out = std::u16string{};
    out.reserve(code_points->size());
    for (auto code_point : *code_points) {
        auto appended = detail::append_utf16(
            static_cast<std::uint32_t>(code_point), out);
        if (!appended)
            return std::unexpected{appended.error()};
    }
    return out;
}

inline auto utf16_to_utf8(
        std::u16string_view value) -> std::expected<std::string, unicode_error> {
    auto out = std::string{};
    out.reserve(value.size());

    for (std::size_t i = 0; i < value.size(); ++i) {
        auto const unit = static_cast<std::uint32_t>(value[i]);
        if (unit >= 0xD800 && unit <= 0xDBFF) {
            if (i + 1 >= value.size())
                return std::unexpected{unicode_error::truncated_sequence};
            auto const low = static_cast<std::uint32_t>(value[i + 1]);
            if (low < 0xDC00 || low > 0xDFFF)
                return std::unexpected{unicode_error::invalid_utf16};
            auto const code_point =
                0x10000 + (((unit - 0xD800) << 10) | (low - 0xDC00));
            auto appended = detail::append_utf8(code_point, out);
            if (!appended)
                return std::unexpected{appended.error()};
            ++i;
            continue;
        }
        if (unit >= 0xDC00 && unit <= 0xDFFF)
            return std::unexpected{unicode_error::invalid_utf16};

        auto appended = detail::append_utf8(unit, out);
        if (!appended)
            return std::unexpected{appended.error()};
    }

    return out;
}

auto utf8_to_wide(
        std::string_view value) -> std::expected<std::wstring, unicode_error>;

auto wide_to_utf8(
        std::wstring_view value) -> std::expected<std::string, unicode_error>;

} // namespace cppx::unicode

module :private;

namespace cppx::unicode {

auto utf8_to_wide(
        std::string_view value) -> std::expected<std::wstring, unicode_error> {
    if constexpr (sizeof(wchar_t) == sizeof(char16_t)) {
        auto utf16 = utf8_to_utf16(value);
        if (!utf16)
            return std::unexpected{utf16.error()};

        auto out = std::wstring{};
        out.reserve(utf16->size());
        for (auto unit : *utf16)
            out.push_back(static_cast<wchar_t>(unit));
        return out;
    } else {
        auto code_points = utf8_to_codepoints(value);
        if (!code_points)
            return std::unexpected{code_points.error()};

        auto out = std::wstring{};
        out.reserve(code_points->size());
        for (auto code_point : *code_points) {
            if constexpr (std::is_signed_v<wchar_t>) {
                if (code_point > static_cast<std::uint32_t>(
                        std::numeric_limits<wchar_t>::max()))
                    return std::unexpected{unicode_error::invalid_code_point};
            }
            out.push_back(static_cast<wchar_t>(code_point));
        }
        return out;
    }
}

auto wide_to_utf8(
        std::wstring_view value) -> std::expected<std::string, unicode_error> {
    if constexpr (sizeof(wchar_t) == sizeof(char16_t)) {
        auto utf16 = std::u16string{};
        utf16.reserve(value.size());
        for (auto unit : value)
            utf16.push_back(static_cast<char16_t>(unit));
        return utf16_to_utf8(utf16);
    } else {
        auto out = std::string{};
        out.reserve(value.size());
        for (auto unit : value) {
            if constexpr (std::is_signed_v<wchar_t>) {
                if (unit < 0)
                    return std::unexpected{unicode_error::invalid_code_point};
            }
            auto const code_point = static_cast<std::uint32_t>(unit);
            auto appended = detail::append_utf8(code_point, out);
            if (!appended)
                return std::unexpected{appended.error()};
        }
        return out;
    }
}

} // namespace cppx::unicode
