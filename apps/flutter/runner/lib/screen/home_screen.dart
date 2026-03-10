import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:get/get.dart';
import 'package:ionclaw/ionclaw.dart';
import 'package:ionclaw_runner/screen/browser_screen.dart';
import 'package:ionclaw_runner/screen/settings_screen.dart';
import 'package:ionclaw_runner/service/platform_handler.dart';
import 'package:path_provider/path_provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen>
    with SingleTickerProviderStateMixin {
  static const _defaultHost = '0.0.0.0';
  static const _defaultPort = 8080;
  static const _prefHost = 'server_host';
  static const _prefPort = 'server_port';

  final _hostController = TextEditingController();
  final _portController = TextEditingController();

  bool _isRunning = false;
  bool _isLoading = false;
  String _serverHost = '';
  int _serverPort = 0;
  String _projectPath = '';
  List<String> _localAddresses = [];
  late AnimationController _pulseController;

  @override
  void initState() {
    super.initState();
    _pulseController = AnimationController(
      vsync: this,
      duration: const Duration(seconds: 2),
    )..repeat(reverse: true);
    _initialize();
  }

  @override
  void dispose() {
    _hostController.dispose();
    _portController.dispose();
    _pulseController.dispose();
    super.dispose();
  }

  Future<void> _initialize() async {
    await _resolveProjectPath();
    await _loadPreferences();
    await PlatformHandler.initialize();
  }

  Future<void> _resolveProjectPath() async {
    if (Platform.isIOS || Platform.isAndroid) {
      final dir = await getApplicationDocumentsDirectory();
      _projectPath = '${dir.path}/ionclaw/project';
    } else if (Platform.isMacOS) {
      final home = Platform.environment['HOME'] ?? '';
      _projectPath = '$home/Library/Containers/com.ionclaw.app/Data/ionclaw/project';
    } else {
      final dir = await getApplicationDocumentsDirectory();
      _projectPath = '${dir.path}/ionclaw/project';
    }
  }

  Future<void> _loadPreferences() async {
    final prefs = await SharedPreferences.getInstance();
    _hostController.text = prefs.getString(_prefHost) ?? _defaultHost;
    _portController.text =
        (prefs.getInt(_prefPort) ?? _defaultPort).toString();
  }

  Future<void> _savePreferences(String host, int port) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_prefHost, host);
    await prefs.setInt(_prefPort, port);
  }

  Future<List<String>> _getLocalAddresses() async {
    final addresses = <String>[];

    try {
      final interfaces = await NetworkInterface.list(
        type: InternetAddressType.IPv4,
      );

      for (final iface in interfaces) {
        for (final addr in iface.addresses) {
          addresses.add(addr.address);
        }
      }
    } catch (_) {}

    if (addresses.isEmpty) {
      addresses.add('127.0.0.1');
    }

    return addresses;
  }

  void _showToast(String message, {bool isError = false}) {
    Get.snackbar(
      isError ? 'Error' : 'Success',
      message,
      snackPosition: SnackPosition.BOTTOM,
      margin: const EdgeInsets.all(16),
      borderRadius: 12,
      duration: const Duration(seconds: 3),
      backgroundColor: isError
          ? const Color(0xFFFDECEA)
          : const Color(0xFFE8F5E9),
      colorText: isError
          ? const Color(0xFFC62828)
          : const Color(0xFF2E7D32),
      icon: Icon(
        isError ? Icons.error_outline : Icons.check_circle_outline,
        color: isError
            ? const Color(0xFFC62828)
            : const Color(0xFF2E7D32),
      ),
      snackStyle: SnackStyle.FLOATING,
    );
  }

  Future<void> _onInitProject() async {
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Initialize Project'),
        content: Text(
          'This will create the project structure at:\n\n$_projectPath\n\nExisting files will not be overwritten.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx, false),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(ctx, true),
            child: const Text('Initialize'),
          ),
        ],
      ),
    );

    if (confirmed != true) return;

    setState(() => _isLoading = true);

    final data = IonClaw.instance.projectInit(_projectPath);
    final success = data['success'] as bool;

    setState(() => _isLoading = false);

    if (success) {
      _showToast('Project initialized');
    } else {
      final error =
          data['error'] as String? ?? 'Failed to initialize project';
      _showToast(error, isError: true);
    }
  }

  Future<void> _onStartStop() async {
    setState(() => _isLoading = true);

    if (_isRunning) {
      IonClaw.instance.serverStop();

      setState(() {
        _isRunning = false;
        _isLoading = false;
        _serverHost = '';
        _serverPort = 0;
        _localAddresses = [];
      });

      _showToast('Server stopped');
    } else {
      final host = _hostController.text.trim();
      final port = int.tryParse(_portController.text.trim()) ?? _defaultPort;

      await _savePreferences(host, port);

      final initData = IonClaw.instance.projectInit(_projectPath);
      final initSuccess = initData['success'] as bool;

      if (!initSuccess) {
        final error =
            initData['error'] as String? ?? 'Failed to initialize project';
        setState(() => _isLoading = false);
        _showToast(error, isError: true);
        return;
      }

      final data = IonClaw.instance.serverStart(
        projectPath: _projectPath,
        host: host,
        port: port,
        rootPath: File(Platform.resolvedExecutable).parent.path,
        webPath: _resolveWebPath(),
      );

      final success = data['success'] as bool;

      if (success) {
        final addresses = await _getLocalAddresses();

        setState(() {
          _isRunning = true;
          _serverHost = data['host'] as String? ?? '';
          _serverPort = data['port'] as int? ?? 0;
          _localAddresses = addresses;
          _isLoading = false;
        });

      } else {
        final error = data['error'] as String? ?? 'Failed to start server';
        setState(() => _isLoading = false);
        _showToast(error, isError: true);
      }
    }
  }

  String _resolveWebPath() {
    if (Platform.isMacOS) {
      final contentsDir = File(Platform.resolvedExecutable).parent.parent;
      return '${contentsDir.path}/Resources/web';
    }

    return '';
  }

  void _onOpenBrowser() {
    Navigator.push(
      context,
      MaterialPageRoute(
        builder: (context) => BrowserScreen(
          url: 'http://$_serverHost:$_serverPort/app/',
        ),
      ),
    );
  }

  void _onOpenSettings() {
    Navigator.push(
      context,
      MaterialPageRoute(builder: (context) => const SettingsScreen()),
    );
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    return Scaffold(
      backgroundColor: const Color(0xFF1A1A2E),
      appBar: AppBar(
        title: Image.asset('assets/logo-dark.png', height: 32),
        actions: [
          IconButton(
            icon: const Icon(Icons.settings_outlined, size: 22),
            onPressed: _onOpenSettings,
            tooltip: 'Settings',
          ),
        ],
      ),
      body: SafeArea(
        top: false,
        child: Container(
          color: theme.scaffoldBackgroundColor,
          child: GestureDetector(
        onTap: () => FocusScope.of(context).unfocus(),
        behavior: HitTestBehavior.translucent,
        child: LayoutBuilder(
          builder: (context, constraints) {
            return SingleChildScrollView(
              physics: const AlwaysScrollableScrollPhysics(),
              child: ConstrainedBox(
                constraints:
                    BoxConstraints(minHeight: constraints.maxHeight),
                child: Center(
                  child: Padding(
                    padding: const EdgeInsets.symmetric(
                        horizontal: 24, vertical: 32),
                    child: Column(
                      mainAxisAlignment: MainAxisAlignment.center,
                      children: [
                        _buildStatusIndicator(theme),
                        const SizedBox(height: 32),
                        _buildConfigCard(theme),
                        const SizedBox(height: 28),
                        _buildActionButtons(theme),
                        if (_isRunning && _localAddresses.isNotEmpty) ...[
                          const SizedBox(height: 28),
                          _buildAddressesCard(theme),
                        ],
                      ],
                    ),
                  ),
                ),
              ),
            );
          },
        ),
      ),
      ),
      ),
    );
  }

  Widget _buildStatusIndicator(ThemeData theme) {
    return AnimatedBuilder(
      animation: _pulseController,
      builder: (context, child) {
        final opacity =
            _isRunning ? 0.4 + (_pulseController.value * 0.6) : 1.0;

        return Column(
          children: [
            Container(
              width: 56,
              height: 56,
              decoration: BoxDecoration(
                shape: BoxShape.circle,
                color: (_isRunning
                        ? const Color(0xFF4CAF50)
                        : Colors.grey.shade400)
                    .withValues(alpha: 0.12),
              ),
              child: Center(
                child: Container(
                  width: 20,
                  height: 20,
                  decoration: BoxDecoration(
                    shape: BoxShape.circle,
                    color: _isRunning
                        ? Color.fromRGBO(76, 175, 80, opacity)
                        : Colors.grey.shade400,
                    boxShadow: _isRunning
                        ? [
                            BoxShadow(
                              color: Color.fromRGBO(
                                  76, 175, 80, opacity * 0.3),
                              blurRadius: 12,
                              spreadRadius: 2,
                            ),
                          ]
                        : null,
                  ),
                ),
              ),
            ),
            const SizedBox(height: 12),
            Text(
              _isRunning ? 'Running' : 'Stopped',
              style: TextStyle(
                fontSize: 14,
                fontWeight: FontWeight.w600,
                color: _isRunning
                    ? const Color(0xFF2E7D32)
                    : Colors.grey.shade500,
                letterSpacing: 0.5,
              ),
            ),
            if (_isRunning && _serverHost.isNotEmpty) ...[
              const SizedBox(height: 4),
              Text(
                '$_serverHost:$_serverPort',
                style: TextStyle(
                  fontSize: 12,
                  fontFamily: 'monospace',
                  color: Colors.grey.shade600,
                ),
              ),
            ],
          ],
        );
      },
    );
  }

  Widget _buildConfigCard(ThemeData theme) {
    final primary = theme.colorScheme.primary;

    return SizedBox(
      width: 340,
      child: Card(
        child: Padding(
          padding: const EdgeInsets.all(20),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Container(
                    width: 32,
                    height: 32,
                    decoration: BoxDecoration(
                      borderRadius: BorderRadius.circular(8),
                      color: primary.withValues(alpha: 0.1),
                    ),
                    child: Icon(Icons.dns_outlined,
                        size: 18, color: primary),
                  ),
                  const SizedBox(width: 12),
                  Text(
                    'Server',
                    style: TextStyle(
                      fontSize: 16,
                      fontWeight: FontWeight.w600,
                      color: theme.colorScheme.onSurface,
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 20),
              Row(
                children: [
                  Expanded(
                    flex: 2,
                    child: TextField(
                      controller: _hostController,
                      enabled: !_isRunning,
                      decoration: const InputDecoration(
                        labelText: 'Host',
                        hintText: _defaultHost,
                        isDense: true,
                      ),
                      style: TextStyle(
                        fontSize: 14,
                        color: theme.colorScheme.onSurface,
                      ),
                    ),
                  ),
                  const SizedBox(width: 12),
                  Expanded(
                    flex: 1,
                    child: TextField(
                      controller: _portController,
                      enabled: !_isRunning,
                      decoration: const InputDecoration(
                        labelText: 'Port',
                        hintText: '8080',
                        isDense: true,
                      ),
                      style: TextStyle(
                        fontSize: 14,
                        color: theme.colorScheme.onSurface,
                      ),
                      keyboardType: TextInputType.number,
                      inputFormatters: [
                        FilteringTextInputFormatter.digitsOnly,
                        LengthLimitingTextInputFormatter(5),
                      ],
                    ),
                  ),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildActionButtons(ThemeData theme) {
    final primary = theme.colorScheme.primary;

    if (_isLoading) {
      return SizedBox(
        width: 32,
        height: 32,
        child: CircularProgressIndicator(
          strokeWidth: 2.5,
          valueColor: AlwaysStoppedAnimation<Color>(primary),
        ),
      );
    }

    return Column(
      children: [
        SizedBox(
          width: 240,
          height: 52,
          child: _isRunning
              ? OutlinedButton(
                  onPressed: _onStartStop,
                  style: OutlinedButton.styleFrom(
                    foregroundColor: const Color(0xFFC62828),
                    side: const BorderSide(
                        color: Color(0xFFC62828), width: 1.5),
                    shape: RoundedRectangleBorder(
                        borderRadius: BorderRadius.circular(12)),
                  ),
                  child: const Row(
                    mainAxisAlignment: MainAxisAlignment.center,
                    children: [
                      Icon(Icons.stop_rounded, size: 24),
                      SizedBox(width: 8),
                      Text(
                        'Stop Server',
                        style: TextStyle(
                            fontSize: 15, fontWeight: FontWeight.w600),
                      ),
                    ],
                  ),
                )
              : ElevatedButton(
                  onPressed: _onStartStop,
                  child: const Row(
                    mainAxisAlignment: MainAxisAlignment.center,
                    children: [
                      Icon(Icons.play_arrow_rounded, size: 24),
                      SizedBox(width: 8),
                      Text(
                        'Start Server',
                        style: TextStyle(
                            fontSize: 15, fontWeight: FontWeight.w600),
                      ),
                    ],
                  ),
                ),
        ),
        if (!_isRunning) ...[
          const SizedBox(height: 12),
          SizedBox(
            width: 240,
            height: 52,
            child: OutlinedButton(
              onPressed: _onInitProject,
              child: const Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  Icon(Icons.folder_open_outlined, size: 22),
                  SizedBox(width: 8),
                  Text(
                    'Initialize Project',
                    style: TextStyle(
                        fontSize: 15, fontWeight: FontWeight.w600),
                  ),
                ],
              ),
            ),
          ),
        ],
        if (_isRunning) ...[
          const SizedBox(height: 12),
          SizedBox(
            width: 240,
            height: 52,
            child: OutlinedButton(
              onPressed: _onOpenBrowser,
              child: const Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  Icon(Icons.language, size: 20),
                  SizedBox(width: 8),
                  Text(
                    'Open Panel',
                    style: TextStyle(
                        fontSize: 15, fontWeight: FontWeight.w600),
                  ),
                ],
              ),
            ),
          ),
        ],
      ],
    );
  }

  Widget _buildAddressesCard(ThemeData theme) {
    return SizedBox(
      width: 340,
      child: Card(
        child: Padding(
          padding: const EdgeInsets.all(20),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Container(
                    width: 32,
                    height: 32,
                    decoration: BoxDecoration(
                      borderRadius: BorderRadius.circular(8),
                      color: const Color(0xFF4CAF50).withValues(alpha: 0.1),
                    ),
                    child: const Icon(Icons.wifi,
                        size: 18, color: Color(0xFF2E7D32)),
                  ),
                  const SizedBox(width: 12),
                  Text(
                    'Network',
                    style: TextStyle(
                      fontSize: 16,
                      fontWeight: FontWeight.w600,
                      color: theme.colorScheme.onSurface,
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 16),
              ..._localAddresses.map(
                (addr) {
                  final url = 'http://$addr:$_serverPort';
                  return Material(
                    color: Colors.transparent,
                    child: InkWell(
                      onTap: () {
                        Clipboard.setData(ClipboardData(text: url));
                        _showToast('Copied to clipboard');
                      },
                      borderRadius: BorderRadius.circular(8),
                      child: Padding(
                        padding: const EdgeInsets.symmetric(
                            vertical: 8, horizontal: 8),
                        child: Row(
                          children: [
                            Expanded(
                              child: Text(
                                url,
                                style: TextStyle(
                                  fontSize: 13,
                                  fontFamily: 'monospace',
                                  color: Colors.grey.shade700,
                                ),
                              ),
                            ),
                            Icon(Icons.copy_outlined,
                                size: 16, color: Colors.grey.shade400),
                          ],
                        ),
                      ),
                    ),
                  );
                },
              ),
            ],
          ),
        ),
      ),
    );
  }
}
