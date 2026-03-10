# Flutter App

IonClaw includes a native Flutter app for iOS, Android, macOS, Linux, and Windows. The app embeds the full C++ engine via FFI and runs everything locally on the device.

## Requirements

- [Flutter SDK](https://flutter.dev/docs/get-started/install) (stable channel)
- For iOS: Xcode 15+
- For Android: Android SDK, NDK, and JDK 17+
- For macOS: Xcode 15+

## Project Structure

```
apps/flutter/
  plugin/       C++ FFI plugin (platform bindings)
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
make run-flutter-ios         # iOS
make run-flutter-android     # Android
make run-flutter             # device picker
make run-flutter-release     # release mode, device picker
```

## Release Builds

### Android

1. Generate the upload keystore (one-time):

```bash
make android-gen-key
```

This creates `extras/android/upload-keystore.jks` and `extras/android/upload-certificate.pem`. The keystore is referenced by `apps/flutter/runner/android/key.properties`.

2. Build the release appbundle:

```bash
make release-android
```

Output: `apps/flutter/runner/build/app/outputs/bundle/release/app-release.aab`

Upload the `.aab` to Google Play Console. Google Play App Signing re-signs the app with the distribution key — the upload key is only used to authenticate the upload.

### iOS

```bash
make release-ios
```

Output: `apps/flutter/runner/build/ios/archive/Runner.xcarchive`

Open the archive in Xcode Organizer to validate and upload to App Store Connect.

### macOS

```bash
make release-macos
```

Output: `apps/flutter/runner/build/macos/Build/Products/Release/`

## Native Library Linking

The Flutter plugin loads the C++ shared library via FFI. The Makefile handles building and linking:

| Command | Description |
|---|---|
| `make link-flutter-macos` | Build macOS `.dylib` and symlink to plugin |
| `make link-flutter-ios` | Build iOS XCFramework and symlink to plugin |
| `make link-flutter-android` | Build Android `.so` for all ABIs and copy to plugin |
| `make link-flutter-web` | Build web client and symlink to plugin for bundling |

The `prepare-flutter-*` targets skip the build if the library already exists. Use `clean-*` followed by `link-flutter-*` to force a rebuild.

## Android Signing

The release signing config is in `apps/flutter/runner/android/key.properties`:

```properties
storePassword=upload
keyPassword=upload
keyAlias=upload
storeFile=../../../../../extras/android/upload-keystore.jks
```

The `build.gradle.kts` loads this file dynamically. If `key.properties` is missing, the build falls back to the debug signing config.

## Clean

```bash
make clean-lib       # Remove macOS shared library build
make clean-ios       # Remove iOS/XCFramework builds
make clean-android   # Remove Android builds and jniLibs
```
