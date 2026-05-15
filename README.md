# cppx

C++23 extension library for projects that want a small, composable
standard-library-first foundation. `cppx` provides aggregate
reflection, platform detection, environment and filesystem helpers,
process/archive/checksum utilities, CLI/shell/terminal primitives,
pseudo-terminal helpers, coroutine primitives, test
utilities, and HTTP building blocks. MIT License.

## What it is for

`cppx` is meant to solve the low-level glue once, then reuse it across
projects:

- pure-by-default reflection and environment helpers
- small filesystem, process, archive, and checksum utilities at the
  system boundary
- reusable terminal formatting, capability detection, and live progress
  helpers for CLI tools
- command parser, config overlay, shell command, shell job, and PTY
  primitives for agent-style CLIs
- coroutine primitives with deterministic test support
- small test utilities for standalone executables
- HTTP types, parser/serializer, client/server/transfer helpers, and
  system adapters

The library stays close to standard C++23: modules, `std::expected`,
`std::optional`, coroutines, and `import std;`.

## Modules

| Module | Purpose |
| --- | --- |
| `cppx` | Umbrella module. Re-exports `cppx.reflect`, `cppx.platform`, `cppx.env`, and `cppx.env.system`. |
| `cppx.bytes` | Pure byte boundary types: `byte_buffer`, `bytes_view`, and `mutable_bytes_view`. |
| `cppx.reflect` | Aggregate reflection: `Reflectable`, `tuple_size_v`, `get<I>`, `name_of<T, I>()`. |
| `cppx.platform` | Compile-time host detection: `OS`, `Arch`, `Platform`, `host()`. |
| `cppx.env` | Pure environment/path helpers such as `get`, `home_dir`, `find_in_path`, `shell_quote`. |
| `cppx.env.system` | System-backed environment/filesystem adapters for `cppx.env`. |
| `cppx.fs` | Filesystem-facing value types such as `TextWrite` and `fs_error`. |
| `cppx.fs.system` | System-backed text and byte read/write helpers such as `read_text`, `read_bytes`, `write_bytes`, `append_bytes`, `write_if_changed`, and `apply_writes`. |
| `cppx.resource` | Pure resource classification and resolution helpers for filesystem paths, `file:` URIs, and HTTP(S) locators. |
| `cppx.resource.system` | System-backed unified byte reads for local paths, local `file:` URIs, and HTTP(S) URLs. |
| `cppx.sync` | Thread-based producer/consumer queues plus a reusable single-thread background worker lifecycle wrapper. |
| `cppx.unicode` | Pure UTF-8 boundary helpers plus UTF-16/UTF-8 offset, range, and conversion utilities for platform boundaries. |
| `cppx.os` | OS-facing capability declarations such as `open_error`. |
| `cppx.os.system` | System-backed OS helpers such as `open_url`. |
| `cppx.process` | Process specs/results and `process_error` types for child-process work. |
| `cppx.process.system` | System-backed `run`, `capture`, `spawn`, event drain, and callback streaming helpers with timeout/cwd/env override support. |
| `cppx.cli` | Pure command parser, help renderer, subcommand aliases, repeatable flags, `--`, closest-command suggestions, and completion-friendly metadata. |
| `cppx.cli.config` | Source-agnostic config overlay for already-parsed defaults/user/project/profile/flag layers. |
| `cppx.shell` | Explicit shell command builders and POSIX/PowerShell/cmd quoting helpers. |
| `cppx.shell.system` | System-backed foreground shell execution plus background job registry helpers. |
| `cppx.pty.system` | Pseudo-terminal spawn/read/write/resize helpers on POSIX and explicit unsupported reporting elsewhere. |
| `cppx.terminal` | Pure terminal formatting and input primitives such as status cells, stages, diagnostics, hints, key parsing, prompt composition, history, tail excerpts, and progress frames. |
| `cppx.terminal.system` | System-backed terminal capability detection, `NO_COLOR`/TTY/CI handling, raw mode, Windows VT setup, and live progress rendering. |
| `cppx.archive` | Archive extraction specs and error types. |
| `cppx.archive.system` | System-backed archive extraction helpers. |
| `cppx.checksum` | Pure checksum parsing/normalization helpers and error types. |
| `cppx.checksum.system` | System-backed SHA-256 hashing helpers. |
| `cppx.async` | Coroutine primitives: `task<T>`, `generator<T>`, `executor_engine`, `run`, `async_scope`, `when_all`. |
| `cppx.net` | Shared network transport errors, sync stream/listener/TLS concepts, and null helpers. |
| `cppx.net.async` | Async transport concepts and null helpers built on `cppx.async::task`. |
| `cppx.async.system` | System event-loop executor, timer awaitables, and networking helpers for async I/O across POSIX and Windows. |
| `cppx.async.test` | Deterministic coroutine test executor with virtual time. |
| `cppx.async.system.test` | Scripted in-memory async listener/stream fakes for deterministic system-layer tests. |
| `cppx.test` | Minimal test helpers for standalone executables. |
| `cppx.http` | Core HTTP types, serializer, parser, concepts, and helpers. |
| `cppx.http.async` | Generic async HTTP client over pluggable async stream/TLS backends. |
| `cppx.http.client` | Generic HTTP client over pluggable stream/TLS backends. |
| `cppx.http.server` | Generic HTTP server, routing, static file serving, MIME helpers. |
| `cppx.http.transfer` | Transfer backend policy, result types, and fallback rules. |
| `cppx.http.system` | Platform-backed sockets/TLS plus convenience helpers like `get` and `download`. |
| `cppx.http.system.test` | Deterministic test double for the first-party sync HTTP facade. |
| `cppx.http.async.system` | First-party async HTTP facade over `cppx.async.system` for HTTP(S) requests. |
| `cppx.http.transfer.system` | First-party text/file transfer facade with bounded shell fallback. |

## Quick Start

### Reflection

```cpp
import cppx.reflect;
import std;

struct User {
    std::string name;
    int age;
};

static_assert(cppx::reflect::Reflectable<User>);
static_assert(cppx::reflect::tuple_size_v<User> == 2);
static_assert(cppx::reflect::name_of<User, 0>() == "name");
```

### Async with deterministic tests

```cpp
import cppx.async;
import cppx.async.test;
import std;

cppx::async::task<int> answer() {
    co_return 42;
}

int main() {
    auto value = cppx::async::test::run_test(
        [](cppx::async::test::test_executor&) -> cppx::async::task<int> {
            co_return co_await answer();
        });

    std::println("{}", value);
}
```

Use `import cppx.async.system.test;` when you want deterministic tests for
system-facing coroutine code without real sockets. Keep real-I/O validation
as an opt-in smoke run with `CPPX_RUN_ASYNC_SYSTEM_SMOKE=1`.

`cppx.async.system` now includes a first-party Windows backend, so the same
module surface stays available across desktop targets.

### Shared transport boundary

```cpp
import cppx.net;
import cppx.net.async;
```

Use `cppx.net` and `cppx.net.async` when you want protocol-independent
transport concepts or shared `net_error` handling outside the HTTP layer.

### Byte boundary

```cpp
import cppx.bytes;
import std;

int main() {
    auto bytes = cppx::bytes::byte_buffer{};
    auto raw = std::array{
        std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF},
    };
    bytes.append(cppx::bytes::bytes_view{std::span{raw}});

    std::println("payload={} bytes", bytes.size());
}
```

Use `cppx.bytes` when you want a stable binary boundary type without
passing `std::vector<std::byte>` and raw span/pointer pairs through your
own public APIs.

### Filesystem bytes

```cpp
import cppx.bytes;
import cppx.fs.system;
import std;

int main() {
    auto payload = cppx::bytes::byte_buffer{};
    auto header = std::array{std::byte{0x50}, std::byte{0x4B}};
    payload.append(cppx::bytes::bytes_view{std::span{header}});

    auto wrote = cppx::fs::system::write_bytes("sample.bin", payload.view());
    auto read = cppx::fs::system::read_bytes("sample.bin");
    if (!wrote || !read) return 1;

    std::println("roundtrip={} bytes", read->size());
}
```

### Unified resource bytes

```cpp
import cppx.resource.system;
import std;

int main() {
    auto local = cppx::resource::system::read_bytes(
        "/workspace/project",
        "assets/logo.bin");
    auto file_uri = cppx::resource::system::read_bytes(
        "/workspace/project",
        "file:///workspace/project/assets/logo.bin");
    auto remote = cppx::resource::system::read_bytes(
        "/workspace/project",
        "https://example.com/logo.bin");

    if (!local || !file_uri || !remote)
        return 1;

    std::println("local={} file={} remote={}",
                 local->size(),
                 file_uri->size(),
                 remote->size());
}
```

Use `cppx.resource.system` when you want one narrow read surface that
accepts relative paths, absolute paths, local `file:` URIs, and remote
HTTP(S) URLs while keeping filesystem and transport details inside
`cppx`.

### Work queues and background worker

```cpp
import cppx.sync;
import std;

int main() {
    cppx::sync::work_queue<int> queue;
    auto worker = cppx::sync::background_worker{
        [&](std::stop_token) {
            while (auto item = queue.wait_pop())
                std::println("work={}", *item);
        },
        {.on_stop = [&] { queue.close(); }},
    };

    queue.push(7);
    queue.push(9);
    worker.close();
}
```

Use `cppx.sync` when you want a small reusable queue + worker lifecycle
surface for background submission, duplicate suppression, and orderly
shutdown without introducing a scheduler or coroutine runtime.

### Terminal output

```cpp
import cppx.terminal;
import cppx.terminal.system;
import std;

int main() {
    auto options = cppx::terminal::TerminalOptions{
        .color_env = "MYTOOL_COLOR",
        .progress_env = "MYTOOL_PROGRESS",
    };
    auto color = cppx::terminal::system::stdout_color_enabled(options);
    auto caps = cppx::terminal::system::stdout_capabilities(options);

    std::println("{}",
                 cppx::terminal::section_header(
                     "build",
                     color,
                     caps.unicode_enabled));
    std::println("{}",
                 cppx::terminal::stage("build", 1, 3, "app", color));
    std::println("{}",
                 cppx::terminal::summary_line(
                     cppx::terminal::StatusKind::ok,
                     "artifact ready",
                     color,
                     caps.unicode_enabled));
}
```

`cppx.terminal` is pure and does not prescribe CLI policy. Tools keep
their own output modes and environment variable names, then pass those
choices into `TerminalOptions`. The system layer follows the `NO_COLOR`
and `FORCE_COLOR` conventions, treats `TERM=dumb` and generic CI as
non-interactive for live progress in auto mode, exposes a capability
snapshot for human renderers, and uses Windows virtual terminal processing
when it is available. `section_header` and `summary_line` provide small
visual accents for human output while preserving plain ASCII fallbacks.

Use `ProgressRenderOptions` for richer TTY frames while keeping an ASCII
fallback for logs and tests:

```cpp
auto frame = cppx::terminal::format_progress_frame(
    {.done = 12, .total = 56, .percent = 21, .label = "build"},
    0,
    {.color_enabled = color, .unicode_enabled = caps.unicode_enabled, .show_bar = true});
```

GitHub Actions log commands are formatted by pure helpers so tools can emit
CI annotations and collapsible groups without duplicating escaping rules:

```cpp
std::println("{}", cppx::terminal::github_actions_group_start("compiler output"));
std::println("{}", cppx::terminal::github_actions_group_end());
std::println("{}", cppx::terminal::github_actions_annotation({
    .severity = cppx::terminal::DiagnosticSeverity::error,
    .message = "build failed",
    .file = "src/app.cppm",
    .line = 42,
}));
```

Use the shared diagnostic and hint helpers when a tool wants consistent
human-facing messages without handing over its reporting model:

```cpp
import cppx.terminal;
import std;

int main() {
    auto message = cppx::terminal::format_diagnostic({
        .severity = cppx::terminal::DiagnosticSeverity::error,
        .message = "build step failed",
        .context = "build",
        .hints = {"rerun with --output wrapped to show full tool output"},
    });

    std::println("{}", message);
}
```

### CLI metadata and completions

```cpp
import cppx.cli;
import std;

int main() {
    auto root = cppx::cli::CommandSpec{
        .name = "tool",
        .subcommands = {{
            .name = "build",
            .summary = "Build a package",
            .options = {{
                .name = "profile",
                .arity = cppx::cli::OptionArity::one,
                .value_name = "name",
                .description = "Build profile",
                .value_hints = {"debug", "release"},
            }},
        }},
    };

    auto args = std::vector<std::string_view>{"build", "--profile"};
    auto completions = cppx::cli::complete(root, args, "re");
    for (auto const& candidate : completions.candidates)
        std::println("{}", candidate.value);

    std::println("{}", cppx::cli::command_catalog_json(root));
    std::println("{}",
                 cppx::cli::completion_script(
                     root,
                     cppx::cli::CompletionShell::bash));
}
```

`cppx.cli` keeps command metadata as plain aggregate data. Consumers can
render help, parse invocations, offer suggestions, emit command and
completion JSON, and generate bash/zsh/fish completion adapters without
adopting a policy-heavy framework.

### Streaming process events

```cpp
import cppx.process.system;
import std;

int main() {
    auto result = cppx::process::system::stream(
        {
            .program = "cmake",
            .args = {"--build", "build"},
        },
        [](cppx::process::ProcessEvent const& event) {
            if (event.kind == cppx::process::ProcessEventKind::stdout_chunk)
                std::print("{}", event.text);
        });

    if (!result)
        return 1;
    return result->exit_code;
}
```

`run`, `capture`, and `spawn` keep their existing behavior. `drain` and
`stream` are opt-in conveniences for tools that want live output events
and a final collected summary for build/test progress.

### CLI and shell core

```cpp
import cppx.cli;
import cppx.shell.system;
import std;

int main() {
    auto root = cppx::cli::CommandSpec{
        .name = "tool",
        .subcommands = {{
            .name = "exec",
            .options = {{
                .name = "json",
                .arity = cppx::cli::OptionArity::none,
            }},
        }},
    };
    auto args = std::vector<std::string_view>{"exec", "--json"};
    auto invocation = cppx::cli::parse(root, args);

    auto shell = cppx::shell::system::run_foreground("printf ok");
    if (shell)
        std::print("{}", shell->output);
}
```

`cppx.cli` and `cppx.cli.config` stay pure. `cppx.shell.system`,
`cppx.process.system`, `cppx.terminal.system`, and `cppx.pty.system`
are the real OS boundary. The `examples/agent_cli` package shows how
these pieces compose into a fake-agent reference shell with `exec`,
slash commands, foreground shell commands, and background jobs.

### HTTP client

```cpp
import cppx.http.system;
import std;

int main() {
    auto resp = cppx::http::system::get("https://api.github.com/zen");
    if (!resp) {
        std::println(std::cerr, "error: {}",
                     cppx::http::to_string(resp.error()));
        return 1;
    }

    std::println("status={} body={}",
                 resp->stat.code,
                 resp->body_string());
}
```

`cppx::http::system::get`, `download`, and `system::client` are the
preferred first-party HTTP entrypoints. Platform transport details stay
behind that facade, including WinHTTP-backed requests on Windows.

### Deterministic HTTP system tests

```cpp
import cppx.http;
import cppx.http.system.test;
import std;

int main() {
    auto http = cppx::http::system::test::test_client{};
    http.next_get = cppx::http::response{
        .stat = {200},
        .hdrs = {},
        .body = cppx::http::as_bytes("ok"),
    };
}
```

Use `import cppx.http.system.test;` when you want deterministic test
doubles for code that depends on the first-party sync HTTP facade.
Always-on `test-http_system` coverage stays machine-local, while public
internet checks live in the opt-in smoke run gated by
`CPPX_RUN_HTTP_SYSTEM_SMOKE=1`.

### Async HTTP client

```cpp
import cppx.async.system;
import cppx.http.async.system;
import std;

cppx::async::task<int> fetch_demo() {
    auto resp = co_await cppx::http::async::system::get(
        "http://example.com/health");
    co_return resp && resp->stat.ok() ? 0 : 1;
}
```

`cppx.http.async` is the generic async client surface.
`cppx.http.async.system` supports first-party HTTP and HTTPS across
macOS, Linux, and Windows.

### Download with backend fallback

```cpp
import cppx.http.transfer;
import cppx.http.transfer.system;
import std;

int main() {
    auto result = cppx::http::transfer::system::download_file(
        "https://example.com/tool.tar.gz",
        "tool.tar.gz",
        {.backend = cppx::http::transfer::TransferBackend::Auto});
    if (!result) {
        std::println(std::cerr, "error: {}", result.error().message);
        return 1;
    }
}
```

### Unicode boundary helpers

```cpp
import cppx.unicode;
import std;

int main() {
    auto caret = cppx::unicode::utf16_offset_to_utf8(
        "A" "\xF0\x9F\x99\x82" "B", 2);
    auto marked = cppx::unicode::utf16_range_to_utf8(
        "A" "\xEC\xB0\xAC" "\xF0\x9F\x99\x82" "B", 1, 3);

    std::println("caret={} marked=[{}, {})",
                 caret,
                 marked.start,
                 marked.end);
}
```

Use `import cppx.unicode;` explicitly when platform caret or IME APIs
report UTF-16 units while your application buffer is stored as UTF-8.

### URL opening

```cpp
import cppx.os;
import cppx.os.system;
import std;

int main() {
    auto opened = cppx::os::system::open_url("https://example.com");
    if (!opened) {
        std::println(std::cerr, "error: {}",
                     cppx::os::to_string(opened.error()));
        return 1;
    }
}
```

### HTTP server

```cpp
import cppx.http;
import cppx.http.server;
import cppx.http.system;
import std;

int main() {
    using namespace cppx::http;

    server<system::listener, system::stream> srv;
    srv.serve_static("/", "./public");
    srv.route(method::GET, "/api/health",
              [](request const&) -> response {
                  return {
                      .stat = {200},
                      .hdrs = {},
                      .body = as_bytes(R"({"status":"ok"})")
                  };
              });

    srv.run("0.0.0.0", 3000);
}
```

## Build

`cppx` is authored as a C++23 modules project and the generated CMake
configuration requires CMake 3.30+.

```sh
mise install
intron install
intron exec -- exon test
```

If you need a persistent developer shell for repeated manual commands,
`intron env` is still available as an advanced workflow:

```sh
eval "$(intron env)"
exon test
```

On Windows PowerShell, run `Invoke-Expression ((intron env) -join "`n")`
instead of `eval "$(intron env)"`. `intron install` reads this repo's
`.intron.toml`, so the same flow provisions MSVC on Windows and LLVM on
macOS/Linux.

## Using `cppx`

### exon

```toml
[dependencies]
"github.com/misut/cppx" = "1.9.0"
```

### CMake

```cmake
include(FetchContent)
FetchContent_Declare(cppx
    GIT_REPOSITORY https://github.com/misut/cppx.git
    GIT_TAG v1.9.0
    GIT_SHALLOW ON
)
FetchContent_MakeAvailable(cppx)
target_link_libraries(your_target PRIVATE cppx)
```

## Notes and limits

- `cppx.reflect` supports aggregate types with up to 64 direct fields.
- Field-name extraction currently targets Clang and MSVC.
- Nested aggregates may need an explicit descriptor in higher-level libraries that build on reflection.
- `import cppx;` is intentionally small. Filesystem, process, CLI, shell, terminal, PTY, archive, checksum, HTTP, async, and sync modules remain opt-in imports.
- `cppx.http.system.test` and `cppx.async.system.test` are opt-in test seams. They are not re-exported from `import cppx;`.
- `cppx.sync::work_queue<T>` and `cppx.sync::coalescing_queue<Key, T>` drain already-queued work after `close()` and only return `std::nullopt` once the queue is both closed and empty.
- `cppx.sync::coalescing_queue<Key, T>` suppresses duplicate keys only while an item is still queued. The key is released as soon as the item is popped, not when downstream processing finishes.
- `cppx.sync::background_worker` is intentionally a thin single-thread lifecycle wrapper. It is not a scheduler, coroutine bridge, thread pool, or generic task-executor layer.
- `cppx.env::shell_quote` is only a whitespace-safe path helper for legacy command strings. Use `cppx.shell::quote_*` for deliberate shell quoting.
- System modules (`cppx.env.system`, `cppx.fs.system`, `cppx.os.system`, `cppx.process.system`, `cppx.shell.system`, `cppx.pty.system`, `cppx.terminal.system`, `cppx.archive.system`, `cppx.checksum.system`, `cppx.async.system`, `cppx.http.system`, `cppx.http.async.system`, `cppx.http.transfer.system`) are the boundary where real OS/network effects occur.

## Tested behavior

Current tests cover:

- reflection field count, field access, and field names
- platform detection and wildcard matching
- pure env helpers and system-backed env lookup
- filesystem writes, process execution/stream events, CLI parsing/config overlay, shell command helpers, PTY smoke coverage, terminal formatting/input/capability helpers, archive extraction, and checksum helpers
- resource classification, unified resource reads, and Unicode boundary/UTF-16 conversion helpers
- synchronization queues, duplicate suppression, background worker exception capture, and orderly queue/worker shutdown
- shared sync/async network transport concepts and null helpers
- coroutine tasks, generators, scopes, and deterministic virtual-time testing
- HTTP URL parsing, headers, serialization, sync and async client behavior, server routing, transfer fallback policy, deterministic sync/async system networking paths, and opt-in external-network smoke coverage
- OS URL-opening error paths

## Repository

- Remote: `git@github.com:misut/cppx.git`
- Default branch: `main`
- Commits follow [Conventional Commits](https://www.conventionalcommits.org/)
