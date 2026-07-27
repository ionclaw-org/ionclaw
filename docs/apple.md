# Apple App (iOS, tvOS, watchOS)

IonClaw includes native SwiftUI apps for **iOS, tvOS, and watchOS**. Each target embeds the full C++ engine via the `ionclaw.xcframework` and runs the server locally on the device — no companion app or remote backend required.

## Requirements

- Xcode 15+ (with the iOS, tvOS, and watchOS SDKs)
- [XcodeGen](https://github.com/yonaskolb/XcodeGen) — `brew install xcodegen`
- The XCFramework, built by the Makefile (see below)

## Project Structure

```
apps/apple/
  IonClaw.xcodeproj    committed Xcode project (open directly after building the xcframework)
  project.yml          XcodeGen spec (source of truth; regenerates the project)
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

The `.xcodeproj` is committed, so a fresh clone opens it directly — only the xcframework needs building first. `project.yml` stays the source of truth: after adding or removing source files, regenerate and commit the project with `make gen-apple`. Xcode's DerivedData should point at its default location (Xcode → Settings → Locations → Derived Data → Default), not inside `apps/apple/`.

## Targets

| Target | Highlights |
|---|---|
| **iOS** | Start/stop the server, edit host/port, view the LAN addresses, and open the web panel in an embedded `WKWebView`. |
| **tvOS** | Two-column dashboard: server controls on the left, a **QR code** plus network URLs on the right. Voice screen for dictating a message to the agent. |
| **watchOS** | Scrollable list with status, host/port, start/stop, a **QR code**, and the network URLs. Voice screen using the watch dictation input. |

### Voice interaction (tvOS and watchOS)

The voice screen captures a message through the platform's native dictation, then sends it to the local server via `POST /api/chat`. On watchOS this uses `TextFieldLink` (the system dictation button); on tvOS the field opens the on-screen keyboard with dictation. There is no public API to capture the Siri Remote microphone directly, so the field is focused on appear to minimize navigation.

### Local notifications

The agent can show a local notification through the `invoke_platform` tool: `invoke_platform("local-notification.send", {"title": "...", "message": "..."})`. The native handler lives in `Shared/Sources/Platform/PlatformBridge.swift`, registered once at launch via `ionclaw_set_platform_handler` and responding asynchronously via `ionclaw_platform_respond` (same C ABI the Flutter app uses). It requests notification authorization at launch and schedules the notification with `UserNotifications`.

- **iOS / watchOS**: full support — alert, sound, and badge, shown in the foreground too (via the `UNUserNotificationCenter` delegate).
- **tvOS**: not supported — Apple restricts tvOS to app-icon badges (no alert banners, and `UNNotificationContent.sound` is unavailable), so `local-notification.send` returns an error explaining the limitation.

To add more platform functions, extend the `switch` in `IonClawPlatform.handle(...)`.

## Signing

Automatic signing is configured in `project.yml` via `DEVELOPMENT_TEAM`. Set it to your team identifier (or change it in Xcode under Signing & Capabilities). The simulator builds run without a team.

## Notes

- On **tvOS**, the project is stored under `Caches` (the only writable location on tvOS); the system may purge it under storage pressure, in which case the project is re-initialized on the next start.
- App icons: iOS and watchOS use a single 1024×1024 master; tvOS ships layered Brand Assets (App Icon + Top Shelf).
- The XCFramework is reused by the Flutter iOS plugin as well — see [Flutter](flutter.md).

## Publishing (App Store)

The three targets are ready to archive and submit (iOS `com.ionclaw.app`, tvOS `com.ionclaw.app.tvos`, watchOS `com.ionclaw.app.watchos`, the watch app is standalone via `WKWatchOnly`).

Already in the repo:
- `Shared/Resources/PrivacyInfo.xcprivacy` — declares no tracking and no data collection, plus the file-timestamp and UserDefaults required-reason APIs the engine and `AppConfig` use. Bundled into every target.
- `ITSAppUsesNonExemptEncryption = false` in each `Info.plist` — the app uses only standard TLS, so it skips the export-compliance questionnaire.
- Local-network and microphone usage descriptions.

To publish:
1. `make build-xcframework` builds the iOS, tvOS, and watchOS slices.
2. Open `apps/apple/IonClaw.xcodeproj`, pick the target, select a device destination, and **Product → Archive**.
3. In the Organizer, Validate then Distribute to App Store Connect.
4. Register the App IDs and create the App Store Connect records (one tvOS app, one standalone watch app), fill the App Privacy label, upload screenshots, and submit for review.

Screenshot sizes (App Store Connect, portrait for phone and watch, landscape for tv):
- iPhone 6.9" 1320×2868, 6.5" 1242×2688, 5.5" 1242×2208; iPad 13" 2064×2752.
- Apple Watch Ultra 49mm 410×502, 46mm 416×496, 45mm 396×484, 41mm 352×430.
- Apple TV HD 1920×1080, 4K 3840×2160.
