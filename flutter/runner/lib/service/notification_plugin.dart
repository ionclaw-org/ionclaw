import 'dart:convert';
import 'dart:io';

import 'package:flutter_local_notifications/flutter_local_notifications.dart';
import 'package:permission_handler/permission_handler.dart';

import 'platform_plugin.dart';

/// Built-in plugin for local notifications.
/// Supports Android, iOS, macOS, and Linux.
class NotificationPlugin extends PlatformPlugin {
  final _notifications = FlutterLocalNotificationsPlugin();
  bool _ready = false;

  @override
  Set<String> get functions => {'local-notification.send'};

  @override
  Set<String> get platforms => {'android', 'ios', 'macos', 'linux'};

  @override
  Future<void> init() async {
    // request notification permission on Android 13+
    if (Platform.isAndroid) {
      final status = await Permission.notification.request();
      if (!status.isGranted) {
        return;
      }
    }

    InitializationSettings? settings;

    if (Platform.isAndroid) {
      settings = const InitializationSettings(
        android: AndroidInitializationSettings('@drawable/ic_notification'),
      );
    } else if (Platform.isIOS) {
      settings = const InitializationSettings(
        iOS: DarwinInitializationSettings(
          requestAlertPermission: true,
          requestBadgePermission: true,
          requestSoundPermission: true,
          defaultPresentAlert: true,
          defaultPresentSound: true,
          defaultPresentBadge: true,
          defaultPresentBanner: true,
          defaultPresentList: true,
        ),
      );
    } else if (Platform.isMacOS) {
      settings = const InitializationSettings(
        macOS: DarwinInitializationSettings(
          requestAlertPermission: true,
          requestBadgePermission: true,
          requestSoundPermission: true,
          defaultPresentAlert: true,
          defaultPresentSound: true,
          defaultPresentBadge: true,
          defaultPresentBanner: true,
          defaultPresentList: true,
        ),
      );
    } else if (Platform.isLinux) {
      settings = const InitializationSettings(
        linux: LinuxInitializationSettings(defaultActionName: 'Open'),
      );
    }

    if (settings != null) {
      _ready = await _notifications.initialize(settings: settings) ?? false;
    }
  }

  @override
  Future<String> handle(String function, String paramsJson) async {
    if (!_ready) {
      return 'Error: notifications failed to initialize on this platform';
    }

    try {
      final params = jsonDecode(paramsJson) as Map<String, dynamic>;
      final title = params['title'] as String? ?? 'IonClaw';
      final body = params['message'] as String? ?? '';

      const androidDetails = AndroidNotificationDetails(
        'ionclaw_default',
        'IonClaw Notifications',
        importance: Importance.high,
        priority: Priority.high,
        icon: '@drawable/ic_notification',
      );

      const darwinDetails = DarwinNotificationDetails(
        presentAlert: true,
        presentSound: true,
        presentBadge: true,
        presentBanner: true,
        presentList: true,
      );

      const linuxDetails = LinuxNotificationDetails();

      const details = NotificationDetails(
        android: androidDetails,
        iOS: darwinDetails,
        macOS: darwinDetails,
        linux: linuxDetails,
      );

      await _notifications.show(
        id: DateTime.now().millisecondsSinceEpoch ~/ 1000,
        title: title,
        body: body,
        notificationDetails: details,
      );

      return 'OK';
    } catch (e) {
      return 'Error: $e';
    }
  }
}
