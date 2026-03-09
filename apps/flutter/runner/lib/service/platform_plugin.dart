import 'dart:io';

/// A platform plugin that handles one or more invoke_platform functions.
/// Each plugin declares which platforms it supports.
///
/// To create a custom plugin, extend this class and register it
/// via [PlatformHandler.register] before calling [PlatformHandler.initialize].
abstract class PlatformPlugin {
  /// The set of function names this plugin handles.
  Set<String> get functions;

  /// The platforms this plugin supports.
  /// Values: `android`, `ios`, `macos`, `linux`, `windows`.
  Set<String> get platforms;

  /// Whether this plugin is available on the current platform.
  bool get isAvailable => platforms.contains(currentPlatform);

  /// Initialize the plugin. Called once during [PlatformHandler.initialize].
  Future<void> init();

  /// Handle a function call. Only called for functions in [functions].
  Future<String> handle(String function, String paramsJson);

  /// The current platform as a lowercase string.
  static String get currentPlatform {
    if (Platform.isAndroid) return 'android';
    if (Platform.isIOS) return 'ios';
    if (Platform.isMacOS) return 'macos';
    if (Platform.isLinux) return 'linux';
    if (Platform.isWindows) return 'windows';
    return 'unknown';
  }
}
