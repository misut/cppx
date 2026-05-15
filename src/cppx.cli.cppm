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
    std::string category;
    std::vector<std::string> value_hints;
    bool hidden = false;
};

struct CommandSpec {
    std::string name;
    std::vector<std::string> aliases;
    std::string summary;
    std::string description;
    std::vector<OptionSpec> options;
    std::vector<CommandSpec> subcommands;
    bool allow_positionals = true;
    std::string category;
    std::string positional_name;
    std::string positional_description;
    std::vector<std::string> examples;
    bool hidden = false;
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

enum class CompletionKind {
    command,
    option,
    option_value,
    positional,
};

enum class CompletionShell {
    bash,
    zsh,
    fish,
};

struct CompletionCandidate {
    CompletionKind kind = CompletionKind::command;
    std::string value;
    std::string description;
    std::string value_name;
    std::string category;
    bool append_space = true;
};

struct CompletionContext {
    std::vector<std::string> command_path;
    bool after_terminator = false;
    bool expects_option_value = false;
    std::string option_name;
};

struct CompletionResult {
    CompletionContext context;
    std::vector<CompletionCandidate> candidates;
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

inline bool starts_with(std::string_view value, std::string_view prefix) {
    return prefix.empty() || value.starts_with(prefix);
}

inline std::string shell_function_name(std::string_view command_name) {
    auto out = std::string{};
    out.reserve(command_name.size());
    for (auto ch : command_name) {
        auto const byte = static_cast<unsigned char>(ch);
        if (std::isalnum(byte) || ch == '_')
            out.push_back(ch);
        else
            out.push_back('_');
    }
    if (out.empty())
        out = "tool";
    return out;
}

inline void add_command_candidate(std::vector<CompletionCandidate>& out,
                                  CommandSpec const& command,
                                  std::string_view value,
                                  std::string_view prefix) {
    if (command.hidden || !starts_with(value, prefix))
        return;
    out.push_back({
        .kind = CompletionKind::command,
        .value = std::string{value},
        .description = command.summary,
        .category = command.category,
    });
}

inline void add_option_candidate(std::vector<CompletionCandidate>& out,
                                 OptionSpec const& option,
                                 std::string_view value,
                                 std::string_view prefix) {
    if (option.hidden || !starts_with(value, prefix))
        return;
    out.push_back({
        .kind = CompletionKind::option,
        .value = std::string{value},
        .description = option.description,
        .value_name = option.value_name,
        .category = option.category,
        .append_space = option.arity == OptionArity::none,
    });
}

inline void add_option_value_candidates(std::vector<CompletionCandidate>& out,
                                        OptionSpec const& option,
                                        std::string_view prefix) {
    for (auto const& value : option.value_hints) {
        if (!starts_with(value, prefix))
            continue;
        out.push_back({
            .kind = CompletionKind::option_value,
            .value = value,
            .description = option.description,
            .value_name = option.value_name,
        });
    }
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

std::vector<std::string> command_names(CommandSpec const& spec,
                                       bool include_aliases = false,
                                       bool include_hidden = false) {
    auto names = std::vector<std::string>{};
    for (auto const& subcommand : spec.subcommands) {
        if (subcommand.hidden && !include_hidden)
            continue;
        names.push_back(subcommand.name);
        if (include_aliases)
            names.insert(names.end(),
                         subcommand.aliases.begin(),
                         subcommand.aliases.end());
    }
    return names;
}

std::string_view option_arity_name(OptionArity arity) {
    switch (arity) {
    case OptionArity::none:
        return "none";
    case OptionArity::one:
        return "one";
    }
    return "none";
}

std::string_view completion_kind_name(CompletionKind kind) {
    switch (kind) {
    case CompletionKind::command:
        return "command";
    case CompletionKind::option:
        return "option";
    case CompletionKind::option_value:
        return "option_value";
    case CompletionKind::positional:
        return "positional";
    }
    return "command";
}

std::string_view completion_shell_name(CompletionShell shell) {
    switch (shell) {
    case CompletionShell::bash:
        return "bash";
    case CompletionShell::zsh:
        return "zsh";
    case CompletionShell::fish:
        return "fish";
    }
    return "bash";
}

std::optional<CompletionShell> parse_completion_shell(std::string_view value) {
    if (value == "bash")
        return CompletionShell::bash;
    if (value == "zsh")
        return CompletionShell::zsh;
    if (value == "fish")
        return CompletionShell::fish;
    return std::nullopt;
}

std::string json_escape(std::string_view text) {
    auto out = std::string{};
    out.reserve(text.size());
    for (auto ch : text) {
        switch (ch) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                out += std::format("\\u{:04x}", static_cast<unsigned char>(ch));
            } else {
                out.push_back(ch);
            }
            break;
        }
    }
    return out;
}

std::string json_string(std::string_view text) {
    return std::format("\"{}\"", json_escape(text));
}

std::string json_string_array(std::span<std::string const> values) {
    auto out = std::string{"["};
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0)
            out += ",";
        out += json_string(values[i]);
    }
    out += "]";
    return out;
}

std::string option_metadata_json(OptionSpec const& option) {
    auto out = std::format(
        "{{\"name\":{},\"arity\":{},\"repeatable\":{},\"required\":{}",
        json_string(option.name),
        json_string(option_arity_name(option.arity)),
        option.repeatable ? "true" : "false",
        option.required ? "true" : "false");
    if (option.short_name != '\0') {
        out += std::format(",\"short_name\":{}",
                           json_string(std::string{option.short_name}));
    } else {
        out += ",\"short_name\":null";
    }
    out += std::format(
        ",\"value_name\":{},\"description\":{},\"category\":{},\"value_hints\":{},\"hidden\":{}}}",
        json_string(option.value_name),
        json_string(option.description),
        json_string(option.category),
        json_string_array(option.value_hints),
        option.hidden ? "true" : "false");
    return out;
}

std::string option_metadata_array_json(std::span<OptionSpec const> options) {
    auto out = std::string{"["};
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (i > 0)
            out += ",";
        out += option_metadata_json(options[i]);
    }
    out += "]";
    return out;
}

std::string command_metadata_json(CommandSpec const& command) {
    return std::format(
        "{{\"name\":{},\"aliases\":{},\"summary\":{},\"description\":{},"
        "\"category\":{},\"positional_name\":{},\"positional_description\":{},"
        "\"allow_positionals\":{},\"hidden\":{},\"examples\":{},\"options\":{}}}",
        json_string(command.name),
        json_string_array(command.aliases),
        json_string(command.summary),
        json_string(command.description),
        json_string(command.category),
        json_string(command.positional_name),
        json_string(command.positional_description),
        command.allow_positionals ? "true" : "false",
        command.hidden ? "true" : "false",
        json_string_array(command.examples),
        option_metadata_array_json(command.options));
}

std::string command_catalog_json(CommandSpec const& root,
                                 bool include_hidden = false) {
    auto out = std::string{"["};
    auto first = true;
    for (auto const& command : root.subcommands) {
        if (command.hidden && !include_hidden)
            continue;
        if (!first)
            out += ",";
        first = false;
        out += command_metadata_json(command);
    }
    out += "]";
    return out;
}

std::string completion_context_json(CompletionContext const& context) {
    return std::format(
        "{{\"command_path\":{},\"after_terminator\":{},\"expects_option_value\":{},\"option_name\":{}}}",
        json_string_array(context.command_path),
        context.after_terminator ? "true" : "false",
        context.expects_option_value ? "true" : "false",
        json_string(context.option_name));
}

std::string completion_candidate_json(CompletionCandidate const& candidate) {
    return std::format(
        "{{\"kind\":{},\"value\":{},\"description\":{},\"value_name\":{},"
        "\"category\":{},\"append_space\":{}}}",
        json_string(completion_kind_name(candidate.kind)),
        json_string(candidate.value),
        json_string(candidate.description),
        json_string(candidate.value_name),
        json_string(candidate.category),
        candidate.append_space ? "true" : "false");
}

std::string
completion_candidates_json(std::span<CompletionCandidate const> candidates) {
    auto out = std::string{"["};
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (i > 0)
            out += ",";
        out += completion_candidate_json(candidates[i]);
    }
    out += "]";
    return out;
}

std::string completion_result_json(CompletionResult const& result) {
    return std::format(
        "{{\"context\":{},\"candidates\":{}}}",
        completion_context_json(result.context),
        completion_candidates_json(result.candidates));
}

std::string completion_script(std::string_view command_name,
                              CompletionShell shell) {
    auto command = std::string{command_name};
    auto function_name = detail::shell_function_name(command_name);
    switch (shell) {
    case CompletionShell::bash:
        return "# bash completion for " + command + "\n_" + function_name +
               "_complete()\n{\n"
               "    local -a candidates\n"
               "    mapfile -t candidates < <(" +
               command +
               " complete --output raw -- \"${COMP_WORDS[@]:1}\")\n"
               "    COMPREPLY=(\"${candidates[@]}\")\n"
               "}\n\n"
               "complete -F _" +
               function_name + "_complete " + command + "\n";
    case CompletionShell::zsh:
        return "#compdef " + command + "\n_" + function_name +
               "()\n{\n"
               "    local -a candidates\n"
               "    candidates=(\"${(@f)$(" +
               command +
               " complete --output raw -- ${words[@]:2})}\")\n"
               "    compadd -a candidates\n"
               "}\n\n"
               "_" +
               function_name + " \"$@\"\n";
    case CompletionShell::fish:
        return "function __" + function_name +
               "_complete\n"
               "    set -l tokens (commandline -opc)\n"
               "    set -e tokens[1]\n"
               "    set -l current (commandline -ct)\n"
               "    if test (count $tokens) -gt 0\n"
               "        set tokens[-1] $current\n"
               "    else\n"
               "        set tokens $current\n"
               "    end\n"
               "    " +
               command +
               " complete --output raw -- $tokens\n"
               "end\n\n"
               "complete -c " +
               command + " -f -a '(__" + function_name + "_complete)'\n";
    }
    return {};
}

std::string completion_script(CommandSpec const& root, CompletionShell shell) {
    return completion_script(root.name, shell);
}

CompletionResult complete(CommandSpec const& root,
                          std::span<std::string_view const> args,
                          std::string_view prefix = {}) {
    auto result = CompletionResult{};
    result.context.command_path.push_back(root.name);

    auto const* command = &root;
    auto const* value_option = static_cast<OptionSpec const*>(nullptr);
    auto after_options_started = false;

    for (std::size_t index = 0; index < args.size(); ++index) {
        auto token = args[index];

        if (value_option != nullptr) {
            value_option = nullptr;
            continue;
        }

        if (result.context.after_terminator)
            continue;

        if (token == "--") {
            result.context.after_terminator = true;
            continue;
        }

        if (token.starts_with("--") && token.size() > 2) {
            after_options_started = true;
            auto body = token.substr(2);
            auto eq = body.find('=');
            auto name = eq == std::string_view::npos ? body : body.substr(0, eq);
            auto const* option = detail::find_long_option(*command, name);
            if (option != nullptr && option->arity == OptionArity::one &&
                eq == std::string_view::npos) {
                value_option = option;
            }
            continue;
        }

        if (token.starts_with('-') && token.size() > 1) {
            after_options_started = true;
            auto name = token.back();
            auto const* option = detail::find_short_option(*command, name);
            if (option != nullptr && option->arity == OptionArity::one &&
                token.size() == 2) {
                value_option = option;
            }
            continue;
        }

        if (!after_options_started) {
            if (auto const* subcommand = detail::find_subcommand(*command, token)) {
                command = subcommand;
                result.context.command_path.push_back(command->name);
                continue;
            }
        }
    }

    if (value_option != nullptr) {
        result.context.expects_option_value = true;
        result.context.option_name = value_option->name;
        detail::add_option_value_candidates(result.candidates, *value_option, prefix);
        return result;
    }

    if (result.context.after_terminator) {
        if (command->allow_positionals && !command->positional_name.empty()) {
            result.candidates.push_back({
                .kind = CompletionKind::positional,
                .value = command->positional_name,
                .description = command->positional_description,
                .append_space = false,
            });
        }
        return result;
    }

    auto const wants_options = prefix.empty() || prefix.starts_with('-');
    auto const wants_commands = prefix.empty() || !prefix.starts_with('-');

    if (wants_commands && !after_options_started) {
        for (auto const& subcommand : command->subcommands) {
            detail::add_command_candidate(
                result.candidates,
                subcommand,
                subcommand.name,
                prefix);
            for (auto const& alias : subcommand.aliases) {
                detail::add_command_candidate(
                    result.candidates,
                    subcommand,
                    alias,
                    prefix);
            }
        }
    }

    if (wants_options) {
        for (auto const& option : command->options) {
            detail::add_option_candidate(
                result.candidates,
                option,
                std::format("--{}", option.name),
                prefix);
            if (option.short_name != '\0') {
                auto short_name = std::string{"-"};
                short_name.push_back(option.short_name);
                detail::add_option_candidate(
                    result.candidates,
                    option,
                    short_name,
                    prefix);
            }
        }
    }

    if (result.candidates.empty() && command->allow_positionals &&
        !command->positional_name.empty() && !prefix.starts_with('-')) {
        result.candidates.push_back({
            .kind = CompletionKind::positional,
            .value = command->positional_name,
            .description = command->positional_description,
            .append_space = false,
        });
    }

    return result;
}

CompletionResult complete(CommandSpec const& root,
                          std::vector<std::string_view> const& args,
                          std::string_view prefix = {}) {
    return complete(
        root,
        std::span<std::string_view const>{args.data(), args.size()},
        prefix);
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
        for (auto const& subcommand : spec.subcommands) {
            if (subcommand.hidden)
                continue;
            width = std::max(width, subcommand.name.size());
        }
        for (auto const& subcommand : spec.subcommands) {
            if (subcommand.hidden)
                continue;
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
            if (option.hidden)
                continue;
            auto display = detail::option_display(option);
            width = std::max(width, display.size());
            displays.push_back(std::move(display));
        }
        auto display_index = std::size_t{0};
        for (auto const& option : spec.options) {
            if (option.hidden)
                continue;
            out += std::format("  {:<{}}  {}\n",
                               displays[display_index++],
                               width,
                               option.description);
        }
    }

    if (!spec.examples.empty()) {
        out += "\nExamples:\n";
        for (auto const& example : spec.examples)
            out += std::format("  {}\n", example);
    }

    return out;
}

} // namespace cppx::cli
