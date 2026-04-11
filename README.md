# cppx

C++23 extension library. A single place for portability shims so each
compiler/OS wrinkle is solved once instead of per project. MIT License.

## Design principles

cppx is built around two ideas, applied uniformly across every module:

- **Pure by default.** Every function is a value-returning expression.
  No raw `new`/`delete`, no statics that outlive the call, no thrown
  exceptions for control flow. `cppx.reflect` and `cppx.platform` are
  fully `consteval` / `constexpr`; `cppx.env` is templated over
  capabilities so it has no observable side effect either.
- **Side effects live at the edge.** When real OS state must be read
  (`std::getenv`, `std::filesystem::is_regular_file`), it is
  quarantined into a `*.system` submodule. The pure core takes the
  capability as a `concept`-constrained parameter. Importing the
  impure submodule (`import cppx.env.system;`) is the audit signal —
  grep for it to find every translation unit that touches real state.

Failures use monadic return types: `std::optional<T>` for "absence is
normal" and `std::expected<T, E>` for "this can fail in distinguishable
ways". Callers chain with `and_then` / `transform` / `or_else` instead
of try/catch.

## Modules

| Module | Purity | Contents |
|---|---|---|
| `cppx.reflect` | pure | Minimal aggregate reflection — `Reflectable` concept, `tuple_size_v`, `get<I>`, `name_of<T, I>`. Up to 16 direct fields. |
| `cppx.platform` | pure | Compile-time host detection — `OS`, `Arch`, `Platform`, `host()`. |
| `cppx.env` | pure | `PATH_SEPARATOR`, `EXE_SUFFIX`, `shell_quote`, plus `env_source` / `fs_source` capability concepts and pure `get` / `home_dir` / `find_in_path` templates. |
| `cppx.env.system` | **impure** | `system_env` / `system_fs` capabilities that wrap `std::getenv` and `std::filesystem`, plus convenience forwarders bound to them. The only side-effecting submodule. |
| `cppx` | — | Umbrella. `import cppx;` re-exports all of the above. |

Prefer importing submodules directly (`import cppx.reflect;`) when you
only need one; the umbrella is a convenience.

## Supported platforms

| Target | Status |
|---|---|
| macOS (aarch64, x86_64) | Native + wasm32-wasi |
| Linux (x86_64, aarch64) | Native + wasm32-wasi |
| Windows (x86_64 MSVC) | Native — see [Known limitations](#known-limitations) |
| wasm32-wasi (wasmtime ≥37) | Requires `exon ≥0.19.0` for `import std;` support |

## Requirements

- C++23 compiler with `import std;` support — clang ≥22 or MSVC
  ≥17.14.
- Build system: [exon](https://github.com/misut/exon) ≥0.21.1.
- Toolchain manager: [intron](https://github.com/misut/intron) or any
  environment that provides `clang++`, `cmake`, and `ninja`.

## Build

```sh
mise install
intron install     # reads .intron.toml (optional — uses mise.toml tools otherwise)
eval "$(intron env)"
exon test
```

## Memory safety

Every public function returns by value. No raw `new`/`delete`, no
caller-owned heap pointers, no statics that outlive process exit. The
reflection module is entirely `consteval` / `constexpr` — no heap
usage at all. CI runs ASan + UBSan + LSan on macOS and Linux and
MSVC AddressSanitizer on Windows; tests exercise every public entry
point.

## Known limitations

- **Reflection**: up to 16 direct fields per aggregate; aggregates with
  nested aggregate members may overcount due to brace elision (use an
  explicit per-type descriptor in that case).
- **Windows MSVC**: the reflection module's `external_storage<T>` path
  currently trips MSVC's constexpr evaluator on types containing
  `std::string` (C2131). The `cppx.platform` and `cppx.env` modules
  are unaffected.
- **wasm32-wasi**: requires wasmtime ≥37 invoked with `-W exceptions=y`
  because libc++'s `csetjmp` / `csignal` headers pull in wasm sjlj,
  which lowers to the WebAssembly Exception Handling proposal.
  `exon test --target wasm32-wasi` handles this automatically from
  exon ≥0.19.0.

## Using cppx in your project

Add a git dependency to your `exon.toml`:

```toml
[dependencies]
"github.com/misut/cppx" = "0.3.0"
```

Then in your code:

```cpp
import cppx.reflect;
import cppx.env;
import cppx.env.system;   // only when you need real OS state
import cppx.platform;

struct Config { std::string host; int port; };

int main() {
    // Reflection — pure, consteval.
    static_assert(cppx::reflect::name_of<Config, 0>() == "host");

    // Environment — impure read goes through cppx.env.system.
    if (auto home = cppx::env::system::home_dir())
        std::println("home: {}", home->string());

    // find_in_path distinguishes "PATH unset" from "not found".
    auto cmake = cppx::env::system::find_in_path("cmake");
    if (!cmake) {
        auto msg = (cmake.error() == cppx::env::find_error::no_PATH_set)
            ? "PATH is not set"
            : "cmake not found on PATH";
        std::println(std::cerr, "{}", msg);
    }

    // Platform — pure, compile-time.
    std::println("host: {}", cppx::platform::host().to_string());
}
```

For unit tests that exercise `cppx.env` logic, inject your own fakes
instead of mutating the real environment:

```cpp
import cppx.env;

struct fake_env {
    std::map<std::string, std::string, std::less<>> vars;
    std::optional<std::string> get(std::string_view name) const {
        if (auto it = vars.find(name); it != vars.end() && !it->second.empty())
            return it->second;
        return std::nullopt;
    }
};

auto env = fake_env{{{"HOME", "/tmp/test"}}};
auto home = cppx::env::home_dir(env);   // pure, no setenv
```

`cppx::env::null_env` and `cppx::env::null_fs` are exported for the
common case where you want a capability that always answers "missing".

## Repository

- Remote: `git@github.com:misut/cppx.git`
- Default branch: `main`
- Commits follow [Conventional Commits](https://www.conventionalcommits.org/).
