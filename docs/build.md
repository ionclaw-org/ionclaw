# Build System

IonClaw uses a Makefile to orchestrate all build targets. Run `make help` for a quick reference.

## Prerequisites

- **CMake** >= 3.25 (required by the openssl-cmake dependency)
- **C++20** compiler (Clang 14+, GCC 11+, or MSVC 2022)
- **Perl** (OpenSSL is built from source with its Perl `Configure`; plus **NASM** on Windows)
- **Node.js** >= 20.19 (for web client)
- **Xcode** command-line tools (for iOS/macOS targets)
- **Android NDK** (for Android targets — set `ANDROID_NDK_ROOT` or pass `ANDROID_NDK=...`)
- **Flutter** SDK (for Flutter targets)
- **XcodeGen** (for the native Apple app — `brew install xcodegen`)
- **Docker** (for container targets)

## Dependencies

All C++ dependencies are fetched and built from source by CPM during configure: Poco, OpenSSL, spdlog, nlohmann/json, yaml-cpp, jwt-cpp, hnswlib, HowardHinnant/date, stb, and (optionally) llama.cpp.

Cron scheduling is DST-correct against the IANA time zone database. On Linux and macOS this is provided by the [HowardHinnant/date](https://github.com/HowardHinnant/date) library reading the system zone database (`/usr/share/zoneinfo`), so that path must be present. On Windows the zone rules come from the OS directly, so no bundled database is needed.

## Build Commands

### `make build`

Builds the **server executable** in release mode. Output: `build/release/bin/ionclaw-server`.

Uses CMake with `-DCMAKE_BUILD_TYPE=Release`. This is the main target for development and production.

Local inference via llama.cpp is built in by default on supported platforms (`-DIONCLAW_LLAMA_CPP=OFF` to disable). See [Local Inference](llama.md).

### `make build-debug`

Builds the **server executable** in debug mode with debug symbols. Output: `build/debug/bin/ionclaw-server`.

Use this for debugging with `gdb`/`lldb` or when running with `--debug` for verbose logging.

### `make build-web`

Builds the **Vue.js web client** using Vite. Source: `apps/web/`. Output: `main/resources/web/`.

The web client is automatically served by the server at `/app/`. You must run `make setup-web` first to install npm dependencies.

### `make build-lib`

Builds the **shared library** (`libionclaw.dylib` on macOS, `libionclaw.so` on Linux) for use via FFI (e.g., Flutter plugin). Output: `build/shared/lib/`.

Uses CMake with `-DIONCLAW_BUILD_SHARED=ON`. This produces a dynamic library with the C API (`ionclaw_server_start`, `ionclaw_server_stop`, `ionclaw_free`).

### `make build-xcframework`

Builds a multi-platform **XCFramework** with **iOS, tvOS, and watchOS** slices (device + simulator for each). Output: `build/xcframework/ionclaw.xcframework`.

This target builds every architecture per platform (iOS arm64 + simulator arm64/x86_64, tvOS arm64 + simulator arm64/x86_64, watchOS arm64_32 + simulator arm64/x86_64) before combining them. Used by the Flutter iOS plugin and the native [Apple app](apple.md).

### `make build-all`

Builds **everything**: web client, server executable, and shared library. Equivalent to running `build-web`, `build`, and `build-lib` in sequence.

### `make build-apple`

Builds all **Apple platform targets**: macOS shared library, iOS XCFramework, and links both to the Flutter plugin. Use this when preparing a full Flutter release.

### `make prepare-apple`

Builds the XCFramework (if not already present) and generates the native Apple Xcode project via XcodeGen. Use this before opening the [Apple app](apple.md) in Xcode.

### `make gen-apple`

Generates the Apple Xcode project (`apps/apple/IonClaw.xcodeproj`) from `apps/apple/project.yml` via [XcodeGen](https://github.com/yonaskolb/XcodeGen). Run after adding or removing source files.

### `make build-android`

Builds `libionclaw.so` for the **Android ABIs** (arm64-v8a, x86_64, x86) using the Android NDK CMake toolchain. Output: `build/android-{ABI}/lib/`.

Requires `ANDROID_NDK_ROOT` environment variable or `ANDROID_NDK=...` argument. Targets API level 21. Uses `c++_static` STL to avoid shipping libc++_shared.so separately.

Individual ABI targets are also available: `build-android-arm64`, `build-android-x86_64`, `build-android-x86`.

### `make build-android-aar`

Assembles the standalone **`ionclaw.aar`** consumed by the native [Android app](android.md) (and distributable on its own). Builds the native `.so` for the 64-bit ABIs (`arm64-v8a`, `x86_64`) if not already present and bundles the NDK `libomp.so` (`link-android`), then runs the standalone `apps/android/library` Gradle build. Output: `build/android-aar/ionclaw.aar`. The aar is 64-bit only because llama.cpp does not build on 32-bit arm.

### `make build-docker`

Builds a **Docker image** tagged `ionclaw`. Requires a `Dockerfile` in the project root.

## Run Commands

### `make run`

Builds (if needed) and **runs the server** in release mode. Pass extra arguments with `ARGS`:

```bash
make run ARGS="--project /path/to/project --port 9090"
```

Server CLI options:
- `--project <path>` — Project directory (default: current directory)
- `--host <host>` — Bind address (default: from config.yml)
- `--port <port>` — Listen port (default: from config.yml)
- `--debug` — Enable debug logging

### `make run-debug`

Builds and runs the server in **debug mode**. Automatically passes `--debug` for verbose logging.

### `make run-web`

Starts the **Vite dev server** with hot module replacement (HMR) for web client development. The dev server proxies API requests to the running IonClaw server.

### `make run-docker`

Builds the Docker image and **runs a container** exposing port 8080.

### `make run-flutter-macos`

Runs the **Flutter desktop app on macOS**. Only builds the native library and web client if they don't already exist (`prepare-flutter-macos`).

### `make run-android`

Builds (if needed) the aar via `build-android-aar`, installs the native [Android app](android.md) with `./gradlew installDebug` from `apps/android/app`, and launches it via `adb`.

## Release Commands

### `make release-macos`

Builds the macOS shared library, bundles the web client, and creates a **release macOS app**.

Output: `apps/flutter/runner/build/macos/Build/Products/Release/`

## Setup Commands

### `make setup-web`

Installs **npm dependencies** for the web client (`apps/web/`). Run this once before `build-web` or `run-web`.

### `make prepare-flutter-macos`

Builds the macOS shared library and web client **only if they don't already exist**. Skips if the outputs are present. Used by `run-flutter-macos` to avoid rebuilding every time.

To force a rebuild: `make clean-lib link-flutter-macos` and `make clean-web link-flutter-web`.

### `make link-flutter-macos`

Creates a **symlink** from the built macOS `libionclaw.dylib` to the Flutter plugin's `macos/` directory. Automatically builds the shared library first.

### `make link-flutter-web`

Creates a **symlink** from the built web client to the Flutter plugin's `macos/` directory for resource bundling. Automatically builds the web client first.

### `make link-android`

Builds the Android native libraries for the **64-bit ABIs** (`arm64-v8a`, `x86_64`) and **copies** them — plus the NDK `libomp.so` the engine links against — into the standalone aar project at `apps/android/library/src/main/jniLibs/{ABI}/`. 32-bit arm is skipped because llama.cpp does not build there.

### `make prepare-android`

Builds the Android `.so` files for the aar library **only if they don't already exist**. Used by `build-android-aar`.

To force a rebuild: `make clean-android link-android`.

## Clean Commands

| Command | What it removes |
|---------|----------------|
| `make clean` | All build directories |
| `make clean-build` | Server builds (`build/release/`, `build/debug/`) |
| `make clean-web` | Web client output (`main/resources/web/`) |
| `make clean-lib` | Shared library build (`build/shared/`) |
| `make clean-ios` | iOS builds and XCFramework |
| `make clean-android` | Android builds, jniLibs, and the aar |

## Code Commands

### `make format`

Formats **all C/C++/ObjC source files** (`.cpp`, `.hpp`, `.c`, `.h`, `.m`, `.mm`) in the `main/` directory using `clang-format`. Uses the `.clang-format` config file at the project root.

## Build Directories

| Directory | Contents |
|-----------|----------|
| `build/release/` | Server release build |
| `build/debug/` | Server debug build |
| `build/shared/` | Shared library (macOS/Linux) |
| `build/ios-arm64/` | iOS device library |
| `build/ios-sim-arm64/` | iOS simulator arm64 library |
| `build/ios-sim-x86_64/` | iOS simulator x86_64 library |
| `build/xcframework/` | Combined iOS XCFramework |
| `build/android-arm64-v8a/` | Android arm64 library |
| `build/android-x86_64/` | Android x86_64 library (emulator) |
| `build/android-x86/` | Android x86 library (emulator) |
| `build/android-aar/` | Standalone `ionclaw.aar` (native Android app) |

## Common Workflows

### Development (server + web)

```bash
make setup-web          # once: install npm packages
make build-web          # build web client
make run-debug          # start server with debug logging
# in another terminal:
make run-web            # start Vite dev server with HMR
```

### Production build

```bash
make build-web          # build web client
make build              # build server (release)
./build/release/bin/ionclaw-server start --project /path/to/project
```

### Flutter (macOS)

```bash
make run-flutter-macos  # builds lib, links, and runs the Flutter desktop app
```

### Native app (Android)

```bash
# set NDK path (or export ANDROID_NDK_ROOT)
make build-android-aar ANDROID_NDK=/path/to/ndk   # standalone, distributable aar
make run-android ANDROID_NDK=/path/to/ndk         # builds the aar, installs, and launches
```

### Release builds

```bash
make release-macos      # builds the Flutter macOS app
```

### Full rebuild

```bash
make clean              # remove all build artifacts
make build-all          # rebuild everything
```

## CI/CD (GitHub Actions)

### Build Workflow (`build-all.yml`)

Runs on push to `main` and pull requests (documentation-only changes are skipped). It is an orchestrator that calls one reusable per-platform workflow per job, so each platform can also be dispatched on its own:

| Job (`workflow_call`) | Runner | What it builds |
|-----|--------|---------------|
| `build-macos.yml` (arm64) | macos-15 | Server executable |
| `build-macos.yml` (x86_64) | macos-15 | Server executable (cross-compiled) |
| `build-linux.yml` (x86_64) | ubuntu-24.04 | Server executable |
| `build-windows.yml` (x86_64) | windows-latest | Server executable |
| `build-ios.yml` (XCFramework) | macos-15 | arm64 device + arm64/x86_64 simulator |
| `build-android.yml` | ubuntu-24.04 | Android shared library |

All desktop jobs build the web client (Node.js 22) before CMake. CPM packages are cached per OS. Every desktop job runs the outbound-HTTPS regression test. The `workflow_dispatch` `release` input additionally produces the platform installers (MSI/PKG/DEB) and release packages.

### Release Workflow (`release.yml`)

Triggered on tag push (`v*`). Produces downloadable artifacts:

| Artifact | Contents |
|----------|----------|
| `ionclaw-{os}-{arch}.tar.gz` | Server executable |
| `ionclaw-{os}-{arch}-shared.tar.gz` | Shared library + ionclaw.h |
| `ionclaw-ios-xcframework.tar.gz` | XCFramework + ionclaw.h |
| `ionclaw-android.tar.gz` | All 4 ABI .so files in jniLibs/ layout |

Android builds the ABIs sequentially (arm64-v8a, x86_64, x86) then packages them together.
