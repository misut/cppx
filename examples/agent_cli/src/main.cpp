import cppx.cli;
import cppx.shell.system;
import cppx.terminal;
import std;

namespace {

auto spec() -> cppx::cli::CommandSpec {
    return {
        .name = "cppx-agent",
        .summary = "Reference agent CLI built from cppx primitives",
        .options = {
            {.name = "help", .short_name = 'h', .description = "Show help"},
        },
        .subcommands = {
            {
                .name = "exec",
                .summary = "Run one fake-agent turn",
                .options = {
                    {.name = "json", .arity = cppx::cli::OptionArity::none,
                     .description = "Emit JSON"},
                    {.name = "ephemeral", .arity = cppx::cli::OptionArity::none,
                     .description = "Do not persist a session"},
                },
            },
        },
    };
}

std::string read_stdin() {
    auto out = std::string{};
    auto line = std::string{};
    while (std::getline(std::cin, line)) {
        if (!out.empty())
            out.push_back('\n');
        out += line;
    }
    return out;
}

std::string json_escape(std::string_view text) {
    auto out = std::string{};
    for (auto ch : text) {
        switch (ch) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
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
            out.push_back(ch);
            break;
        }
    }
    return out;
}

std::string fake_agent(std::string_view prompt) {
    if (prompt.empty())
        return "No prompt provided.";
    return std::format("fake-agent: {}", prompt);
}

int run_exec(cppx::cli::Invocation const& invocation) {
    auto prompt = std::string{};
    if (!invocation.positionals.empty()) {
        prompt = invocation.positionals.front() == "-"
            ? read_stdin()
            : invocation.positionals.front();
    }

    auto response = fake_agent(prompt);
    if (invocation.has("json")) {
        std::println("{{\"response\":\"{}\",\"ephemeral\":{}}}",
                     json_escape(response),
                     invocation.has("ephemeral") ? "true" : "false");
    } else {
        std::println("{}", response);
    }
    return 0;
}

void print_interactive_help() {
    std::println("/help        Show commands");
    std::println("/clear       Clear the visible transcript");
    std::println("/jobs        Show background shell jobs");
    std::println("/kill <id>   Mark a background shell job as killed");
    std::println("/bg <cmd>    Run a shell command in the background");
    std::println("/exit        Exit");
    std::println("!<cmd>       Run a foreground shell command");
}

int run_interactive() {
    auto jobs = cppx::shell::system::JobRegistry{};
    auto history = cppx::terminal::CommandHistory{};
    auto line = std::string{};

    std::println("cppx-agent reference shell");
    print_interactive_help();

    for (;;) {
        std::print("> ");
        if (!std::getline(std::cin, line))
            break;

        auto input = cppx::terminal::classify_input(line);
        if (input.kind == cppx::terminal::InputKind::empty)
            continue;
        history.push(line);

        if (input.kind == cppx::terminal::InputKind::prompt) {
            std::println("{}", fake_agent(input.body));
            continue;
        }

        if (input.kind == cppx::terminal::InputKind::shell_command) {
            auto result = cppx::shell::system::run_foreground(input.body);
            if (!result) {
                std::println(std::cerr, "shell: {}", result.error());
            } else {
                std::print("{}", result->output);
                if (!result->output.ends_with('\n'))
                    std::println("");
            }
            continue;
        }

        auto command = input.body;
        if (command == "help") {
            print_interactive_help();
        } else if (command == "clear") {
            std::println("\x1b[2J\x1b[H");
        } else if (command == "exit" || command == "quit") {
            break;
        } else if (command == "jobs") {
            auto snapshots = jobs.snapshots();
            if (snapshots.empty()) {
                std::println("no jobs");
            }
            for (auto const& snap : snapshots) {
                std::println("#{} state={} exit={}{}",
                             snap.id,
                             static_cast<int>(snap.state),
                             snap.exit_code,
                             snap.timed_out ? " timed-out" : "");
                if (!snap.recent_output.empty())
                    std::print("{}", snap.recent_output);
            }
        } else if (command.starts_with("kill ")) {
            auto id = std::atoi(command.substr(5).c_str());
            std::println("{}", jobs.kill(id) ? "killed" : "unknown job");
        } else if (command.starts_with("bg ")) {
            auto id = jobs.start(command.substr(3));
            std::println("started #{}", id);
        } else {
            std::println("unknown slash command: {}", command);
        }
    }

    return 0;
}

} // namespace

int main(int argc, char** argv) {
    auto args = std::vector<std::string_view>{};
    for (int i = 1; i < argc; ++i)
        args.push_back(argv[i]);

    auto root = spec();
    if (args.empty())
        return run_interactive();
    if (args.size() == 1 && (args[0] == "--help" || args[0] == "-h")) {
        std::print("{}", cppx::cli::render_help(root));
        return 0;
    }

    auto parsed = cppx::cli::parse(root, args);
    if (!parsed) {
        std::println(std::cerr, "error: {}", parsed.error().message);
        if (parsed.error().suggestion)
            std::println(std::cerr, "hint: did you mean '{}'?",
                         *parsed.error().suggestion);
        return 2;
    }

    if (parsed->command_path.size() >= 2 && parsed->command_path[1] == "exec")
        return run_exec(*parsed);

    return run_interactive();
}
