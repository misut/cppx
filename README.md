# cppx

C++23 extension library for misut projects. A single place for
portability shims so each compiler/OS wrinkle is solved once instead of
per project. MIT License.

## Modules

| Module | Contents |
|---|---|
| `cppx.reflect` | Minimal aggregate reflection — `Reflectable` concept, `tuple_size_v`, `get<I>`, `name_of<T, I>`. Up to 16 direct fields. |
| `cppx.platform` | Compile-time host detection — `OS`, `Arch`, `Platform`, `host()`. |
| `cppx.env` | Environment & shell shims — `PATH_SEPARATOR`, `EXE_SUFFIX`, `get`, `home_dir`, `find_in_path`, `shell_quote`. |
| `cppx` | Umbrella. `import cppx;` re-exports all of the above. |

Prefer importing submodules directly (`import cppx.reflect;`) when you
only need one; the umbrella is a convenience for projects that want
everything.

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
- Build system: [exon](https://github.com/misut/exon) ≥0.17.0. For
  wasm builds, exon ≥0.19.0.
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

By design, every public function returns by value. No raw `new`/`delete`,
no caller-owned heap pointers, no statics that outlive process exit.
The reflection module is entirely `consteval` / `constexpr` — no heap
usage at all. Tests exercise every public entry point.

A sanitizer CI job (ASan + UBSan + LSan on macOS and Linux) is planned
for a follow-up release once exon supports per-target `[build]`
sections or an env-var override for CXXFLAGS.

## Known limitations

- **Reflection**: up to 16 direct fields per aggregate; aggregates with
  nested aggregate members may overcount due to brace elision (use an
  explicit per-type descriptor in that case).
- **Windows MSVC**: the reflection module's `external_storage<T>` path
  currently trips MSVC's constexpr evaluator on types containing
  `std::string` (C2131). This is a pre-existing limitation inherited
  from `misut/txn/refl` and will be addressed in a follow-up. The
  `cppx.platform` and `cppx.env` modules are unaffected.
- **wasm32-wasi**: requires wasmtime ≥37 invoked with `-W exceptions=y`
  because libc++'s `csetjmp` / `csignal` headers pull in wasm sjlj,
  which lowers to the WebAssembly Exception Handling proposal.
  `exon test --target wasm32-wasi` handles this automatically from
  exon ≥0.19.0.

## Using cppx in your project

Add a git dependency to your `exon.toml`:

```toml
[dependencies]
"github.com/misut/cppx" = "0.1.0"
```

Then in your module:

```cpp
import cppx.reflect;
import cppx.env;
import cppx.platform;

struct Config { std::string host; int port; };

int main() {
    // Reflection
    static_assert(cppx::reflect::name_of<Config, 0>() == "host");

    // Environment
    if (auto home = cppx::env::home_dir())
        std::println("home: {}", home->string());

    // Platform
    std::println("host: {}", cppx::platform::host().to_string());
}
```

## Repository

- Remote: `git@github.com:misut/cppx.git`
- Default branch: `main`
- Commits follow [Conventional Commits](https://www.conventionalcommits.org/).
