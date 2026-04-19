// Pure byte boundary types. These keep binary data handling explicit
// without exposing std::vector<std::byte> and std::span directly at
// higher-level module boundaries.

export module cppx.bytes;
import std;

namespace cppx::bytes::detail {

template <class Byte>
constexpr auto clamp_subspan(std::span<Byte> bytes,
                             std::size_t offset,
                             std::size_t count) -> std::span<Byte> {
    if (offset >= bytes.size())
        return {};

    auto remaining = bytes.size() - offset;
    if (count == std::dynamic_extent || count > remaining)
        count = remaining;
    return bytes.subspan(offset, count);
}

} // namespace cppx::bytes::detail

export namespace cppx::bytes {

class bytes_view {
    std::span<std::byte const> bytes_{};

public:
    constexpr bytes_view() = default;
    constexpr bytes_view(std::byte const* data, std::size_t size)
        : bytes_{data, size} {}
    constexpr bytes_view(std::span<std::byte const> bytes)
        : bytes_{bytes} {}
    constexpr bytes_view(std::span<std::byte> bytes)
        : bytes_{bytes.data(), bytes.size()} {}

    constexpr auto data() const -> std::byte const* { return bytes_.data(); }
    constexpr auto size() const -> std::size_t { return bytes_.size(); }
    constexpr auto empty() const -> bool { return bytes_.empty(); }

    constexpr auto view() const -> bytes_view { return *this; }

    constexpr auto subview(std::size_t offset,
                           std::size_t count = std::dynamic_extent) const
        -> bytes_view {
        auto sub = detail::clamp_subspan(bytes_, offset, count);
        return bytes_view{sub};
    }
};

class mutable_bytes_view {
    std::span<std::byte> bytes_{};

public:
    constexpr mutable_bytes_view() = default;
    constexpr mutable_bytes_view(std::byte* data, std::size_t size)
        : bytes_{data, size} {}
    constexpr mutable_bytes_view(std::span<std::byte> bytes)
        : bytes_{bytes} {}

    constexpr auto data() const -> std::byte* { return bytes_.data(); }
    constexpr auto size() const -> std::size_t { return bytes_.size(); }
    constexpr auto empty() const -> bool { return bytes_.empty(); }

    constexpr auto view() const -> bytes_view {
        return bytes_view{bytes_.data(), bytes_.size()};
    }

    constexpr auto mutable_view() const -> mutable_bytes_view { return *this; }

    constexpr auto subview(std::size_t offset,
                           std::size_t count = std::dynamic_extent) const
        -> mutable_bytes_view {
        auto sub = detail::clamp_subspan(bytes_, offset, count);
        return mutable_bytes_view{sub};
    }
};

class byte_buffer {
    std::vector<std::byte> bytes_{};

public:
    byte_buffer() = default;

    explicit byte_buffer(bytes_view bytes) {
        append(bytes);
    }

    auto data() -> std::byte* { return bytes_.data(); }
    auto data() const -> std::byte const* { return bytes_.data(); }
    auto size() const -> std::size_t { return bytes_.size(); }
    auto empty() const -> bool { return bytes_.empty(); }

    auto view() const -> bytes_view {
        return bytes_view{bytes_.data(), bytes_.size()};
    }

    auto mutable_view() -> mutable_bytes_view {
        return mutable_bytes_view{bytes_.data(), bytes_.size()};
    }

    auto subview(std::size_t offset,
                 std::size_t count = std::dynamic_extent) -> mutable_bytes_view {
        auto sub = detail::clamp_subspan(
            std::span<std::byte>{bytes_.data(), bytes_.size()},
            offset,
            count);
        return mutable_bytes_view{sub};
    }

    auto subview(std::size_t offset,
                 std::size_t count = std::dynamic_extent) const -> bytes_view {
        auto sub = detail::clamp_subspan(
            std::span<std::byte const>{bytes_.data(), bytes_.size()},
            offset,
            count);
        return bytes_view{sub};
    }

    void append(bytes_view bytes) {
        if (bytes.empty())
            return;
        bytes_.insert(bytes_.end(), bytes.data(), bytes.data() + bytes.size());
    }
};

} // namespace cppx::bytes
