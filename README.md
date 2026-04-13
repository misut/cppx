# cppx

C++23 extension library. Portable reflection, platform detection,
environment shims, and HTTP networking — so each compiler/OS
wrinkle is solved once instead of per project. MIT License.

## Design principles

cppx is built around two ideas, applied uniformly across every
module:

- **Pure by default.** Every function is a value-returning
  expression. No raw `new`/`delete`, no statics that outlive the
  call, no thrown exceptions for control flow. `cppx.reflect` and
  `cppx.platform` are fully `consteval` / `constexpr`; `cppx.env`
  and `cppx.http` are templated over capability concepts so they
  have no observable side effect.
- **Side effects live at the edge.** When real OS state must be
  touched (sockets, TLS, `std::getenv`), it is quarantined into a
  `*.system` submodule. Importing the impure submodule
  (`import cppx.http.system;`) is the audit signal — grep for it
  to find every translation unit that touches real state.

Failures use monadic return types: `std::optional<T>` for "absence
is normal" and `std::expected<T, E>` for "this can fail in
distinguishable ways". Callers chain with `and_then` / `transform`
/ `or_else` instead of try/catch.

## Modules

| Module | Purity | Contents |
|---|---|---|
| `cppx.reflect` | pure | Minimal aggregate reflection — `Reflectable` concept, `tuple_size_v`, `get<I>`, `name_of<T, I>`. Up to 24 direct fields. |
| `cppx.platform` | pure | Compile-time host detection — `OS`, `Arch`, `Platform`, `host()`. |
| `cppx.env` | pure | `PATH_SEPARATOR`, `EXE_SUFFIX`, `shell_quote`, plus `env_source` / `fs_source` capability concepts and pure `get` / `home_dir` / `find_in_path` templates. |
| `cppx.env.system` | impure | `system_env` / `system_fs` capabilities, convenience forwarders. |
| `cppx.http` | pure | HTTP types (`method`, `status`, `headers`, `url`, `request`, `response`), HTTP/1.1 incremental parser/serializer, engine concepts (`stream_engine`, `listener_engine`, `tls_provider`), test doubles. |
| `cppx.http.client` | pure | `client<RawStream, Tls>` — `get`, `head`, `post`, `request`. Transparent http/https via URL scheme. |
| `cppx.http.server` | pure | `server<Listener, Stream>` — `route`, `serve_static`, `run`. MIME detection, path matching. |
| `cppx.http.system` | impure | Platform socket engines (POSIX/WinSock), platform TLS (SecureTransport/SChannel/OpenSSL), convenience forwarders (`get`, `download`, `serve_static`). |
| `cppx` | — | Umbrella. `import cppx;` re-exports reflect, platform, env, env.system. HTTP modules are opt-in. |

## Quick start — HTTP client

```cpp
import cppx.http.system;
import std;

int main() {
    // HTTPS GET (platform TLS automatically selected)
    auto resp = cppx::http::system::get("https://api.github.com/zen");
    if (!resp) {
        std::println(std::cerr, "error: {}",
                     cppx::http::to_string(resp.error()));
        return 1;
    }
    std::println("status: {} body: {}",
                 resp->stat.code, resp->body_string());

    // Download to file
    auto dl = cppx::http::system::download(
        "https://example.com/archive.tar.gz", "/tmp/archive.tar.gz");
    if (!dl) return 1;
}
```

## Quick start — HTTP server

```cpp
import cppx.http;
import cppx.http.server;
import cppx.http.system;
import std;

int main() {
    using namespace cppx::http;

    server<system::listener, system::stream> srv;

    // Static file serving (replaces python3 -m http.server)
    srv.serve_static("/", "./public");

    // API routes
    srv.route(method::GET, "/api/health",
              [](request const&) -> response {
        return {.stat = {200}, .hdrs = {},
                .body = as_bytes(R"({"status":"ok"})")};
    });

    std::println("listening on http://localhost:3000");
    srv.run("0.0.0.0", 3000);
}
```

## Quick start — pure testing with fakes

```cpp
import cppx.http;
import cppx.http.client;

struct fake_stream {
    inline static std::vector<std::byte> next_response;
    // satisfies stream_engine — returns canned bytes on recv
    static auto connect(std::string_view, std::uint16_t)
        -> std::expected<fake_stream, cppx::http::net_error>;
    auto send(std::span<std::byte const>) const -> ...;
    auto recv(std::span<std::byte>) const -> ...;
    void close() const;
};

struct fake_tls { /* no-op wrap, satisfies tls_provider */ };

// Pure test — no sockets, no TLS, no network
auto c = cppx::http::client<fake_stream, fake_tls>{};
auto resp = c.get("http://example.com/api");
```

## Supported platforms

| Target | Status |
|---|---|
| macOS (aarch64, x86_64) | Full (native + wasm32-wasi for non-HTTP modules) |
| Linux (x86_64, aarch64) | Full |
| Windows (x86_64 MSVC) | Full (SChannel body parsing has a known issue — handshake works) |
| wasm32-wasi | Reflection, platform, env only (WASI sockets not ready) |

## Requirements

- C++23 compiler with `import std;` support — clang ≥22 or MSVC
  ≥17.14.
- Build system: [exon](https://github.com/misut/exon) ≥0.21.3.
- Toolchain manager: [intron](https://github.com/misut/intron) or
  any environment that provides `clang++`, `cmake`, and `ninja`.
- Linux: `libssl-dev` for HTTPS support.

## Build

```sh
mise install
intron install
eval "$(intron env)"
exon test
```

## Using cppx in your project

```toml
[dependencies]
"github.com/misut/cppx" = "1.0.0"
```

## TLS backends

| Platform | Backend | TLS version | Notes |
|---|---|---|---|
| macOS | Security.framework (SecureTransport) | TLS 1.2 | Deprecated but functional. Network.framework planned for v2. |
| Linux | System OpenSSL | TLS 1.2/1.3 | Requires `-lssl -lcrypto`. |
| Windows | SChannel (SSPI) | TLS 1.2/1.3 | Body parsing fix pending. |

The `tls_provider` concept allows swapping in a custom TLS
implementation without changing client code.

## Known limitations

- **Reflection**: up to 24 direct fields per aggregate; nested
  aggregates may need manual descriptors due to brace elision.
- **Windows MSVC**: reflection `external_storage<T>` trips MSVC's
  constexpr evaluator on `std::string` members (C2131).
- **Windows SChannel**: HTTPS handshake works but response body
  parsing has a buffering issue. Fix planned for a follow-up.
- **wasm32-wasi**: HTTP modules are not available (WASI sockets
  are pre-standardization).
- **HTTP server**: thread-per-connection model. Suitable for
  development servers; not for production load.

## Repository

- Remote: `git@github.com:misut/cppx.git`
- Default branch: `main`
- Commits follow [Conventional Commits](https://www.conventionalcommits.org/).
