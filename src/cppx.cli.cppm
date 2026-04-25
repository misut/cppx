// Pure command-line parsing helpers for tools that want a small,
// dependency-free CLI surface. System I/O stays with the consuming
// executable.

export module cppx.cli;
import std;

export namespace cppx::cli {

enum class OptionArity {
    none,
    one,
};

enum class ParseErrorCode {
    unknown_command,
    unknown_option,
    missing_value,
    unexpected_value,
    missing_required_option,
};

struct OptionSpec {
    std::string name;
    char short_name = '\0';
    OptionArity arity = OptionArity::none;
    bool repeatable = false;
    bool required = false;
    std::string value_name;
    std::string description;
};

struct CommandSpec {
    std::string name;
    std::vector<std::string> aliases;
    std::string summary;
    std::string description;
    std::vector<OptionSpec> options;
    std::vector<CommandSpec> subcommands;
    bool allow_positionals = true;
};

struct Invocation {
    std::vector<std::string> command_path;
    std::map<std::string, std::vector<std::string>> options;
    std::vector<std::string> positionals;
    bool terminator_seen = false;

    bool has(std::string_view name) const {
        return options.contains(std::string{name});
    }

    std::optional<std::string_view> value(std::string_view name) const {
        auto found = options.find(std::string{name});
        if (found == options.end() || found->second.empty())
            return std::nullopt;
        return std::string_view{found->second.back()};
    }

    std::span<std::string const> values(std::string_view name) const {
        static auto const empty = std::vector<std::string>{};
        auto found = options.find(std::string{name});
        if (found == options.end())
            return empty;
        return found->second;
    }
};

struct ParseError {
    ParseErrorCode code = ParseErrorCode::unknown_option;
    std::string message;
    std::string token;
    std::optional<std::string> suggestion;
};

namespace detail {

inline bool command_matches(CommandSpec const& spec, std::string_view token) {
    if (spec.name == token)
        return true;
    return std::ranges::any_of(spec.aliases, [&](std::string const& alias) {
        return alias == token;
    });
}

inline CommandSpec const* find_subcommand(CommandSpec const& spec,
                                          std::string_view token) {
    for (auto const& subcommand : spec.subcommands) {
        if (command_matches(subcommand, token))
            return &subcommand;
    }
    return nullptr;
}

inline OptionSpec const* find_long_option(CommandSpec const& spec,
                                          std::string_view name) {
    for (auto const& option : spec.options) {
        if (option.name == name)
            return &option;
    }
    return nullptr;
}

inline OptionSpec const* find_short_option(CommandSpec const& spec, char name) {
    for (auto const& option : spec.options) {
        if (option.short_name == name)
            return &option;
    }
    return nullptr;
}

inline std::size_t edit_distance(std::string_view lhs, std::string_view rhs) {
    auto previous = std::vector<std::size_t>(rhs.size() + 1);
    auto current = std::vector<std::size_t>(rhs.size() + 1);
    std::iota(previous.begin(), previous.end(), std::size_t{0});

    for (std::size_t i = 0; i < lhs.size(); ++i) {
        current[0] = i + 1;
        for (std::size_t j = 0; j < rhs.size(); ++j) {
            auto substitution = previous[j] + (lhs[i] == rhs[j] ? 0 : 1);
            current[j + 1] = std::min({
                previous[j + 1] + 1,
                current[j] + 1,
                substitution,
            });
        }
        previous.swap(current);
    }
    return previous.back();
}

inline std::string option_display(OptionSpec const& option) {
    auto out = std::string{};
    if (option.short_name != '\0')
        out += std::format("-{}, ", option.short_name);
    out += std::format("--{}", option.name);
    if (option.arity == OptionArity::one)
        out += std::format(" <{}>",
                           option.value_name.empty() ? "value"
                                                     : option.value_name);
    return out;
}

inline void add_option(Invocation& invocation,
                       OptionSpec const& option,
                       std::string value) {
    auto& values = invocation.options[option.name];
    if (!option.repeatable)
        values.clear();
    values.push_back(std::move(value));
}

inline ParseError make_error(ParseErrorCode code,
                             std::string token,
                             std::string message,
                             std::optional<std::string> suggestion = std::nullopt) {
    return {
        .code = code,
        .message = std::move(message),
        .token = std::move(token),
        .suggestion = std::move(suggestion),
    };
}

} // namespace detail

std::optional<std::string> suggest_command(CommandSpec const& spec,
                                           std::string_view token) {
    auto best_name = std::optional<std::string>{};
    auto best_distance = std::numeric_limits<std::size_t>::max();

    for (auto const& subcommand : spec.subcommands) {
        auto consider = [&](std::string_view name) {
            auto distance = detail::edit_distance(token, name);
            if (distance < best_distance) {
                best_distance = distance;
                best_name = std::string{name};
            }
        };
        consider(subcommand.name);
        for (auto const& alias : subcommand.aliases)
            consider(alias);
    }

    auto threshold = std::max<std::size_t>(2, token.size() / 3);
    if (best_name && best_distance <= threshold)
        return best_name;
    return std::nullopt;
}

std::expected<Invocation, ParseError>
parse(CommandSpec const& root, std::span<std::string_view const> args) {
    auto invocation = Invocation{};
    invocation.command_path.push_back(root.name);
    auto const* command = &root;

    auto index = std::size_t{0};
    while (index < args.size()) {
        auto token = args[index];
        if (token == "--") {
            invocation.terminator_seen = true;
            ++index;
            break;
        }

        if (token.starts_with('-'))
            break;

        if (auto const* subcommand = detail::find_subcommand(*command, token)) {
            command = subcommand;
            invocation.command_path.push_back(command->name);
            ++index;
            continue;
        }

        if (!command->subcommands.empty()) {
            auto suggestion = suggest_command(*command, token);
            return std::unexpected{detail::make_error(
                ParseErrorCode::unknown_command,
                std::string{token},
                std::format("unknown command '{}'", token),
                std::move(suggestion))};
        }
        break;
    }

    while (index < args.size()) {
        auto token = args[index++];

        if (invocation.terminator_seen) {
            invocation.positionals.push_back(std::string{token});
            continue;
        }
        if (token == "--") {
            invocation.terminator_seen = true;
            continue;
        }

        if (token.starts_with("--") && token.size() > 2) {
            auto body = token.substr(2);
            auto eq = body.find('=');
            auto name = eq == std::string_view::npos ? body : body.substr(0, eq);
            auto const* option = detail::find_long_option(*command, name);
            if (option == nullptr) {
                return std::unexpected{detail::make_error(
                    ParseErrorCode::unknown_option,
                    std::string{token},
                    std::format("unknown option '--{}'", name))};
            }

            if (option->arity == OptionArity::none) {
                if (eq != std::string_view::npos) {
                    return std::unexpected{detail::make_error(
                        ParseErrorCode::unexpected_value,
                        std::string{token},
                        std::format("option '--{}' does not take a value", name))};
                }
                detail::add_option(invocation, *option, "true");
                continue;
            }

            if (eq != std::string_view::npos) {
                detail::add_option(invocation, *option,
                                   std::string{body.substr(eq + 1)});
                continue;
            }
            if (index >= args.size()) {
                return std::unexpected{detail::make_error(
                    ParseErrorCode::missing_value,
                    std::string{token},
                    std::format("option '--{}' requires a value", name))};
            }
            detail::add_option(invocation, *option, std::string{args[index++]});
            continue;
        }

        if (token.starts_with('-') && token.size() > 1) {
            for (std::size_t short_index = 1; short_index < token.size(); ++short_index) {
                auto name = token[short_index];
                auto const* option = detail::find_short_option(*command, name);
                if (option == nullptr) {
                    return std::unexpected{detail::make_error(
                        ParseErrorCode::unknown_option,
                        std::string{token},
                        std::format("unknown option '-{}'", name))};
                }
                if (option->arity == OptionArity::none) {
                    detail::add_option(invocation, *option, "true");
                    continue;
                }

                if (short_index + 1 < token.size()) {
                    detail::add_option(invocation, *option,
                                       std::string{token.substr(short_index + 1)});
                    break;
                }
                if (index >= args.size()) {
                    return std::unexpected{detail::make_error(
                        ParseErrorCode::missing_value,
                        std::string{token},
                        std::format("option '-{}' requires a value", name))};
                }
                detail::add_option(invocation, *option, std::string{args[index++]});
                break;
            }
            continue;
        }

        if (command->allow_positionals) {
            invocation.positionals.push_back(std::string{token});
            continue;
        }

        return std::unexpected{detail::make_error(
            ParseErrorCode::unknown_command,
            std::string{token},
            std::format("unexpected positional argument '{}'", token))};
    }

    for (auto const& option : command->options) {
        if (option.required && !invocation.has(option.name)) {
            return std::unexpected{detail::make_error(
                ParseErrorCode::missing_required_option,
                option.name,
                std::format("missing required option '--{}'", option.name))};
        }
    }

    return invocation;
}

std::expected<Invocation, ParseError>
parse(CommandSpec const& root, std::vector<std::string_view> const& args) {
    return parse(root, std::span<std::string_view const>{args.data(), args.size()});
}

std::string render_help(CommandSpec const& spec,
                        std::string_view program_name = {}) {
    auto name = program_name.empty() ? std::string_view{spec.name}
                                     : program_name;
    auto out = std::format("Usage: {}", name);
    if (!spec.subcommands.empty())
        out += " <command>";
    if (!spec.options.empty())
        out += " [options]";
    if (spec.allow_positionals)
        out += " [args...]";
    out += "\n";

    if (!spec.summary.empty())
        out += std::format("\n{}\n", spec.summary);
    if (!spec.description.empty())
        out += std::format("\n{}\n", spec.description);

    if (!spec.subcommands.empty()) {
        out += "\nCommands:\n";
        auto width = std::size_t{0};
        for (auto const& subcommand : spec.subcommands)
            width = std::max(width, subcommand.name.size());
        for (auto const& subcommand : spec.subcommands) {
            out += std::format("  {:<{}}  {}\n",
                               subcommand.name,
                               width,
                               subcommand.summary);
        }
    }

    if (!spec.options.empty()) {
        out += "\nOptions:\n";
        auto displays = std::vector<std::string>{};
        auto width = std::size_t{0};
        for (auto const& option : spec.options) {
            auto display = detail::option_display(option);
            width = std::max(width, display.size());
            displays.push_back(std::move(display));
        }
        for (std::size_t i = 0; i < spec.options.size(); ++i) {
            out += std::format("  {:<{}}  {}\n",
                               displays[i],
                               width,
                               spec.options[i].description);
        }
    }

    return out;
}

} // namespace cppx::cli
