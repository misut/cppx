import cppx.terminal;
import cppx.test;
import std;

cppx::test::context tc;

void test_capability_settings() {
    tc.check(cppx::terminal::parse_capability_setting("auto") ==
                 cppx::terminal::CapabilitySetting::auto_detect,
             "parse capability auto");
    tc.check(cppx::terminal::parse_capability_setting("always") ==
                 cppx::terminal::CapabilitySetting::always,
             "parse capability always");
    tc.check(cppx::terminal::parse_capability_setting("never") ==
                 cppx::terminal::CapabilitySetting::never,
             "parse capability never");
    tc.check(!cppx::terminal::parse_capability_setting("sometimes").has_value(),
             "reject invalid capability");
    tc.check(cppx::terminal::to_string(
                 cppx::terminal::CapabilitySetting::auto_detect) == "auto",
             "capability string auto");
}

void test_style_disabled() {
    tc.check(cppx::terminal::style(
                 "hello",
                 cppx::terminal::StyleRole::accent,
                 false) == "hello",
             "style disabled returns plain text");
}

void test_status_cell_width() {
    tc.check(cppx::terminal::status_cell(
                 cppx::terminal::StatusKind::ok,
                 false) == "OK     ",
             "ok status cell padded");
    tc.check(cppx::terminal::status_cell(
                 cppx::terminal::StatusKind::timeout,
                 false) == "TIMEOUT",
             "timeout status cell preserves width");
}

void test_status_badge() {
    tc.check(cppx::terminal::status_badge(
                 cppx::terminal::StatusKind::ok,
                 false,
                 false) == "+ OK",
             "status badge has ascii symbol and label");
    tc.check(cppx::terminal::status_badge(
                 cppx::terminal::StatusKind::fail,
                 false,
                 true) == "\u00d7 FAIL",
             "status badge has unicode symbol and label");
    tc.check(cppx::terminal::summary_line(
                 cppx::terminal::StatusKind::ok,
                 "toolchain ready",
                 false,
                 false) == "  + OK  toolchain ready",
             "summary line combines badge and message");
    tc.check(cppx::terminal::summary_line(
                 cppx::terminal::StatusKind::fail,
                 "needs attention",
                 false,
                 true) == "  \u00d7 FAIL  needs attention",
             "summary line supports unicode badge");
}

void test_key_value() {
    tc.check(cppx::terminal::key_value("target", "native") ==
                 "  target     native",
             "key value uses stable label column");
}

void test_section_header() {
    tc.check(cppx::terminal::section_header("terminal", false, false) ==
                 ":: terminal",
             "section header uses ascii accent");
    tc.check(cppx::terminal::section_header("terminal", false, true) ==
                 "\u25c6 terminal",
             "section header uses unicode accent");
}

void test_stage() {
    tc.check(cppx::terminal::stage("build", 4, 5) == "[4/5] build",
             "stage formats indexed title");
    tc.check(cppx::terminal::stage("build", 4, 5, "app (apps/app)") ==
                 "[4/5] [app (apps/app)] build",
             "stage formats context inline");
}

void test_tail_excerpt() {
    tc.check(cppx::terminal::tail_excerpt("short", 100) == "short",
             "tail excerpt keeps short text");
    tc.check(cppx::terminal::tail_excerpt("first\nsecond\nthird", 8) ==
                 "...\nthird",
             "tail excerpt starts at a line boundary when possible");
}

void test_progress_frame() {
    auto frame = cppx::terminal::format_progress_frame({
        .done = 12,
        .total = 56,
        .percent = 21,
        .label = "build",
    }, 0, false);
    tc.check(frame == "  RUN     [|] [12/56 21%] build",
             "progress frame uses status cell and spinner");

    auto detailed = cppx::terminal::format_progress_frame({
        .label = "discover",
        .detail = "  locked: github.com/misut/tomlcpp v0.4.0 (12345678)",
    }, 0, false);
    tc.check(detailed ==
                 "  RUN     [|] discover...\n"
                 "  locked: github.com/misut/tomlcpp v0.4.0 (12345678)",
             "progress frame renders transient detail on the next line");
}

void test_progress_frame_with_detail_lines() {
    auto frame = cppx::terminal::format_progress_frame({
        .done = 12,
        .total = 56,
        .percent = 21,
        .label = "build",
        .detail_lines = {"Building CXX object foo.o", "Linking app"},
    }, 0, false);
    tc.check(frame == "  RUN     [|] [12/56 21%] build\n"
                      "    Building CXX object foo.o\n"
                      "    Linking app",
             "progress frame appends detail lines");

    auto colored = cppx::terminal::format_progress_frame({
        .done = 12,
        .total = 56,
        .percent = 21,
        .label = "build",
        .detail_lines = {"Building CXX object foo.o"},
    }, 0, true);
    tc.check(colored.contains("\n\x1b[2m    Building CXX object foo.o\x1b[0m"),
             "progress frame dims detail lines when color is enabled");
}

void test_visual_progress_frame() {
    auto frame = cppx::terminal::format_progress_frame({
        .done = 12,
        .total = 56,
        .percent = 21,
        .label = "build",
    }, 0, cppx::terminal::ProgressRenderOptions{
        .show_bar = true,
        .bar_width = 10,
    });
    tc.check(frame == "  RUN     [|] [==--------] 21% [12/56] build",
             "visual progress frame adds an ascii progress bar");

    auto unicode = cppx::terminal::format_progress_frame({
        .done = 12,
        .total = 56,
        .percent = 21,
        .label = "build",
    }, 0, cppx::terminal::ProgressRenderOptions{
        .unicode_enabled = true,
        .show_bar = true,
        .bar_width = 4,
    });
    tc.check(unicode.contains("\u280b"), "unicode progress frame uses braille spinner");
    tc.check(unicode.contains("\u2591"), "unicode progress frame uses shaded bar");

    auto colored = cppx::terminal::format_progress_frame({
        .done = 12,
        .total = 56,
        .percent = 21,
        .label = "build",
    }, 0, cppx::terminal::ProgressRenderOptions{
        .color_enabled = true,
        .show_bar = true,
        .bar_width = 10,
    });
    tc.check(colored.contains("\x1b[32m==\x1b[0m\x1b[2m--------\x1b[0m"),
             "colored visual progress frame styles filled and remaining bar");
}

std::string active_char(char ch) {
    return std::format("\x1b[1m\x1b[97m{}\x1b[0m", ch);
}

std::string dim_char(char ch) {
    return std::format("\x1b[2m{}\x1b[0m", ch);
}

void test_shimmer_label() {
    tc.check(cppx::terminal::shimmer_label("build", 0, false) == "build",
             "shimmer label falls back when color is disabled");

    auto first = cppx::terminal::shimmer_label("build", 0, true);
    tc.check(first == active_char('b') + active_char('u') + dim_char('i') +
                      dim_char('l') + dim_char('d'),
             "shimmer label highlights from the left");

    auto later = cppx::terminal::shimmer_label("build", 3, true);
    tc.check(later == dim_char('b') + dim_char('u') + dim_char('i') +
                      active_char('l') + active_char('d'),
             "shimmer label moves right");
}

void test_key_event_parser_and_prompt_composer() {
    auto events = cppx::terminal::parse_key_events("ab\x1b[D\x7f\n");
    tc.check_eq(events.size(), std::size_t{5}, "key parser returns events");
    tc.check(events.at(0).code == cppx::terminal::KeyCode::character,
             "character event parsed");
    tc.check(events.at(2).code == cppx::terminal::KeyCode::arrow_left,
             "arrow-left escape parsed");
    tc.check(events.at(3).code == cppx::terminal::KeyCode::backspace,
             "backspace parsed");
    tc.check(events.at(4).code == cppx::terminal::KeyCode::enter,
             "enter parsed");

    auto composer = cppx::terminal::PromptComposer{};
    composer.apply({.code = cppx::terminal::KeyCode::character, .text = "a"});
    composer.apply({.code = cppx::terminal::KeyCode::character, .text = "b"});
    composer.apply({.code = cppx::terminal::KeyCode::arrow_left});
    composer.apply({.code = cppx::terminal::KeyCode::backspace});
    tc.check_eq(std::string{composer.text()}, std::string{"b"},
                "prompt composer edits at cursor");
}

void test_history_and_input_classification() {
    auto history = cppx::terminal::CommandHistory{};
    history.push("first");
    history.push("second");
    tc.check(history.previous() == std::optional<std::string_view>{"second"},
             "history previous returns newest");
    tc.check(history.previous() == std::optional<std::string_view>{"first"},
             "history previous walks backward");
    tc.check(history.next() == std::optional<std::string_view>{"second"},
             "history next walks forward");

    auto shell = cppx::terminal::classify_input("  !echo ok  ");
    tc.check(shell.kind == cppx::terminal::InputKind::shell_command,
             "shell command classified");
    tc.check_eq(shell.body, std::string{"echo ok"}, "shell command body trimmed");

    auto slash = cppx::terminal::classify_input("/help");
    tc.check(slash.kind == cppx::terminal::InputKind::slash_command,
             "slash command classified");
    tc.check_eq(slash.body, std::string{"help"}, "slash command body");
}

void test_status_frame() {
    auto lines = std::array{
        cppx::terminal::StatusLine{
            .label = "model",
            .value = "fake",
            .status = cppx::terminal::StatusKind::ok,
        },
    };
    auto frame = cppx::terminal::format_status_frame(lines, false);
    tc.check(frame.contains("OK"), "status frame includes status");
    tc.check(frame.contains("model"), "status frame includes label");
    tc.check(frame.contains("fake"), "status frame includes value");
}

void test_diagnostic_formatting() {
    auto line = cppx::terminal::diagnostic_line(
        cppx::terminal::DiagnosticSeverity::warning,
        "cache is stale",
        false);
    tc.check_eq(line, std::string{"warning: cache is stale"},
                "diagnostic line includes severity label");

    auto hint = cppx::terminal::hint_line("rerun with --output wrapped", false);
    tc.check_eq(hint, std::string{"hint: rerun with --output wrapped"},
                "hint line uses shared prefix");

    auto diagnostic = cppx::terminal::format_diagnostic({
        .severity = cppx::terminal::DiagnosticSeverity::error,
        .message = "build failed",
        .context = "build",
        .hints = {"rerun with --output wrapped"},
    }, false);
    tc.check_eq(diagnostic,
                std::string{"error: [build] build failed\n"
                            "  hint: rerun with --output wrapped"},
                "diagnostic formatter appends hints");
}

void test_github_actions_formatting() {
    tc.check_eq(cppx::terminal::github_actions_group_start("build\nphase"),
                std::string{"::group::build%0Aphase"},
                "github group title is escaped");
    tc.check_eq(cppx::terminal::github_actions_group_end(),
                std::string{"::endgroup::"},
                "github group end command");

    auto annotation = cppx::terminal::github_actions_annotation({
        .severity = cppx::terminal::DiagnosticSeverity::warning,
        .message = "bad\nthing",
        .title = "build, warning",
        .file = "src/a:b.cppm",
        .line = 7,
        .column = 3,
    });
    tc.check_eq(annotation,
                std::string{
                    "::warning file=src/a%3Ab.cppm,line=7,col=3,"
                    "title=build%2C warning::bad%0Athing"},
                "github annotation escapes properties and data");
}

int main() {
    test_capability_settings();
    test_style_disabled();
    test_status_cell_width();
    test_status_badge();
    test_key_value();
    test_section_header();
    test_stage();
    test_tail_excerpt();
    test_progress_frame();
    test_progress_frame_with_detail_lines();
    test_visual_progress_frame();
    test_shimmer_label();
    test_key_event_parser_and_prompt_composer();
    test_history_and_input_classification();
    test_status_frame();
    test_diagnostic_formatting();
    test_github_actions_formatting();
    return tc.summary("cppx.terminal");
}
