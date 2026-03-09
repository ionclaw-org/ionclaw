import 'package:ionclaw-org/ionclaw.dart';

import 'notification_plugin.dart';
import 'platform_plugin.dart';

export 'platform_plugin.dart' show PlatformPlugin;

/// Central registry for platform plugins.
/// Automatically filters plugins by the current platform
/// and registers only the supported functions with the C++ bridge.
class PlatformHandler {
  static bool _initialized = false;
  static final Map<String, PlatformPlugin> _routes = {};
  static final List<PlatformPlugin> _plugins = [];

  /// Register a custom plugin. Must be called before [initialize].
  static void register(PlatformPlugin plugin) {
    _plugins.add(plugin);
  }

  static Future<void> initialize() async {
    if (_initialized) return;
    _initialized = true;

    // built-in plugins
    _plugins.insert(0, NotificationPlugin());

    // initialize only plugins available on this platform
    for (final plugin in _plugins) {
      if (!plugin.isAvailable) continue;

      await plugin.init();

      for (final fn in plugin.functions) {
        _routes[fn] = plugin;
      }
    }

    // register with C++ bridge — only functions available on this platform
    if (_routes.isNotEmpty) {
      Ionclaw.instance.setPlatformHandler(_handle, _routes.keys.toSet());
    }
  }

  static Future<String> _handle(String function, String params) async {
    final plugin = _routes[function];
    if (plugin == null) {
      return "Error: '$function' is not implemented on this platform.";
    }
    return plugin.handle(function, params);
  }
}
