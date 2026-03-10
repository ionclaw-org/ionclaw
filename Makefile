# IonClaw - Build System
# Usage: make help

.DEFAULT_GOAL := help

# --- directories ---
BUILD_DIR               := build/release
BUILD_DEBUG_DIR         := build/debug
BUILD_SHARED_DIR        := build/shared
BUILD_IOS_ARM64         := build/ios-arm64
BUILD_IOS_SIM_ARM64     := build/ios-sim-arm64
BUILD_IOS_SIM_X86       := build/ios-sim-x86_64
BUILD_XCFRAMEWORK       := build/xcframework
BUILD_ANDROID_ARM64     := build/android-arm64-v8a
BUILD_ANDROID_ARMV7     := build/android-armeabi-v7a
BUILD_ANDROID_X86_64    := build/android-x86_64
BUILD_ANDROID_X86       := build/android-x86
WEB_SRC_DIR             := apps/web
WEB_OUT_DIR             := main/resources/web
FLUTTER_PLUGIN_DIR      := apps/flutter/plugin
FLUTTER_RUNNER_DIR      := apps/flutter/runner
FLUTTER_ANDROID_JNILIBS := $(FLUTTER_PLUGIN_DIR)/android/src/main/jniLibs

# --- cmake flags ---
CMAKE_FLAGS        := -DCMAKE_BUILD_TYPE=Release
CMAKE_DEBUG_FLAGS  := -DCMAKE_BUILD_TYPE=Debug
CMAKE_SHARED_FLAGS := $(CMAKE_FLAGS) -DIONCLAW_BUILD_SHARED=ON

# --- ios toolchain ---
IOS_MIN_VERSION    := 13.0

# --- android toolchain ---
ANDROID_NDK        ?= $(ANDROID_NDK_ROOT)
ANDROID_API        := 24
ANDROID_TOOLCHAIN   = $(ANDROID_NDK)/build/cmake/android.toolchain.cmake
ANDROID_ABIS       := arm64-v8a armeabi-v7a x86_64 x86

# --- helpers ---
NPROC := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu)

# ============================================================
# Help
# ============================================================

.PHONY: help
help:
	@echo ""
	@echo "IonClaw Build System"
	@echo "===================="
	@echo ""
	@echo "Build:"
	@echo "  make build                   Build server executable (release)"
	@echo "  make build-debug             Build server executable (debug)"
	@echo "  make install                 Build and install to /usr/local/bin (symlink)"
	@echo "  make uninstall               Remove installed symlink"
	@echo "  make build-web               Build web client (Vue.js)"
	@echo "  make build-lib               Build shared library (macOS/Linux)"
	@echo "  make build-xcframework       Build iOS XCFramework"
	@echo "  make build-android           Build Android .so for all ABIs"
	@echo "  make build-all               Build everything (server + web + shared lib)"
	@echo "  make build-apple             Build all Apple targets (macOS + iOS)"
	@echo "  make build-docker            Build Docker image"
	@echo ""
	@echo "Run:"
	@echo "  make run                     Build and run server (release)"
	@echo "  make run-debug               Build and run server (debug)"
	@echo "  make run-web                 Run web client dev server (Vite HMR)"
	@echo "  make run-docker              Build and run Docker container"
	@echo "  make run-flutter             Build and run Flutter app (device picker)"
	@echo "  make run-flutter-release     Build and run Flutter app (release, device picker)"
	@echo "  make run-flutter-macos       Build and run Flutter app (macOS)"
	@echo "  make run-flutter-ios         Build and run Flutter app (iOS)"
	@echo "  make run-flutter-android     Build and run Flutter app (Android)"
	@echo ""
	@echo "Release:"
	@echo "  make release-android         Build Android appbundle (.aab)"
	@echo "  make release-ios             Build iOS archive (.ipa)"
	@echo "  make release-macos           Build macOS app"
	@echo "  make android-gen-key         Generate Android upload keystore and certificate"
	@echo ""
	@echo "Setup:"
	@echo "  make setup-web               Install web client npm dependencies"
	@echo "  make flutter-deps            Install and upgrade Flutter dependencies"
	@echo "  make prepare-flutter-macos   Build macOS dylib + web (if not present)"
	@echo "  make prepare-flutter-ios     Build iOS XCFramework (if not present)"
	@echo "  make prepare-flutter-android Build Android .so + web (if not present)"
	@echo "  make link-flutter-macos      Force rebuild macOS dylib to Flutter plugin"
	@echo "  make link-flutter-web        Force rebuild web client to Flutter plugin"
	@echo "  make link-flutter-ios        Force rebuild iOS XCFramework to Flutter plugin"
	@echo "  make link-flutter-android    Force rebuild Android .so files to Flutter plugin"
	@echo ""
	@echo "Clean:"
	@echo "  make clean                   Remove all build directories"
	@echo "  make clean-build             Remove server build directories"
	@echo "  make clean-web               Remove web client build output"
	@echo "  make clean-lib               Remove shared library build"
	@echo "  make clean-ios               Remove iOS/XCFramework builds"
	@echo "  make clean-android           Remove Android builds and jniLibs"
	@echo ""
	@echo "Code:"
	@echo "  make format                  Format all C/C++/ObjC sources with clang-format"
	@echo ""
	@echo "Options:"
	@echo "  ARGS=\"...\"                   Extra arguments passed to the server binary"
	@echo "  ANDROID_NDK=\"...\"            Android NDK root (defaults to ANDROID_NDK_ROOT)"
	@echo ""
	@echo "Examples:"
	@echo "  make run ARGS=\"--project /path/to/project --port 9090\""
	@echo "  make build-android ANDROID_NDK=/path/to/ndk"
	@echo ""

# ============================================================
# Build
# ============================================================

.PHONY: build
build: ## Build server executable (release)
	@echo "==> Building server (release)..."
	cmake -B $(BUILD_DIR) $(CMAKE_FLAGS)
	cmake --build $(BUILD_DIR) -j$(NPROC)
	@echo "==> Done: $(BUILD_DIR)/bin/ionclaw-server"

.PHONY: build-debug
build-debug: ## Build server executable (debug)
	@echo "==> Building server (debug)..."
	cmake -B $(BUILD_DEBUG_DIR) $(CMAKE_DEBUG_FLAGS)
	cmake --build $(BUILD_DEBUG_DIR) -j$(NPROC)
	@echo "==> Done: $(BUILD_DEBUG_DIR)/bin/ionclaw-server"

# --- install directories ---
ifeq ($(OS),Windows_NT)
    INSTALL_DIR ?= $(USERPROFILE)\bin
    BINARY_NAME := ionclaw-server.exe
    BUILT_BINARY := $(BUILD_DIR)/bin/Release/$(BINARY_NAME)
else
    INSTALL_DIR ?= $(HOME)/.local/bin
    BINARY_NAME := ionclaw-server
    BUILT_BINARY := $(shell pwd)/$(BUILD_DIR)/bin/$(BINARY_NAME)
endif

.PHONY: install
install: build ## Build and install ionclaw-server to INSTALL_DIR (default: ~/.local/bin)
ifeq ($(OS),Windows_NT)
	@echo "==> Installing to $(INSTALL_DIR)..."
	@if not exist "$(INSTALL_DIR)" mkdir "$(INSTALL_DIR)"
	copy /Y "$(BUILT_BINARY)" "$(INSTALL_DIR)\$(BINARY_NAME)"
	@echo "==> Installed: $(INSTALL_DIR)\$(BINARY_NAME)"
	@echo "    Make sure $(INSTALL_DIR) is in your PATH."
else
	@mkdir -p $(INSTALL_DIR)
	@echo "==> Installing symlink to $(INSTALL_DIR)/$(BINARY_NAME)..."
	ln -sf $(BUILT_BINARY) $(INSTALL_DIR)/$(BINARY_NAME)
	@echo "==> Installed: $(INSTALL_DIR)/$(BINARY_NAME) -> $(BUILT_BINARY)"
	@if ! echo "$$PATH" | grep -q "$(INSTALL_DIR)"; then \
		echo "    NOTE: Add $(INSTALL_DIR) to your PATH if not already:"; \
		echo "    export PATH=\"$(INSTALL_DIR):\$$PATH\""; \
	fi
endif

.PHONY: uninstall
uninstall: ## Remove installed ionclaw-server from INSTALL_DIR
ifeq ($(OS),Windows_NT)
	@echo "==> Removing $(INSTALL_DIR)\$(BINARY_NAME)..."
	@if exist "$(INSTALL_DIR)\$(BINARY_NAME)" del "$(INSTALL_DIR)\$(BINARY_NAME)"
else
	@echo "==> Removing $(INSTALL_DIR)/$(BINARY_NAME)..."
	rm -f $(INSTALL_DIR)/$(BINARY_NAME)
endif
	@echo "==> Uninstalled."

.PHONY: build-web
build-web: ## Build web client (Vue.js → main/resources/web/)
	@echo "==> Building web client..."
	cd $(WEB_SRC_DIR) && npm run build
	@echo "==> Done: $(WEB_OUT_DIR)/"

.PHONY: build-lib
build-lib: ## Build shared library for FFI (libionclaw.dylib/.so)
	@echo "==> Building shared library..."
	cmake -B $(BUILD_SHARED_DIR) $(CMAKE_SHARED_FLAGS)
	cmake --build $(BUILD_SHARED_DIR) -j$(NPROC)
	@echo "==> Done: $(BUILD_SHARED_DIR)/lib/"

.PHONY: build-ios-arm64
build-ios-arm64: ## Build iOS arm64 shared library
	@echo "==> Building iOS arm64..."
	cmake -B $(BUILD_IOS_ARM64) $(CMAKE_SHARED_FLAGS) \
		-DCMAKE_SYSTEM_NAME=iOS \
		-DCMAKE_OSX_ARCHITECTURES=arm64 \
		-DCMAKE_OSX_DEPLOYMENT_TARGET=$(IOS_MIN_VERSION) \
		-DCMAKE_OSX_SYSROOT=iphoneos
	cmake --build $(BUILD_IOS_ARM64) -j$(NPROC)
	@echo "==> Done: $(BUILD_IOS_ARM64)/lib/"

.PHONY: build-ios-sim-arm64
build-ios-sim-arm64: ## Build iOS simulator arm64 shared library
	@echo "==> Building iOS simulator arm64..."
	cmake -B $(BUILD_IOS_SIM_ARM64) $(CMAKE_SHARED_FLAGS) \
		-DCMAKE_SYSTEM_NAME=iOS \
		-DCMAKE_OSX_ARCHITECTURES=arm64 \
		-DCMAKE_OSX_DEPLOYMENT_TARGET=$(IOS_MIN_VERSION) \
		-DCMAKE_OSX_SYSROOT=iphonesimulator
	cmake --build $(BUILD_IOS_SIM_ARM64) -j$(NPROC)
	@echo "==> Done: $(BUILD_IOS_SIM_ARM64)/lib/"

.PHONY: build-ios-sim-x86
build-ios-sim-x86: ## Build iOS simulator x86_64 shared library
	@echo "==> Building iOS simulator x86_64..."
	cmake -B $(BUILD_IOS_SIM_X86) $(CMAKE_SHARED_FLAGS) \
		-DCMAKE_SYSTEM_NAME=iOS \
		-DCMAKE_OSX_ARCHITECTURES=x86_64 \
		-DCMAKE_OSX_DEPLOYMENT_TARGET=$(IOS_MIN_VERSION) \
		-DCMAKE_OSX_SYSROOT=iphonesimulator
	cmake --build $(BUILD_IOS_SIM_X86) -j$(NPROC)
	@echo "==> Done: $(BUILD_IOS_SIM_X86)/lib/"

.PHONY: build-xcframework
build-xcframework: build-ios-arm64 build-ios-sim-arm64 build-ios-sim-x86 ## Build iOS XCFramework
	@echo "==> Creating simulator fat framework..."
	mkdir -p $(BUILD_XCFRAMEWORK)/sim/ionclaw.framework
	lipo -create \
		$(BUILD_IOS_SIM_ARM64)/lib/ionclaw.framework/ionclaw \
		$(BUILD_IOS_SIM_X86)/lib/ionclaw.framework/ionclaw \
		-output $(BUILD_XCFRAMEWORK)/sim/ionclaw.framework/ionclaw
	cp $(BUILD_IOS_SIM_ARM64)/lib/ionclaw.framework/Info.plist $(BUILD_XCFRAMEWORK)/sim/ionclaw.framework/
	@echo "==> Creating XCFramework..."
	rm -rf $(BUILD_XCFRAMEWORK)/ionclaw.xcframework
	xcodebuild -create-xcframework \
		-framework $(BUILD_IOS_ARM64)/lib/ionclaw.framework \
		-framework $(BUILD_XCFRAMEWORK)/sim/ionclaw.framework \
		-output $(BUILD_XCFRAMEWORK)/ionclaw.xcframework
	@echo "==> Done: $(BUILD_XCFRAMEWORK)/ionclaw.xcframework"

.PHONY: build-android-arm64
build-android-arm64: ## Build Android arm64-v8a shared library
	@echo "==> Building Android arm64-v8a..."
	cmake -B $(BUILD_ANDROID_ARM64) $(CMAKE_SHARED_FLAGS) \
		-DCMAKE_TOOLCHAIN_FILE=$(ANDROID_TOOLCHAIN) \
		-DANDROID_ABI=arm64-v8a \
		-DANDROID_PLATFORM=android-$(ANDROID_API) \
		-DANDROID_STL=c++_static
	cmake --build $(BUILD_ANDROID_ARM64) -j$(NPROC)
	@echo "==> Done: $(BUILD_ANDROID_ARM64)/lib/"

.PHONY: build-android-armv7
build-android-armv7: ## Build Android armeabi-v7a shared library
	@echo "==> Building Android armeabi-v7a..."
	cmake -B $(BUILD_ANDROID_ARMV7) $(CMAKE_SHARED_FLAGS) \
		-DCMAKE_TOOLCHAIN_FILE=$(ANDROID_TOOLCHAIN) \
		-DANDROID_ABI=armeabi-v7a \
		-DANDROID_PLATFORM=android-$(ANDROID_API) \
		-DANDROID_STL=c++_static
	cmake --build $(BUILD_ANDROID_ARMV7) -j$(NPROC)
	@echo "==> Done: $(BUILD_ANDROID_ARMV7)/lib/"

.PHONY: build-android-x86_64
build-android-x86_64: ## Build Android x86_64 shared library
	@echo "==> Building Android x86_64..."
	cmake -B $(BUILD_ANDROID_X86_64) $(CMAKE_SHARED_FLAGS) \
		-DCMAKE_TOOLCHAIN_FILE=$(ANDROID_TOOLCHAIN) \
		-DANDROID_ABI=x86_64 \
		-DANDROID_PLATFORM=android-$(ANDROID_API) \
		-DANDROID_STL=c++_static
	cmake --build $(BUILD_ANDROID_X86_64) -j$(NPROC)
	@echo "==> Done: $(BUILD_ANDROID_X86_64)/lib/"

.PHONY: build-android-x86
build-android-x86: ## Build Android x86 shared library
	@echo "==> Building Android x86..."
	cmake -B $(BUILD_ANDROID_X86) $(CMAKE_SHARED_FLAGS) \
		-DCMAKE_TOOLCHAIN_FILE=$(ANDROID_TOOLCHAIN) \
		-DANDROID_ABI=x86 \
		-DANDROID_PLATFORM=android-$(ANDROID_API) \
		-DANDROID_STL=c++_static
	cmake --build $(BUILD_ANDROID_X86) -j$(NPROC)
	@echo "==> Done: $(BUILD_ANDROID_X86)/lib/"

.PHONY: build-android
build-android: build-android-arm64 build-android-armv7 build-android-x86_64 build-android-x86 ## Build Android shared libraries for all ABIs
	@echo "==> All Android ABIs built."

.PHONY: build-all
build-all: build-web build build-lib ## Build everything (web + server + shared lib)
	@echo "==> All targets built."

.PHONY: build-apple
build-apple: build-lib build-xcframework link-flutter-macos link-flutter-ios ## Build all Apple targets
	@echo "==> All Apple targets built."

.PHONY: build-docker
build-docker: ## Build Docker image
	@echo "==> Building Docker image..."
	docker build -t ionclaw .
	@echo "==> Done: docker image 'ionclaw'"

# ============================================================
# Run
# ============================================================

.PHONY: run
run: build ## Build and run server (release)
	@echo "==> Starting server..."
	$(BUILD_DIR)/bin/ionclaw-server $(ARGS)

.PHONY: run-debug
run-debug: build-debug ## Build and run server (debug)
	@echo "==> Starting server (debug)..."
	$(BUILD_DEBUG_DIR)/bin/ionclaw-server --debug $(ARGS)

.PHONY: run-web
run-web: ## Run web client dev server (Vite HMR)
	cd $(WEB_SRC_DIR) && npm run dev

.PHONY: run-docker
run-docker: build-docker ## Build and run Docker container
	@echo "==> Running Docker container..."
	docker run -p 8080:8080 -e PORT=8080 ionclaw

.PHONY: run-flutter
run-flutter: prepare-flutter-macos ## Build (if needed) and run Flutter app (device picker)
	@echo "==> Running Flutter app..."
	cd $(FLUTTER_RUNNER_DIR) && flutter run

.PHONY: run-flutter-release
run-flutter-release: prepare-flutter-macos ## Build (if needed) and run Flutter app (release, device picker)
	@echo "==> Running Flutter app (release)..."
	cd $(FLUTTER_RUNNER_DIR) && flutter run --release

.PHONY: run-flutter-macos
run-flutter-macos: prepare-flutter-macos ## Build (if needed) and run Flutter app (macOS)
	@echo "==> Running Flutter app (macOS)..."
	cd $(FLUTTER_RUNNER_DIR) && flutter run -d macos

.PHONY: run-flutter-ios
run-flutter-ios: prepare-flutter-ios ## Build (if needed) and run Flutter app (iOS)
	@echo "==> Running Flutter app (iOS)..."
	cd $(FLUTTER_RUNNER_DIR) && flutter run -d ios

.PHONY: run-flutter-android
run-flutter-android: prepare-flutter-android ## Build (if needed) and run Flutter app (Android)
	@echo "==> Running Flutter app (Android)..."
	cd $(FLUTTER_RUNNER_DIR) && flutter run -d android

# ============================================================
# Setup
# ============================================================

.PHONY: setup-web
setup-web: ## Install web client npm dependencies
	@echo "==> Installing web dependencies..."
	cd $(WEB_SRC_DIR) && npm install

.PHONY: prepare-flutter-macos
prepare-flutter-macos: ## Build macOS dylib + web client only if not present
	@if [ ! -f "$(BUILD_SHARED_DIR)/lib/libionclaw.dylib" ]; then $(MAKE) link-flutter-macos; \
	else echo "==> macOS dylib already built (skip). Use 'make clean-lib link-flutter-macos' to rebuild."; fi
	@if [ ! -d "$(WEB_OUT_DIR)" ]; then $(MAKE) link-flutter-web; \
	else echo "==> Web client already built (skip). Use 'make clean-web link-flutter-web' to rebuild."; fi

.PHONY: prepare-flutter-ios
prepare-flutter-ios: ## Build iOS XCFramework only if not present
	@if [ ! -d "$(BUILD_XCFRAMEWORK)/ionclaw.xcframework" ]; then $(MAKE) link-flutter-ios; \
	else echo "==> iOS XCFramework already built (skip). Use 'make clean-ios link-flutter-ios' to rebuild."; fi

.PHONY: prepare-flutter-android
prepare-flutter-android: ## Build Android .so files only if not present
	@if [ ! -d "$(FLUTTER_ANDROID_JNILIBS)/arm64-v8a" ]; then $(MAKE) link-flutter-android; \
	else echo "==> Android libraries already built (skip). Use 'make clean-android link-flutter-android' to rebuild."; fi
	@if [ ! -d "$(WEB_OUT_DIR)" ]; then $(MAKE) link-flutter-web; \
	else echo "==> Web client already built (skip). Use 'make clean-web link-flutter-web' to rebuild."; fi

.PHONY: link-flutter-macos
link-flutter-macos: build-lib ## Symlink macOS dylib to Flutter plugin
	@echo "==> Linking macOS dylib to Flutter plugin..."
	ln -sf $$(pwd)/$(BUILD_SHARED_DIR)/lib/libionclaw.dylib $(FLUTTER_PLUGIN_DIR)/macos/libionclaw.dylib
	@echo "==> Done: $(FLUTTER_PLUGIN_DIR)/macos/libionclaw.dylib"

.PHONY: link-flutter-web
link-flutter-web: build-web ## Symlink web client to Flutter plugin for bundling
	@echo "==> Linking web client to Flutter plugin..."
	ln -sf $$(pwd)/$(WEB_OUT_DIR) $(FLUTTER_PLUGIN_DIR)/macos/web
	@echo "==> Done: $(FLUTTER_PLUGIN_DIR)/macos/web"

.PHONY: link-flutter-android
link-flutter-android: build-android ## Copy Android .so files to Flutter plugin jniLibs
	@echo "==> Copying Android libraries to Flutter plugin..."
	mkdir -p $(FLUTTER_ANDROID_JNILIBS)/arm64-v8a
	mkdir -p $(FLUTTER_ANDROID_JNILIBS)/armeabi-v7a
	mkdir -p $(FLUTTER_ANDROID_JNILIBS)/x86_64
	mkdir -p $(FLUTTER_ANDROID_JNILIBS)/x86
	cp $(BUILD_ANDROID_ARM64)/lib/libionclaw.so $(FLUTTER_ANDROID_JNILIBS)/arm64-v8a/
	cp $(BUILD_ANDROID_ARMV7)/lib/libionclaw.so $(FLUTTER_ANDROID_JNILIBS)/armeabi-v7a/
	cp $(BUILD_ANDROID_X86_64)/lib/libionclaw.so $(FLUTTER_ANDROID_JNILIBS)/x86_64/
	cp $(BUILD_ANDROID_X86)/lib/libionclaw.so $(FLUTTER_ANDROID_JNILIBS)/x86/
	@echo "==> Done: $(FLUTTER_ANDROID_JNILIBS)/"

.PHONY: link-flutter-ios
link-flutter-ios: build-xcframework ## Symlink iOS XCFramework to Flutter plugin
	@echo "==> Linking iOS XCFramework to Flutter plugin..."
	ln -sf $$(pwd)/$(BUILD_XCFRAMEWORK)/ionclaw.xcframework $(FLUTTER_PLUGIN_DIR)/ios/ionclaw.xcframework
	@echo "==> Done: $(FLUTTER_PLUGIN_DIR)/ios/ionclaw.xcframework"

# ============================================================
# Release
# ============================================================

.PHONY: release-android
release-android: link-flutter-android link-flutter-web ## Build Android release (appbundle)
	@echo "==> Building Android release (appbundle)..."
	cd $(FLUTTER_RUNNER_DIR) && flutter build appbundle
	@echo "==> Done."
	@echo "Upload: $(FLUTTER_RUNNER_DIR)/build/app/outputs/bundle/release/app-release.aab"

.PHONY: release-ios
release-ios: link-flutter-ios link-flutter-web ## Build iOS release (ipa)
	@echo "==> Building iOS release (ipa)..."
	cd $(FLUTTER_RUNNER_DIR) && flutter build ipa
	@echo "==> Done."
	@echo "Open: $(FLUTTER_RUNNER_DIR)/build/ios/archive/Runner.xcarchive"

.PHONY: release-macos
release-macos: link-flutter-macos link-flutter-web ## Build macOS release
	@echo "==> Building macOS release..."
	cd $(FLUTTER_RUNNER_DIR) && flutter build macos
	@echo "==> Done."
	@echo "App: $(FLUTTER_RUNNER_DIR)/build/macos/Build/Products/Release/"

.PHONY: android-gen-key
android-gen-key: ## Generate Android upload keystore and certificate
	@if [ -f "extras/android/upload-keystore.jks" ]; then \
		echo "==> Keystore already exists: extras/android/upload-keystore.jks (skip)"; \
	else \
		echo "==> Generating Android upload keystore..."; \
		mkdir -p extras/android; \
		keytool -genkeypair -v \
			-keystore extras/android/upload-keystore.jks \
			-storepass upload \
			-keypass upload \
			-alias upload \
			-keyalg RSA -keysize 2048 -validity 10000 \
			-dname "CN=Upload, OU=Upload, O=Upload, L=Upload, ST=Upload, C=BR"; \
		keytool -export -rfc \
			-keystore extras/android/upload-keystore.jks \
			-storepass upload \
			-alias upload \
			-file extras/android/upload-certificate.pem; \
		echo "==> Done."; \
		echo "Keystore: extras/android/upload-keystore.jks"; \
		echo "Certificate: extras/android/upload-certificate.pem"; \
	fi

.PHONY: flutter-deps
flutter-deps: ## Install and upgrade Flutter dependencies
	cd $(FLUTTER_RUNNER_DIR) && flutter pub get && flutter pub upgrade

# ============================================================
# Clean
# ============================================================

.PHONY: clean
clean: ## Remove all build directories
	@echo "==> Cleaning all build directories..."
	rm -rf build
	@echo "==> Clean complete."

.PHONY: clean-build
clean-build: ## Remove server build directories
	rm -rf $(BUILD_DIR) $(BUILD_DEBUG_DIR)

.PHONY: clean-web
clean-web: ## Remove web client build output
	rm -rf $(WEB_OUT_DIR)

.PHONY: clean-lib
clean-lib: ## Remove shared library build
	rm -rf $(BUILD_SHARED_DIR)

.PHONY: clean-ios
clean-ios: ## Remove iOS/XCFramework build directories
	rm -rf $(BUILD_IOS_ARM64) $(BUILD_IOS_SIM_ARM64) $(BUILD_IOS_SIM_X86) $(BUILD_XCFRAMEWORK)

.PHONY: clean-android
clean-android: ## Remove Android build directories and jniLibs
	rm -rf $(BUILD_ANDROID_ARM64) $(BUILD_ANDROID_ARMV7) $(BUILD_ANDROID_X86_64) $(BUILD_ANDROID_X86)
	rm -rf $(FLUTTER_ANDROID_JNILIBS)

# ============================================================
# Code
# ============================================================

.PHONY: format
format: ## Format all C/C++/ObjC sources with clang-format
	@echo "==> Formatting sources..."
	find main -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.c" -o -name "*.h" -o -name "*.m" -o -name "*.mm" \) | xargs clang-format -i
	@echo "==> Done."
