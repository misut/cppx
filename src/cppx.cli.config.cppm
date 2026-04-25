// Source-agnostic CLI configuration overlay. Parsing TOML/JSON stays
// with consumers; this module only merges already-parsed key/value
// layers with an explicit precedence order.

export module cppx.cli.config;
import std;

export namespace cppx::cli::config {

enum class LayerKind {
    defaults,
    user,
    project,
    profile,
    flags,
};

inline constexpr std::string_view to_string(LayerKind kind) {
    switch (kind) {
    case LayerKind::defaults:
        return "defaults";
    case LayerKind::user:
        return "user";
    case LayerKind::project:
        return "project";
    case LayerKind::profile:
        return "profile";
    case LayerKind::flags:
        return "flags";
    }
    return "defaults";
}

struct Layer {
    LayerKind kind = LayerKind::defaults;
    std::string name;
    std::map<std::string, std::string> values;
};

struct Entry {
    std::string value;
    LayerKind source_kind = LayerKind::defaults;
    std::string source_name;
};

using ResolvedConfig = std::map<std::string, Entry>;

inline int rank(LayerKind kind) {
    switch (kind) {
    case LayerKind::defaults:
        return 0;
    case LayerKind::user:
        return 1;
    case LayerKind::project:
        return 2;
    case LayerKind::profile:
        return 3;
    case LayerKind::flags:
        return 4;
    }
    return 0;
}

ResolvedConfig merge(std::span<Layer const> layers) {
    auto sorted = std::vector<Layer const*>{};
    sorted.reserve(layers.size());
    for (auto const& layer : layers)
        sorted.push_back(&layer);

    std::ranges::stable_sort(sorted, [](Layer const* lhs, Layer const* rhs) {
        return rank(lhs->kind) > rank(rhs->kind);
    });

    auto resolved = ResolvedConfig{};
    for (auto const* layer : sorted) {
        for (auto const& [key, value] : layer->values) {
            if (resolved.contains(key))
                continue;
            resolved.emplace(key, Entry{
                .value = value,
                .source_kind = layer->kind,
                .source_name = layer->name.empty()
                    ? std::string{to_string(layer->kind)}
                    : layer->name,
            });
        }
    }
    return resolved;
}

std::optional<std::string_view> get(ResolvedConfig const& config,
                                    std::string_view key) {
    auto found = config.find(std::string{key});
    if (found == config.end())
        return std::nullopt;
    return std::string_view{found->second.value};
}

bool get_bool_or(ResolvedConfig const& config,
                 std::string_view key,
                 bool default_value) {
    auto value = get(config, key);
    if (!value)
        return default_value;
    auto lowered = std::string{};
    lowered.reserve(value->size());
    for (auto ch : *value)
        lowered.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch))));
    return lowered == "1" || lowered == "true" || lowered == "yes" ||
           lowered == "on";
}

} // namespace cppx::cli::config
