import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:logging/logging.dart';

/// What came of asking the iOS plugin to claim the native engine.
enum DarwinEnginePrepareResult {
  /// The plugin took the native lifecycle claim. Initialization continues, and
  /// automatic teardown on FlutterEngine destruction is armed.
  claimed,

  /// The channel could not be used at all, so nothing was claimed and nothing
  /// happened natively. The caller must take the claim itself.
  ///
  /// Only ever reported for failures that happen *before* the message is sent —
  /// this is not iOS, Flutter's messaging is not initialized, or the plugin is
  /// not registered. Never for a failure that might mean the handler ran.
  unavailable,

  /// The plugin refused, or the outcome of a sent request is unknown.
  ///
  /// Either way the caller must not claim the engine itself: the request may
  /// already have been committed, and a second claim would advance the native
  /// lifecycle generation a second time. This initialization is over.
  refused,
}

/// Hands the iOS or macOS plugin the engine id it cannot obtain for itself.
///
/// On Android the plugin reads `FlutterEngine.getEngineId()` directly. Neither
/// Apple platform exposes such a thing to a plugin, so the id has to travel
/// from the isolate, which gets it from `PlatformDispatcher.instance.engineId`.
/// Once the plugin knows it, it can retire that engine's callables and ask
/// native code to tear down the engine it owns when the FlutterEngine goes —
/// through `detachFromEngineForRegistrar:` on iOS, and through the plugin's own
/// deallocation on macOS, which has no such hook.
///
/// The native claim is taken by the *plugin*, inside the channel handler,
/// before it replies. Storing the id and letting Dart claim afterwards would
/// leave a window where the engine can be deallocated with no claim to tear
/// down — which is the case this exists to cover.
class DarwinEngineLifecycle {
  /// Creates a bridge over the plugin's engine-local lifecycle channel.
  const DarwinEngineLifecycle();

  static final Logger _log = Logger('flutter_soloud.DarwinEngineLifecycle');

  /// The plugin's engine-local channel name.
  @visibleForTesting
  static const String channelName = 'flutter_soloud/engine_lifecycle';

  /// Whether this platform runs the Darwin lifecycle plugin at all.
  ///
  /// Uses [defaultTargetPlatform] rather than `dart:io` so it works on every
  /// build target and so tests can drive both branches.
  static bool get isSupported =>
      defaultTargetPlatform == TargetPlatform.iOS ||
      defaultTargetPlatform == TargetPlatform.macOS;

  /// Asks the plugin to adopt [engineId] and claim the native engine, valid
  /// only while the native shutdown epoch is still [shutdownEpoch].
  ///
  /// The epoch is what makes a superseded request safe: `deinit()` can run
  /// while this call is suspended, and the claim must not land afterwards.
  Future<DarwinEnginePrepareResult> prepareEngineInit(
    int engineId,
    int shutdownEpoch,
  ) async {
    if (!isSupported) return DarwinEnginePrepareResult.unavailable;

    // Resolved before sending, and separately, so that "there is no messenger"
    // stays distinguishable from "the message was sent and something went
    // wrong". Only the former may fall back to claiming directly: the latter
    // might mean the handler already claimed.
    final BinaryMessenger messenger;
    try {
      messenger = ServicesBinding.instance.defaultBinaryMessenger;
    } on Object catch (error) {
      _log.warning(
        'Flutter messaging is not available, so automatic FlutterEngine '
        'teardown cannot be armed; initializing directly instead. Calling '
        'WidgetsFlutterBinding.ensureInitialized() before SoLoud.init() arms '
        'it. The engine is still torn down by an explicit deinit(), and a '
        'later init() recovers a stale engine.',
        error,
      );
      return DarwinEnginePrepareResult.unavailable;
    }

    final channel = MethodChannel(
      channelName,
      const StandardMethodCodec(),
      messenger,
    );

    try {
      final claimed = await channel.invokeMethod<bool>('prepareEngineInit', {
        'engineId': engineId,
        'shutdownEpoch': shutdownEpoch,
      });
      if (claimed ?? false) return DarwinEnginePrepareResult.claimed;

      // A reply that is not `true` is not something to work around.
      _log.warning('The lifecycle plugin did not claim engine $engineId.');
      return DarwinEnginePrepareResult.refused;
    } on MissingPluginException catch (error) {
      // Definitive: nothing handled the message, so nothing was claimed.
      _log.warning(
        'The lifecycle plugin is not registered, so automatic '
        'FlutterEngine teardown cannot be armed; initializing directly '
        'instead.',
        error,
      );
      return DarwinEnginePrepareResult.unavailable;
    } on Object catch (error, stackTrace) {
      // Everything else is either an explicit refusal (stale_prepare,
      // engine_detached, invalid arguments) or an unknown failure of a message
      // that was already sent. Both must be treated as refused: retrying or
      // falling back could claim the engine a second time on top of a request
      // the platform may have committed.
      _log.warning(
        'The lifecycle handshake for engine $engineId failed after the '
        'request was sent; abandoning this initialization rather than risking '
        'a duplicate native claim.',
        error,
        stackTrace,
      );
      return DarwinEnginePrepareResult.refused;
    }
  }
}
