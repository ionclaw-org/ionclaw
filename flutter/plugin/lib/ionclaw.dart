import 'dart:convert';
import 'dart:ffi' as ffi;
import 'dart:io';

import 'package:ffi/ffi.dart';

// native function signatures
typedef _ProjectInitNative = ffi.Pointer<ffi.Char> Function(
  ffi.Pointer<ffi.Char> path,
);

typedef _ProjectInitDart = ffi.Pointer<ffi.Char> Function(
  ffi.Pointer<ffi.Char> path,
);

typedef _ServerStartNative = ffi.Pointer<ffi.Char> Function(
  ffi.Pointer<ffi.Char> projectPath,
  ffi.Pointer<ffi.Char> host,
  ffi.Int32 port,
  ffi.Pointer<ffi.Char> rootPath,
  ffi.Pointer<ffi.Char> webPath,
);

typedef _ServerStartDart = ffi.Pointer<ffi.Char> Function(
  ffi.Pointer<ffi.Char> projectPath,
  ffi.Pointer<ffi.Char> host,
  int port,
  ffi.Pointer<ffi.Char> rootPath,
  ffi.Pointer<ffi.Char> webPath,
);

typedef _ServerStopNative = ffi.Pointer<ffi.Char> Function();
typedef _ServerStopDart = ffi.Pointer<ffi.Char> Function();

typedef _FreeNative = ffi.Void Function(ffi.Pointer<ffi.Char> ptr);
typedef _FreeDart = void Function(ffi.Pointer<ffi.Char> ptr);

// async platform handler callback (void return, called from any thread)
typedef _PlatformCallbackNative = ffi.Void Function(
  ffi.Int64 requestId,
  ffi.Pointer<ffi.Char> functionName,
  ffi.Pointer<ffi.Char> paramsJson,
);

typedef _SetPlatformHandlerNative = ffi.Void Function(
  ffi.Pointer<ffi.NativeFunction<_PlatformCallbackNative>> callback,
  ffi.Int32 timeoutSeconds,
);

typedef _SetPlatformHandlerDart = void Function(
  ffi.Pointer<ffi.NativeFunction<_PlatformCallbackNative>> callback,
  int timeoutSeconds,
);

// respond function — delivers the result back to the waiting C++ thread
typedef _PlatformRespondNative = ffi.Void Function(
  ffi.Int64 requestId,
  ffi.Pointer<ffi.Char> result,
);

typedef _PlatformRespondDart = void Function(
  int requestId,
  ffi.Pointer<ffi.Char> result,
);

class Ionclaw {
  static Ionclaw? _instance;
  Ionclaw._();
  static Ionclaw get instance => _instance ??= Ionclaw._();

  late ffi.DynamicLibrary _library;
  late _ProjectInitDart _projectInit;
  late _ServerStartDart _serverStart;
  late _ServerStopDart _serverStop;
  late _FreeDart _free;
  late _SetPlatformHandlerDart _setPlatformHandler;
  late _PlatformRespondDart _platformRespond;

  static ffi.NativeCallable<_PlatformCallbackNative>? _nativeCallable;
  static Set<String>? _supportedFunctions;
  static Future<String> Function(String function, String params)? _handler;

  bool _initialized = false;

  void initialize() {
    if (_initialized) return;
    _initialized = true;

    if (Platform.isAndroid || Platform.isLinux) {
      _library = ffi.DynamicLibrary.open('libionclaw.so');
    } else if (Platform.isWindows) {
      _library = ffi.DynamicLibrary.open('ionclaw.dll');
    } else {
      _library = ffi.DynamicLibrary.process();
    }

    _projectInit = _library
        .lookupFunction<_ProjectInitNative, _ProjectInitDart>(
            'ionclaw_project_init');

    _serverStart = _library
        .lookupFunction<_ServerStartNative, _ServerStartDart>(
            'ionclaw_server_start');

    _serverStop = _library
        .lookupFunction<_ServerStopNative, _ServerStopDart>(
            'ionclaw_server_stop');

    _free = _library
        .lookupFunction<_FreeNative, _FreeDart>('ionclaw_free');

    _setPlatformHandler = _library
        .lookupFunction<_SetPlatformHandlerNative, _SetPlatformHandlerDart>(
            'ionclaw_set_platform_handler');

    _platformRespond = _library
        .lookupFunction<_PlatformRespondNative, _PlatformRespondDart>(
            'ionclaw_platform_respond');
  }

  Map<String, dynamic> projectInit(String path) {
    final pathPtr = path.toNativeUtf8().cast<ffi.Char>();
    final resultPtr = _projectInit(pathPtr);
    calloc.free(pathPtr);

    final resultStr = resultPtr.cast<Utf8>().toDartString();
    _free(resultPtr);

    return jsonDecode(resultStr) as Map<String, dynamic>;
  }

  Map<String, dynamic> serverStart({
    String projectPath = '',
    String host = '',
    int port = 0,
    String rootPath = '',
    String webPath = '',
  }) {
    final projectPathPtr = projectPath.toNativeUtf8().cast<ffi.Char>();
    final hostPtr = host.toNativeUtf8().cast<ffi.Char>();
    final rootPathPtr = rootPath.toNativeUtf8().cast<ffi.Char>();
    final webPathPtr = webPath.toNativeUtf8().cast<ffi.Char>();

    final resultPtr = _serverStart(projectPathPtr, hostPtr, port, rootPathPtr, webPathPtr);

    calloc.free(projectPathPtr);
    calloc.free(hostPtr);
    calloc.free(rootPathPtr);
    calloc.free(webPathPtr);

    final resultStr = resultPtr.cast<Utf8>().toDartString();
    _free(resultPtr);

    return jsonDecode(resultStr) as Map<String, dynamic>;
  }

  Map<String, dynamic> serverStop() {
    final resultPtr = _serverStop();
    final resultStr = resultPtr.cast<Utf8>().toDartString();
    _free(resultPtr);

    return jsonDecode(resultStr) as Map<String, dynamic>;
  }

  /// Registers an async platform handler for invoke_platform tool calls.
  /// Only [supportedFunctions] are dispatched to the [handler].
  /// Unknown functions return an error to the agent.
  /// The C++ side blocks until the handler completes and responds.
  void setPlatformHandler(
      Future<String> Function(String function, String params) handler,
      Set<String> supportedFunctions,
      {int timeoutSeconds = 30}) {
    _nativeCallable?.close();
    _supportedFunctions = supportedFunctions;
    _handler = handler;

    _nativeCallable =
        ffi.NativeCallable<_PlatformCallbackNative>.listener(_onPlatformRequest);

    _setPlatformHandler(_nativeCallable!.nativeFunction, timeoutSeconds);
  }

  static void _onPlatformRequest(
    int requestId,
    ffi.Pointer<ffi.Char> functionName,
    ffi.Pointer<ffi.Char> paramsJson,
  ) {
    final function = functionName.cast<Utf8>().toDartString();

    if (_supportedFunctions == null ||
        !_supportedFunctions!.contains(function)) {
      _respond(requestId, "Error: '$function' is not implemented.");
      return;
    }

    final params = paramsJson.cast<Utf8>().toDartString();

    _handler!.call(function, params).then((result) {
      _respond(requestId, result);
    }).catchError((e) {
      _respond(requestId, 'Error: $e');
    });
  }

  static void _respond(int requestId, String result) {
    final resultPtr = result.toNativeUtf8().cast<ffi.Char>();
    Ionclaw.instance._platformRespond(requestId, resultPtr);
    calloc.free(resultPtr);
  }
}
