# Build System

IonClaw uses a Makefile to orchestrate all build targets. Run `make help` for a quick reference.

## Prerequisites

- **CMake** >= 3.20
- **C++17** compiler (Clang, GCC, or MSVC)
- **Node.js** >= 18 (for web client)
- **Xcode** command-line tools (for iOS/macOS targets)
- **Android NDK** (for Android targets — set `ANDROID_NDK_ROOT` or pass `ANDROID_NDK=...`)
- **Flutter** SDK (for Flutter targets)
- **Docker** (for container targets)

## Build Commands

### `make build`

Builds the **server executable** in release mode. Output: `build/release/bin/ionclaw-server`.

Uses CMake with `-DCMAKE_BUILD_TYPE=Release`. This is the main target for development and production.

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

Builds an **iOS XCFramework** containing arm64 device and simulator (arm64 + x86_64) libraries. Output: `build/xcframework/ionclaw.xcframework`.

This target automatically builds all three iOS architectures before combining them. Used by the Flutter iOS plugin.

### `make build-all`

Builds **everything**: web client, server executable, and shared library. Equivalent to running `build-web`, `build`, and `build-lib` in sequence.

### `make build-apple`

Builds all **Apple platform targets**: macOS shared library, iOS XCFramework, and links both to the Flutter plugin. Use this when preparing a full Flutter release.

### `make build-android`

Builds `libionclaw.so` for **all Android ABIs** (arm64-v8a, armeabi-v7a, x86_64, x86) using the Android NDK CMake toolchain. Output: `build/android-{ABI}/lib/`.

Requires `ANDROID_NDK_ROOT` environment variable or `ANDROID_NDK=...` argument. Targets API level 21. Uses `c++_static` STL to avoid shipping libc++_shared.so separately.

Individual ABI targets are also available: `build-android-arm64`, `build-android-armv7`, `build-android-x86_64`, `build-android-x86`.

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

Runs the **Flutter app on macOS**. Only builds the native library and web client if they don't already exist (`prepare-flutter-macos`).

### `make run-flutter-ios`

Runs the **Flutter app on iOS**. Only builds the XCFramework if it doesn't already exist (`prepare-flutter-ios`).

### `make run-flutter-android`

Runs the **Flutter app on Android**. Only builds the native libraries and web client if they don't already exist (`prepare-flutter-android`).

## Release Commands

### `make release-android`

Builds the Android native libraries, bundles the web client, and creates a **release appbundle** (`.aab`) for Google Play upload.

Output: `flutter/runner/build/app/outputs/bundle/release/app-release.aab`

### `make release-ios`

Builds the iOS XCFramework, bundles the web client, and creates a **release archive** (`.ipa`) for App Store upload.

Output: `flutter/runner/build/ios/archive/Runner.xcarchive`

### `make release-macos`

Builds the macOS shared library, bundles the web client, and creates a **release macOS app**.

Output: `flutter/runner/build/macos/Build/Products/Release/`

## Setup Commands

### `make setup-web`

Installs **npm dependencies** for the web client (`apps/web/`). Run this once before `build-web` or `run-web`.

### `make prepare-flutter-macos`

Builds the macOS shared library and web client **only if they don't already exist**. Skips if the outputs are present. Used by `run-flutter-macos` to avoid rebuilding every time.

To force a rebuild: `make clean-lib link-flutter-macos` and `make clean-web link-flutter-web`.

### `make prepare-flutter-ios`

Builds the iOS XCFramework **only if it doesn't already exist**. Used by `run-flutter-ios` to avoid rebuilding the native library on every Flutter run.

To force a rebuild: `make clean-ios link-flutter-ios`.

### `make prepare-flutter-android`

Builds Android native libraries and web client **only if they don't already exist**. Used by `run-flutter-android`.

To force a rebuild: `make clean-android link-flutter-android`.

### `make link-flutter-macos`

Creates a **symlink** from the built macOS `libionclaw.dylib` to the Flutter plugin's `macos/` directory. Automatically builds the shared library first.

### `make link-flutter-web`

Creates a **symlink** from the built web client to the Flutter plugin's `macos/` directory for resource bundling. Automatically builds the web client first.

### `make link-flutter-ios`

Creates a **symlink** from the built iOS `ionclaw.xcframework` to the Flutter plugin's `ios/` directory. Automatically builds the XCFramework first.

### `make link-flutter-android`

Builds Android native libraries and **copies** the `.so` files to `flutter/plugin/android/src/main/jniLibs/{ABI}/`. Automatically builds all ABIs first.

## Clean Commands

| Command | What it removes |
|---------|----------------|
| `make clean` | All build directories |
| `make clean-build` | Server builds (`build/release/`, `build/debug/`) |
| `make clean-web` | Web client output (`main/resources/web/`) |
| `make clean-lib` | Shared library build (`build/shared/`) |
| `make clean-ios` | iOS builds and XCFramework |
| `make clean-android` | Android builds and jniLibs |

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
| `build/android-armeabi-v7a/` | Android armv7 library |
| `build/android-x86_64/` | Android x86_64 library (emulator) |
| `build/android-x86/` | Android x86 library (emulator) |

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
make run-flutter-macos  # builds lib, links, and runs Flutter app
```

### Flutter (iOS)

```bash
make run-flutter-ios    # builds xcframework, links, and runs Flutter app
```

### Flutter (Android)

```bash
# set NDK path (or export ANDROID_NDK_ROOT)
make run-flutter-android ANDROID_NDK=/path/to/ndk
```

### Release builds

```bash
make release-android    # builds .aab for Google Play
make release-ios        # builds .ipa for App Store
make release-macos      # builds macOS app
```

### Full rebuild

```bash
make clean              # remove all build artifacts
make build-all          # rebuild everything
```

## CI/CD (GitHub Actions)

### Build Workflow (`build.yml`)

Runs on push to `main` and pull requests. Builds and validates:

| Job | Runner | What it builds |
|-----|--------|---------------|
| macOS (arm64) | macos-15 | Server executable |
| macOS (x86_64) | macos-15 | Server executable (cross-compiled) |
| Linux (x86_64) | ubuntu-24.04 | Server executable |
| Windows (x86_64) | windows-latest | Server executable |
| iOS (XCFramework) | macos-15 | arm64 device + arm64/x86_64 simulator |
| Android (arm64) | ubuntu-24.04 | arm64-v8a shared library |

All desktop jobs build the web client (Node.js 22) before CMake. CPM packages are cached per OS.

### Release Workflow (`release.yml`)

Triggered on tag push (`v*`). Produces downloadable artifacts:

| Artifact | Contents |
|----------|----------|
| `ionclaw-{os}-{arch}.tar.gz` | Server executable |
| `ionclaw-{os}-{arch}-shared.tar.gz` | Shared library + ionclaw.h |
| `ionclaw-ios-xcframework.tar.gz` | XCFramework + ionclaw.h |
| `ionclaw-android.tar.gz` | All 4 ABI .so files in jniLibs/ layout |

Android builds all 4 ABIs in parallel (arm64-v8a, armeabi-v7a, x86_64, x86) then packages them together.
