import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:logging/logging.dart';

/// Tells the Android plugin that this isolate uses the native library, before
/// the isolate creates native state that an engine teardown has to undo.
///
/// The Android plugin tears the native engine down when its FlutterEngine is
/// destroyed or hot-restarted. It does that from the platform thread, and it
/// only calls into the library when something says the library is in use: an
/// app that never touched SoLoud must not pay for a multi-megabyte load in the
/// middle of an engine teardown. That something used to be only a marker the
/// library leaves in `/proc/self/maps` when it loads. Publishing the marker can
/// fail, though, and then the plugin would skip a teardown that the engine
/// claim and the callback registrations of this isolate still need.
///
/// So the isolate says it, in so many words, before
/// `prepareEngineInit` takes the claim and before `setDartEventCallbacks`
/// registers callbacks. The plugin keeps the signal for the rest of the
/// process. The marker stays as the fallback, so nothing here can fail an
/// initialization: a failure is logged and initialization continues.
class AndroidEngineLifecycle {
  /// Creates a bridge over the plugin's engine-local lifecycle channel.
  const AndroidEngineLifecycle();

  static final Logger _log = Logger('flutter_soloud.AndroidEngineLifecycle');

  /// The plugin's engine-local channel name, shared with iOS and macOS.
  @visibleForTesting
  static const String channelName = 'flutter_soloud/engine_lifecycle';

  /// The method the plugin answers with `true` once it has recorded the
  /// signal. Must match `FlutterSoloudPlugin.METHOD_NATIVE_LIBRARY_IN_USE`.
  @visibleForTesting
  static const String methodName = 'nativeLibraryInUse';

  /// Whether this isolate's plugin has acknowledged the signal.
  ///
  /// Static fields are per isolate, which is exactly the scope wanted: the
  /// plugin's record is process-wide and permanent, so one acknowledgement is
  /// enough for every later initialization on this isolate, while a new
  /// isolate (a hot restart, another engine) asks again and costs one round
  /// trip.
  static bool _acknowledged = false;

  /// Whether this isolate's engine turned out to have no lifecycle plugin to
  /// tell, as under `flutter test`. Remembered per isolate too: an engine
  /// normally registers its plugins before it runs any Dart, so asking again
  /// would only fail again.
  static bool _pluginMissing = false;

  /// Whether this platform runs the Android plugin at all.
  ///
  /// [kIsWeb] first: [defaultTargetPlatform] is [TargetPlatform.android] in a
  /// browser on an Android device, where there is no plugin to talk to.
  static bool get isSupported =>
      !kIsWeb && defaultTargetPlatform == TargetPlatform.android;

  /// Whether [markNativeLibraryInUse] still has work to do on this isolate.
  ///
  /// False off Android, and on Android once the plugin has acknowledged or
  /// turned out not to be registered. The caller checks it so that it only
  /// suspends when there is a message to send.
  static bool get isPending => isSupported && !_acknowledged && !_pluginMissing;

  /// Forgets what this isolate has learned, as a fresh isolate would.
  @visibleForTesting
  static void debugReset() {
    _acknowledged = false;
    _pluginMissing = false;
  }

  /// Sends the signal, unless this isolate already has.
  ///
  /// Returns whether the plugin has acknowledged it, now or earlier. Never
  /// throws: when the message cannot be delivered, the library's own marker is
  /// what the plugin goes by, and a failure here is only logged. A failure is
  /// not remembered, so the next initialization tries again, unless the
  /// plugin is not registered at all.
  Future<bool> markNativeLibraryInUse() async {
    if (!isSupported) return false;
    if (_acknowledged) return true;
    if (_pluginMissing) return false;

    // Resolved on its own, before anything is sent, so that "this isolate has
    // no Flutter messaging" gets its own, actionable, message. Info rather than
    // a warning: SoLoud.init() has never required a binding, and the marker
    // covers this case unless it also failed to publish.
    final BinaryMessenger messenger;
    try {
      messenger = ServicesBinding.instance.defaultBinaryMessenger;
    } on Object catch (error) {
      _log.info(
        'Flutter messaging is not available, so the Android plugin cannot be '
        'told that SoLoud is in use; automatic FlutterEngine teardown falls '
        "back to the native library's load marker. Calling "
        'WidgetsFlutterBinding.ensureInitialized() before SoLoud.init() arms '
        'it reliably. The engine is still torn down by an explicit deinit(), '
        'and a later init() recovers a stale engine.',
        error,
      );
      return false;
    }

    final channel = MethodChannel(
      channelName,
      const StandardMethodCodec(),
      messenger,
    );

    try {
      final acknowledged = await channel.invokeMethod<bool>(methodName);
      if (acknowledged ?? false) {
        _acknowledged = true;
        return true;
      }
      _log.warning(
        'The Android lifecycle plugin did not acknowledge that SoLoud is in '
        'use; automatic FlutterEngine teardown falls back to the native '
        "library's load marker.",
      );
      return false;
    } on MissingPluginException catch (error) {
      // An engine without the plugin registered: every engine under flutter
      // test, or one created with automatic plugin registration turned off.
      // Nothing to warn about, so it is logged only at FINE, and remembered,
      // so that later inits on this isolate neither ask nor log again.
      _pluginMissing = true;
      _log.fine(
        'The Android lifecycle plugin is not registered on this engine; '
        "automatic FlutterEngine teardown falls back to the native library's "
        'load marker.',
        error,
      );
      return false;
    } on Object catch (error, stackTrace) {
      // Unlike the iOS handshake, nothing here claims anything: the plugin
      // only records a flag, so whatever happened, it is safe to carry on.
      _log.warning(
        'Telling the Android lifecycle plugin that SoLoud is in use failed; '
        "automatic FlutterEngine teardown falls back to the native library's "
        'load marker.',
        error,
        stackTrace,
      );
      return false;
    }
  }
}
