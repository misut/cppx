# cppx

C++23 extension library for projects that want a small, composable
standard-library-first foundation. `cppx` provides aggregate
reflection, platform detection, environment helpers, coroutine
primitives, test utilities, and HTTP building blocks. MIT License.

## What it is for

`cppx` is meant to solve the low-level glue once, then reuse it across
projects:

- pure-by-default reflection and environment helpers
- coroutine primitives with deterministic test support
- small test utilities for standalone executables
- HTTP types, parser/serializer, client/server helpers, and system adapters

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
| `cppx.resource` | Pure resource classification and path-resolution helpers for filesystem paths and URLs. |
| `cppx.unicode` | Pure UTF-8/UTF-16/wide-string conversion helpers for platform boundaries. |
| `cppx.os` | OS-facing capability declarations such as `open_error`. |
| `cppx.os.system` | System-backed OS helpers such as `open_url`. |
| `cppx.async` | Coroutine primitives: `task<T>`, `generator<T>`, `executor_engine`, `run`, `async_scope`, `when_all`. |
| `cppx.async.system` | System event-loop executor, timer awaitables, and networking helpers for async I/O. |
| `cppx.async.test` | Deterministic coroutine test executor with virtual time. |
| `cppx.async.system.test` | Scripted in-memory async listener/stream fakes for deterministic system-layer tests. |
| `cppx.test` | Minimal test helpers for standalone executables. |
| `cppx.http` | Core HTTP types, serializer, parser, concepts, and helpers. |
| `cppx.http.client` | Generic HTTP client over pluggable stream/TLS backends. |
| `cppx.http.server` | Generic HTTP server, routing, static file serving, MIME helpers. |
| `cppx.http.system` | Platform-backed sockets/TLS plus convenience helpers like `get` and `download`. |

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
eval "$(intron env)"
exon test
```

## Using `cppx`

### exon

```toml
[dependencies]
"github.com/misut/cppx" = "1.1.0"
```

### CMake

```cmake
include(FetchContent)
FetchContent_Declare(cppx
    GIT_REPOSITORY https://github.com/misut/cppx.git
    GIT_TAG v1.1.0
    GIT_SHALLOW ON
)
FetchContent_MakeAvailable(cppx)
target_link_libraries(your_target PRIVATE cppx)
```

## Notes and limits

- `cppx.reflect` supports aggregate types with up to 24 direct fields.
- Field-name extraction currently targets Clang and MSVC.
- Nested aggregates may need an explicit descriptor in higher-level libraries that build on reflection.
- `import cppx;` is intentionally small. HTTP and async modules remain opt-in imports.
- System modules (`cppx.env.system`, `cppx.async.system`, `cppx.http.system`) are the boundary where real OS/network effects occur.

## Tested behavior

Current tests cover:

- reflection field count, field access, and field names
- platform detection and wildcard matching
- pure env helpers and system-backed env lookup
- resource classification and Unicode boundary conversions
- coroutine tasks, generators, scopes, and deterministic virtual-time testing
- HTTP URL parsing, headers, serialization, incremental parsing, client behavior, server routing, static serving, and system networking paths
- OS URL-opening error paths

## Repository

- Remote: `git@github.com:misut/cppx.git`
- Default branch: `main`
- Commits follow [Conventional Commits](https://www.conventionalcommits.org/)
