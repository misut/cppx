import cppx.cli.config;
import cppx.test;
import std;

cppx::test::context tc;

void test_config_precedence() {
    auto layers = std::array{
        cppx::cli::config::Layer{
            .kind = cppx::cli::config::LayerKind::defaults,
            .name = "defaults",
            .values = {{"model", "default"}, {"approval", "ask"}},
        },
        cppx::cli::config::Layer{
            .kind = cppx::cli::config::LayerKind::user,
            .name = "user",
            .values = {{"model", "user"}},
        },
        cppx::cli::config::Layer{
            .kind = cppx::cli::config::LayerKind::project,
            .name = "project",
            .values = {{"sandbox", "workspace-write"}},
        },
        cppx::cli::config::Layer{
            .kind = cppx::cli::config::LayerKind::flags,
            .name = "flags",
            .values = {{"model", "flag"}, {"json", "true"}},
        },
    };

    auto merged = cppx::cli::config::merge(layers);
    tc.check(cppx::cli::config::get(merged, "model") ==
                 std::optional<std::string_view>{"flag"},
             "flags override lower-precedence config");
    tc.check(cppx::cli::config::get(merged, "approval") ==
                 std::optional<std::string_view>{"ask"},
             "default values survive when not overridden");
    tc.check(cppx::cli::config::get_bool_or(merged, "json", false),
             "bool helper parses true-ish values");
    tc.check_eq(merged.at("model").source_name, std::string{"flags"},
                "resolved entry records source layer");
}

int main() {
    test_config_precedence();
    return tc.summary("cppx.cli.config");
}
