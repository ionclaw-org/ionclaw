import 'dart:io';

import 'package:flutter/material.dart';
import 'package:path_provider/path_provider.dart';
import 'package:permission_handler/permission_handler.dart';
import 'package:share_plus/share_plus.dart';
import 'package:url_launcher/url_launcher.dart';
import 'package:webview_flutter/webview_flutter.dart';

class BrowserScreen extends StatefulWidget {
  final String url;

  const BrowserScreen({super.key, required this.url});

  @override
  State<BrowserScreen> createState() => _BrowserScreenState();
}

class _BrowserScreenState extends State<BrowserScreen> {
  late final Uri _baseUri;
  late final WebViewController _controller;
  bool _isPageLoading = true;

  @override
  void initState() {
    super.initState();
    _baseUri = Uri.parse(widget.url);

    _controller = WebViewController(
      onPermissionRequest: _onPermissionRequest,
    )
      ..setJavaScriptMode(JavaScriptMode.unrestricted)
      ..enableZoom(false)
      ..setNavigationDelegate(
        NavigationDelegate(
          onNavigationRequest: (request) {
            final uri = Uri.parse(request.url);
            final sameOrigin =
                uri.host == _baseUri.host && uri.port == _baseUri.port;

            if (!sameOrigin) {
              launchUrl(uri, mode: LaunchMode.externalApplication);
              return NavigationDecision.prevent;
            }

            // Intercept file download URLs
            if (uri.path.startsWith('/api/files/download/')) {
              _downloadFile(uri);
              return NavigationDecision.prevent;
            }

            return NavigationDecision.navigate;
          },
          onPageFinished: (_) {
            setState(() => _isPageLoading = false);
          },
        ),
      )
      ..loadRequest(Uri.parse(widget.url));
  }

  Future<void> _downloadFile(Uri uri) async {
    try {
      final client = HttpClient();
      final request = await client.getUrl(uri);
      final response = await request.close();

      // Extract filename from Content-Disposition or URL path
      final disposition =
          response.headers.value('content-disposition') ?? '';
      String filename;
      final match =
          RegExp(r'filename="?([^";\s]+)"?').firstMatch(disposition);
      if (match != null) {
        filename = match.group(1)!;
      } else {
        filename = Uri.decodeComponent(uri.pathSegments.last);
      }

      // Save to temp directory
      final dir = await getTemporaryDirectory();
      final file = File('${dir.path}/$filename');
      final sink = file.openWrite();
      await response.pipe(sink);

      // Present share sheet so user can save/share
      await SharePlus.instance.share(ShareParams(files: [XFile(file.path)]));
    } catch (e) {
      debugPrint('Download failed: $e');
    }
  }

  Future<void> _onPermissionRequest(WebViewPermissionRequest request) async {
    if (Platform.isAndroid) {
      for (final type in request.types) {
        if (type == WebViewPermissionResourceType.microphone) {
          final status = await Permission.microphone.request();
          if (!status.isGranted) {
            request.deny();
            return;
          }
        } else if (type == WebViewPermissionResourceType.camera) {
          final status = await Permission.camera.request();
          if (!status.isGranted) {
            request.deny();
            return;
          }
        }
      }
    }

    request.grant();
  }

  void _onGoHome() {
    setState(() => _isPageLoading = true);
    _controller.loadRequest(Uri.parse(widget.url));
  }

  Future<void> _onBackInvoked() async {
    final navigator = Navigator.of(context);
    final confirmed = await showDialog<bool>(
      context: context,
      barrierDismissible: false,
      builder: (context) => AlertDialog(
        title: const Text('Leave panel?'),
        content: const Text(
          'Do you want to leave the panel and return to the home screen?',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(context).pop(false),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () => Navigator.of(context).pop(true),
            child: const Text('Leave'),
          ),
        ],
      ),
    );
    if (confirmed == true && context.mounted) {
      navigator.pop();
    }
  }

  static const _headerBg = Color(0xFF1A1A2E);

  @override
  Widget build(BuildContext context) {
    final primary = Theme.of(context).colorScheme.primary;

    return PopScope(
      canPop: false,
      onPopInvokedWithResult: (didPop, result) async {
        if (didPop) return;
        await _onBackInvoked();
      },
      child: Scaffold(
        backgroundColor: _headerBg,
        appBar: AppBar(
          title: Image.asset('assets/logo-dark.png', height: 32),
          actions: [
            IconButton(
              icon: const Icon(Icons.home_outlined, size: 22),
              onPressed: _onGoHome,
              tooltip: 'Home',
            ),
          ],
        ),
        body: SafeArea(
          top: false,
          child: Stack(
            children: [
              WebViewWidget(controller: _controller),
              if (_isPageLoading)
                Center(
                  child: CircularProgressIndicator(
                    valueColor: AlwaysStoppedAnimation<Color>(primary),
                  ),
                ),
            ],
          ),
        ),
      ),
    );
  }
}
