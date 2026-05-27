# Apple App (iOS, tvOS, watchOS)

IonClaw includes native SwiftUI apps for **iOS, tvOS, and watchOS**. Each target embeds the full C++ engine via the `ionclaw.xcframework` and runs the server locally on the device — no companion app or remote backend required.

## Requirements

- Xcode 15+ (with the iOS, tvOS, and watchOS SDKs)
- [XcodeGen](https://github.com/yonaskolb/XcodeGen) — `brew install xcodegen`
- The XCFramework, built by the Makefile (see below)

## Project Structure

```
apps/apple/
  project.yml          XcodeGen spec (defines the 3 app targets)
  Shared/
    Sources/           shared swift: theme, server controller, c bridge, networking, ui
    CIonClaw/          clang module map exposing the native C ABI to swift
    Resources/         shared assets (logo)
  iOS/                 ios target: server control + webview panel
  tvOS/                tvos target: server control + voice interaction
  watchOS/             watchos target: server control + voice interaction
```

The C ABI is reached from Swift through `Shared/CIonClaw/module.modulemap`, which points at the canonical header `main/lib/include/ionclaw/ionclaw.h`. Symbols are resolved at link time from the embedded `ionclaw.xcframework`.

## Build and Run

1. Build the XCFramework and generate the Xcode project:

```bash
make prepare-apple
```

This builds `build/xcframework/ionclaw.xcframework` (iOS + tvOS + watchOS, device + simulator) if it is not already present, then runs XcodeGen.

2. Open the generated project and pick a target (`IonClaw-iOS`, `IonClaw-tvOS`, or `IonClaw-watchOS`):

```bash
open apps/apple/IonClaw.xcodeproj
```

The `.xcodeproj` is generated from `project.yml` and is not checked in. After adding or removing source files, regenerate it with `make gen-apple`.

## Targets

| Target | Highlights |
|---|---|
| **iOS** | Start/stop the server, edit host/port, view the LAN addresses, and open the web panel in an embedded `WKWebView`. |
| **tvOS** | Two-column dashboard: server controls on the left, a **QR code** plus network URLs on the right. Voice screen for dictating a message to the agent. |
| **watchOS** | Scrollable list with status, host/port, start/stop, a **QR code**, and the network URLs. Voice screen using the watch dictation input. |

### Voice interaction (tvOS and watchOS)

The voice screen captures a message through the platform's native dictation, then sends it to the local server via `POST /api/chat`. On watchOS this uses `TextFieldLink` (the system dictation button); on tvOS the field opens the on-screen keyboard with dictation. There is no public API to capture the Siri Remote microphone directly, so the field is focused on appear to minimize navigation.

## Signing

Automatic signing is configured in `project.yml` via `DEVELOPMENT_TEAM`. Set it to your team identifier (or change it in Xcode under Signing & Capabilities). The simulator builds run without a team.

## Notes

- On **tvOS**, the project is stored under `Caches` (the only writable location on tvOS); the system may purge it under storage pressure, in which case the project is re-initialized on the next start.
- App icons: iOS and watchOS use a single 1024×1024 master; tvOS ships layered Brand Assets (App Icon + Top Shelf).
- The XCFramework is reused by the Flutter iOS plugin as well — see [Flutter](flutter.md).
