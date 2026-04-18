# cppx

C++23 extension library for projects that want a small, composable
standard-library-first foundation. `cppx` provides aggregate
reflection, platform detection, environment and filesystem helpers,
process/archive/checksum utilities, coroutine primitives, test
utilities, and HTTP building blocks. MIT License.

## What it is for

`cppx` is meant to solve the low-level glue once, then reuse it across
projects:

- pure-by-default reflection and environment helpers
- small filesystem, process, archive, and checksum utilities at the
  system boundary
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
| `cppx.reflect` | Aggregate reflection: `Reflectable`, `tuple_size_v`, `get<I>`, `name_of<T, I>()`. |
| `cppx.platform` | Compile-time host detection: `OS`, `Arch`, `Platform`, `host()`. |
| `cppx.env` | Pure environment/path helpers such as `get`, `home_dir`, `find_in_path`, `shell_quote`. |
| `cppx.env.system` | System-backed environment/filesystem adapters for `cppx.env`. |
| `cppx.fs` | Filesystem-facing value types such as `TextWrite` and `fs_error`. |
| `cppx.fs.system` | System-backed text read/write helpers such as `read_text`, `write_if_changed`, and `apply_writes`. |
| `cppx.resource` | Pure resource classification and path-resolution helpers for filesystem paths and URLs. |
| `cppx.unicode` | Pure UTF-8 boundary helpers plus UTF-16/UTF-8 offset, range, and conversion utilities for platform boundaries. |
| `cppx.os` | OS-facing capability declarations such as `open_error`. |
| `cppx.os.system` | System-backed OS helpers such as `open_url`. |
| `cppx.process` | Process specs/results and `process_error` types for child-process work. |
| `cppx.process.system` | System-backed `run` and `capture` helpers with timeout/cwd/env override support. |
| `cppx.archive` | Archive extraction specs and error types. |
| `cppx.archive.system` | System-backed archive extraction helpers. |
| `cppx.checksum` | Pure checksum parsing/normalization helpers and error types. |
| `cppx.checksum.system` | System-backed SHA-256 hashing helpers. |
| `cppx.async` | Coroutine primitives: `task<T>`, `generator<T>`, `executor_engine`, `run`, `async_scope`, `when_all`. |
| `cppx.async.system` | System event-loop executor, timer awaitables, and networking helpers for async I/O across POSIX and Windows. |
| `cppx.async.test` | Deterministic coroutine test executor with virtual time. |
| `cppx.async.system.test` | Scripted in-memory async listener/stream fakes for deterministic system-layer tests. |
| `cppx.test` | Minimal test helpers for standalone executables. |
| `cppx.http` | Core HTTP types, serializer, parser, concepts, and helpers. |
| `cppx.http.client` | Generic HTTP client over pluggable stream/TLS backends. |
| `cppx.http.server` | Generic HTTP server, routing, static file serving, MIME helpers. |
| `cppx.http.transfer` | Transfer backend policy, result types, and fallback rules. |
| `cppx.http.system` | Platform-backed sockets/TLS plus convenience helpers like `get` and `download`. |
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
"github.com/misut/cppx" = "1.5.0"
```

### CMake

```cmake
include(FetchContent)
FetchContent_Declare(cppx
    GIT_REPOSITORY https://github.com/misut/cppx.git
    GIT_TAG v1.5.0
    GIT_SHALLOW ON
)
FetchContent_MakeAvailable(cppx)
target_link_libraries(your_target PRIVATE cppx)
```

## Notes and limits

- `cppx.reflect` supports aggregate types with up to 24 direct fields.
- Field-name extraction currently targets Clang and MSVC.
- Nested aggregates may need an explicit descriptor in higher-level libraries that build on reflection.
- `import cppx;` is intentionally small. Filesystem, process, archive, checksum, HTTP, and async modules remain opt-in imports.
- System modules (`cppx.env.system`, `cppx.fs.system`, `cppx.os.system`, `cppx.process.system`, `cppx.archive.system`, `cppx.checksum.system`, `cppx.async.system`, `cppx.http.system`, `cppx.http.transfer.system`) are the boundary where real OS/network effects occur.

## Tested behavior

Current tests cover:

- reflection field count, field access, and field names
- platform detection and wildcard matching
- pure env helpers and system-backed env lookup
- filesystem writes, process execution, archive extraction, and checksum helpers
- resource classification and Unicode boundary/UTF-16 conversion helpers
- coroutine tasks, generators, scopes, and deterministic virtual-time testing
- HTTP URL parsing, headers, serialization, incremental parsing, client behavior, server routing, transfer fallback policy, and system networking paths
- OS URL-opening error paths

## Repository

- Remote: `git@github.com:misut/cppx.git`
- Default branch: `main`
- Commits follow [Conventional Commits](https://www.conventionalcommits.org/)
