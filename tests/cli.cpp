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
                     .value_name = "model"},
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

int main() {
    test_subcommand_alias_options_and_terminator();
    test_unknown_command_suggestion();
    test_missing_value_error();
    test_help_renders_commands_and_options();
    return tc.summary("cppx.cli");
}
