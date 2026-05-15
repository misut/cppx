import cppx.cli;
import cppx.test;
import std;

cppx::test::context tc;

auto root_spec() -> cppx::cli::CommandSpec {
    return {
        .name = "agent",
        .summary = "agent shell",
        .options = {
            {.name = "verbose", .short_name = 'v', .description = "Verbose output"},
        },
        .subcommands = {
            {
                .name = "exec",
                .aliases = {"run"},
                .summary = "Run once",
                .options = {
                    {.name = "json", .arity = cppx::cli::OptionArity::none},
                    {.name = "model", .short_name = 'm',
                     .arity = cppx::cli::OptionArity::one,
                     .value_name = "model",
                     .description = "Model name",
                     .value_hints = {"haiku", "sonnet"}},
                    {.name = "file", .short_name = 'f',
                     .arity = cppx::cli::OptionArity::one,
                     .repeatable = true,
                     .value_name = "path"},
                },
            },
        },
    };
}

void test_subcommand_alias_options_and_terminator() {
    auto args = std::vector<std::string_view>{
        "run", "--json", "-m", "sonnet", "-fREADME.md", "--",
        "--literal",
    };
    auto parsed = cppx::cli::parse(root_spec(), args);
    tc.check(parsed.has_value(), "parse alias invocation");
    if (!parsed)
        return;

    tc.check_eq(parsed->command_path.back(), std::string{"exec"},
                "alias resolves canonical command");
    tc.check(parsed->has("json"), "flag is present");
    tc.check(parsed->value("model") == std::optional<std::string_view>{"sonnet"},
             "option value parsed");
    tc.check_eq(parsed->values("file").front(), std::string{"README.md"},
                "short option attached value parsed");
    tc.check(parsed->terminator_seen, "terminator recorded");
    tc.check_eq(parsed->positionals.front(), std::string{"--literal"},
                "terminator preserves following positional");
}

void test_unknown_command_suggestion() {
    auto args = std::vector<std::string_view>{"exce"};
    auto parsed = cppx::cli::parse(root_spec(), args);
    tc.check(!parsed, "unknown command fails");
    if (!parsed) {
        tc.check(parsed.error().code == cppx::cli::ParseErrorCode::unknown_command,
                 "unknown command error code");
        tc.check(parsed.error().suggestion ==
                     std::optional<std::string>{"exec"},
                 "unknown command carries suggestion");
    }
}

void test_missing_value_error() {
    auto args = std::vector<std::string_view>{"exec", "--model"};
    auto parsed = cppx::cli::parse(root_spec(), args);
    tc.check(!parsed, "missing option value fails");
    if (!parsed)
        tc.check(parsed.error().code == cppx::cli::ParseErrorCode::missing_value,
                 "missing value error code");
}

void test_help_renders_commands_and_options() {
    auto help = cppx::cli::render_help(root_spec(), "cppx-agent");
    tc.check(help.contains("Usage: cppx-agent <command> [options] [args...]"),
             "help contains usage");
    tc.check(help.contains("exec"), "help contains subcommand");
    tc.check(help.contains("--verbose"), "help contains root option");
}

void test_completion_candidates_for_commands_and_options() {
    auto empty = std::vector<std::string_view>{};
    auto commands = cppx::cli::complete(root_spec(), empty, "ex");
    tc.check_eq(commands.context.command_path.back(), std::string{"agent"},
                "completion context starts at root command");
    tc.check(!commands.candidates.empty(), "command completion has candidates");
    tc.check(commands.candidates.front().kind == cppx::cli::CompletionKind::command,
             "command completion candidate kind");
    tc.check_eq(commands.candidates.front().value, std::string{"exec"},
                "command completion includes canonical name");

    auto args = std::vector<std::string_view>{"exec"};
    auto options = cppx::cli::complete(root_spec(), args, "--m");
    tc.check_eq(options.context.command_path.back(), std::string{"exec"},
                "completion resolves subcommand context");
    tc.check(!options.candidates.empty(), "option completion has candidates");
    tc.check_eq(options.candidates.front().value, std::string{"--model"},
                "option completion includes long option");
    tc.check(!options.candidates.front().append_space,
             "value-taking option does not append a completion space");
}

void test_completion_candidates_for_option_values() {
    auto args = std::vector<std::string_view>{"exec", "--model"};
    auto values = cppx::cli::complete(root_spec(), args, "so");
    tc.check(values.context.expects_option_value,
             "completion context expects option value");
    tc.check_eq(values.context.option_name, std::string{"model"},
                "completion context carries option name");
    tc.check_eq(values.candidates.size(), std::size_t{1},
                "option value completion filters hints");
    tc.check_eq(values.candidates.front().value, std::string{"sonnet"},
                "option value completion returns hint");
    tc.check(values.candidates.front().kind ==
                 cppx::cli::CompletionKind::option_value,
             "option value completion candidate kind");
}

void test_metadata_json_helpers() {
    auto catalog = cppx::cli::command_catalog_json(root_spec());
    tc.check(catalog.contains("\"name\":\"exec\""),
             "command catalog json includes command name");
    tc.check(catalog.contains("\"aliases\":[\"run\"]"),
             "command catalog json includes aliases");
    tc.check(catalog.contains("\"name\":\"model\""),
             "command catalog json includes option name");
    tc.check(catalog.contains("\"arity\":\"one\""),
             "command catalog json includes option arity");
    tc.check(catalog.contains("\"value_hints\":[\"haiku\",\"sonnet\"]"),
             "command catalog json includes value hints");

    auto escaped = cppx::cli::json_string("a\"b\\c\n");
    tc.check_eq(escaped, std::string{"\"a\\\"b\\\\c\\n\""},
                "json string escapes special characters");
}

void test_completion_json_helpers() {
    auto args = std::vector<std::string_view>{"exec", "--model"};
    auto values = cppx::cli::complete(root_spec(), args, "so");
    auto json = cppx::cli::completion_result_json(values);
    tc.check(json.contains("\"expects_option_value\":true"),
             "completion json includes context flag");
    tc.check(json.contains("\"option_name\":\"model\""),
             "completion json includes option name");
    tc.check(json.contains("\"kind\":\"option_value\""),
             "completion json includes candidate kind");
    tc.check(json.contains("\"value\":\"sonnet\""),
             "completion json includes candidate value");
}

void test_completion_shell_scripts() {
    tc.check(cppx::cli::parse_completion_shell("bash") ==
                 std::optional{cppx::cli::CompletionShell::bash},
             "parse bash completion shell");
    tc.check(!cppx::cli::parse_completion_shell("powershell"),
             "reject unsupported completion shell");

    auto bash = cppx::cli::completion_script(root_spec(),
                                             cppx::cli::CompletionShell::bash);
    tc.check(bash.contains("# bash completion for agent"),
             "bash script includes header");
    tc.check(bash.contains("_agent_complete()"),
             "bash script includes function name");
    tc.check(bash.contains("agent complete --output raw"),
             "bash script calls raw completion command");

    auto zsh = cppx::cli::completion_script("agent",
                                            cppx::cli::CompletionShell::zsh);
    tc.check(zsh.contains("#compdef agent"), "zsh script includes compdef");
    tc.check(zsh.contains("_agent \"$@\""), "zsh script invokes function");

    auto fish = cppx::cli::completion_script("agent-tool",
                                             cppx::cli::CompletionShell::fish);
    tc.check(fish.contains("function __agent_tool_complete"),
             "fish script sanitizes function name");
    tc.check(fish.contains("complete -c agent-tool"),
             "fish script targets command name");
}

int main() {
    test_subcommand_alias_options_and_terminator();
    test_unknown_command_suggestion();
    test_missing_value_error();
    test_help_renders_commands_and_options();
    test_completion_candidates_for_commands_and_options();
    test_completion_candidates_for_option_values();
    test_metadata_json_helpers();
    test_completion_json_helpers();
    test_completion_shell_scripts();
    return tc.summary("cppx.cli");
}
