# Flutter App

IonClaw includes a Flutter desktop app for macOS, Linux, and Windows. It embeds the full C++ engine via FFI and runs everything locally. iOS and Android are served by the dedicated native apps (`apps/apple`, `apps/android`), so the Flutter target is desktop only.

## Requirements

- [Flutter SDK](https://flutter.dev/docs/get-started/install) (stable channel) with desktop support enabled
- For macOS: Xcode 15+

## Project Structure

```
apps/flutter/
  plugin/       C++ FFI plugin (desktop platform bindings: linux, macos, windows)
  runner/       Flutter app (UI, screens, routing)
```

## Setup

Install Flutter dependencies:

```bash
make flutter-deps
```

## Run (Debug)

Each command builds the native library if not already present:

```bash
make run-flutter-macos       # macOS
make run-flutter             # device picker (desktop)
make run-flutter-release     # release mode
```

On Linux and Windows, run the app directly with `flutter run -d linux` / `flutter run -d windows` from `apps/flutter/runner` after building the shared library for that platform.

## Release Builds

### macOS

```bash
make release-macos
```

Output: `apps/flutter/runner/build/macos/Build/Products/Release/`

Linux and Windows release builds use `flutter build linux` / `flutter build windows` against the shared library built for the host.

## Native Library Linking

The Flutter plugin loads the C++ shared library via FFI. The Makefile builds and links it:

| Command | Description |
|---|---|
| `make link-flutter-macos` | Build macOS `.dylib` and symlink to plugin |
| `make link-flutter-web` | Build web client and symlink to plugin for bundling |

`prepare-flutter-macos` skips the build if the library already exists. Use `clean-lib` followed by `link-flutter-macos` to force a rebuild.

## Clean

```bash
make clean-lib       # Remove macOS shared library build
```
