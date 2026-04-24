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

void test_key_value() {
    tc.check(cppx::terminal::key_value("target", "native") ==
                 "  target     native",
             "key value uses stable label column");
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
}

std::string active_char(char ch) {
    return std::format("\x1b[1m\x1b[36m{}\x1b[0m", ch);
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

int main() {
    test_capability_settings();
    test_style_disabled();
    test_status_cell_width();
    test_key_value();
    test_stage();
    test_tail_excerpt();
    test_progress_frame();
    test_progress_frame_with_detail_lines();
    test_shimmer_label();
    return tc.summary("cppx.terminal");
}
