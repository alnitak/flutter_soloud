// ignore_for_file: require_trailing_commas, avoid_positional_boolean_parameters

import 'dart:async';
import 'dart:collection';
import 'dart:ffi' as ffi;
import 'dart:isolate';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:flutter_soloud/src/audio_isolate.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:flutter_soloud/src/exceptions.dart';
import 'package:flutter_soloud/src/filter_params.dart';
import 'package:flutter_soloud/src/soloud_capture.dart';
import 'package:flutter_soloud/src/soloud_controller.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';
import 'package:flutter_soloud/src/utils/loader.dart';
import 'package:http/http.dart' as http;
import 'package:logging/logging.dart';
import 'package:meta/meta.dart';

/// sound event types
enum SoundEvent {
  /// handle reached the end of playback
  handleIsNoMoreValid,

  /// the sound has been disposed
  soundDisposed,
}

/// the type sent back to the user when a sound event occurs
typedef StreamSoundEvent = ({
  SoundEvent event,
  SoundProps sound,
  SoundHandle handle,
});

/// the sound class
class SoundProps {
  ///
  SoundProps(this.soundHash);

  /// The hash uniquely identifying this loaded sound.
  final SoundHash soundHash;

  /// This getter is [deprecated] and will be removed. Use [handles] instead.
  @Deprecated("Use 'handles' instead")
  UnmodifiableSetView<SoundHandle> get handle => handles;

  /// The handles of currently playing instances of this sound.
  ///
  /// A sound (expressed as [SoundProps]) can be loaded once, but then
  /// played multiple times. It's even possible to play several instances
  /// of the sound simultaneously.
  ///
  /// Each time you [SoLoud.play] a sound, you get back a [SoundHandle],
  /// and that same handle will be added to this [handles] set.
  /// When the sound finishes playing, its handle will be removed from this set.
  ///
  /// This set is unmodifiable.
  late final UnmodifiableSetView<SoundHandle> handles =
      UnmodifiableSetView(handlesInternal);

  /// The [internal] backing of [handles].
  ///
  /// Use [handles].
  @internal
  final Set<SoundHandle> handlesInternal = {};

  ///
  // TODO(me): make marker keys time able to trigger an event
  final List<double> keys = [];

  /// the user can listen ie when a sound ends or key events (TODO)
  final StreamController<StreamSoundEvent> soundEvents =
      StreamController.broadcast();

  @override
  String toString() {
    return 'soundHash: $soundHash has ${handles.length} active handles';
  }
}

/// The events exposed by the plugin
enum AudioEvent {
  /// emitted when audio isolate is started
  isolateStarted,

  /// emitted when audio isolate is stopped
  isolateStopped,

  /// emitted when audio capture is started
  captureStarted,

  /// emitted when audio capture is stopped
  captureStopped,
}

/// The main class to call all the audio methods that play sounds.
///
/// For methods that _capture_ sounds, use [SoLoudCapture].
interface class SoLoud {
  /// A deprecated way to access the singleton [instance] of SoLoud.
  ///
  /// The reason this is deprecated is that it leads to code that misrepresents
  /// what is actually happening. For example:
  ///
  /// ```dart
  /// // BAD
  /// var sound = await SoLoud().loadFile('path/to/sound.mp3');
  /// await SoLoud().play(sound)
  /// ```
  ///
  /// The code above suggests, on the face of it, that we're _constructing_
  /// two instances of [SoLoud], although we're in fact only accessing
  /// the singleton instance.
  @Deprecated('Use SoLoudPlayer.instance instead')
  factory SoLoud() => instance;

  /// The private constructor of [SoLoud]. This prevents developers from
  /// instantiating new instances.
  SoLoud._();

  static final Logger _log = Logger('flutter_soloud.SoLoud');

  /// The singleton instance of [SoLoud]. Only one SoLoud instance
  /// can exist in C++ land, so – for consistency and to avoid confusion
  /// – only one instance can exist in Dart land.
  ///
  /// Using this static field, you can get a hold of the single instance
  /// of this class from anywhere. This ability to access global state
  /// from anywhere can lead to hard-to-debug bugs, though, so it is
  /// preferable to encapsulate this and provide it through a facade.
  /// For example:
  ///
  /// ```dart
  /// final audioController = MyAudioController(SoLoudPlayer.instance);
  ///
  /// // Now provide the audio controller to parts of the app that need it.
  /// // No other part of the codebase need import `package:flutter_soloud`.
  /// ```
  ///
  /// Alternatively, at least create a field with the single instance
  /// of [SoLoud], and provide that (without the facade, but also without
  /// accessing [SoLoud.instance] from different places of the app).
  /// For example:
  ///
  /// ```dart
  /// class _MyWidgetState extends State<MyWidget> {
  ///   SoLoud? _soloud;
  ///
  ///   void _initializeSound() async {
  ///     // The only place in the codebase that accesses SoLoudPlayer.instance
  ///     // directly.
  ///     final soloud = SoLoudPlayer.instance;
  ///     await soloud.initialize();
  ///
  ///     setState(() {
  ///       _soloud = soloud;
  ///     });
  ///   }
  ///
  ///   // ...
  /// }
  /// ```
  static final SoLoud instance = SoLoud._();

  /// the way to talk to the audio isolate
  SendPort? _mainToIsolateStream;

  /// internally used to listen from isolate
  StreamController<dynamic>? _returnedEvent;

  /// the isolate used to spawn the audio management
  Isolate? _isolate;

  /// the way to receive events from audio isolate
  ReceivePort? _isolateToMainStream;

  /// A helper for loading files that aren't on disk.
  final SoLoudLoader _loader = SoLoudLoader();

  /// The backing private field for [isInitialized].
  bool _isInitialized = false;

  /// Whether or not is it possible to ask for wave and FFT data.
  bool _isVisualizationEnabled = false;

  /// The current status of the engine. This is `true` when the engine
  /// has been initialized and is immediately ready.
  ///
  /// The result will be `false` in all the following cases:
  ///
  /// - the engine was never initialized
  /// - it's being initialized right now (but not finished yet)
  /// - its most recent initialization failed
  /// - it's being shut down right now
  /// - it has been shut down
  ///
  /// You can `await` [initialized] instead if you want to wait for the engine
  /// to become ready (in case it's being initialized right now).
  ///
  /// Use [isInitialized] only if you want to check the current status of
  /// the engine synchronously and you don't care that it might be ready soon.
  bool get isInitialized => _isInitialized;

  /// The completer for an initialization in progress.
  ///
  /// This is `null` when the engine is not currently being initialized.
  Completer<PlayerErrors>? _initializeCompleter;

  /// The completer for a shutdown in progress.
  ///
  /// This is `null` when the engine is not currently being shut down.
  Completer<bool>? _shutdownCompleter;

  /// A [Future] that returns `true` when the audio engine is initialized
  /// (and ready to play sounds, for example).
  ///
  /// You can call this at any time. For example:
  ///
  /// ```dart
  /// void onPressed() async {
  ///   if (await SoLoud.instance.initialized) {
  ///     // The audio engine is ready. We can play sounds now.
  ///     await SoLoud.instance.play(sound);
  ///   }
  /// }
  /// ```
  ///
  /// The future will complete immediately (synchronously) if the engine is
  /// either already initialized (`true`),
  /// or it had failed to initialize (`false`),
  /// or it was already shut down (`false`),
  /// or it is _being_ shut down (`false`),
  /// or when there wasn't ever a call to [initialize] at all (`false`).
  ///
  /// If the engine is in the middle of initializing, the future will complete
  /// when the initialization is done. It will be `true` if the initialization
  /// was successful, and `false` if it failed. The future will never throw.
  ///
  /// It is _not_ needed to await this future after a call to [initialize].
  /// The [initialize] method already returns a future, and it is the
  /// same future that this getter returns.
  ///
  /// ```dart
  /// final result = await SoLoud.instance.initialize();
  /// await SoLoud.instance.initialized;  // NOT NEEDED
  /// ```
  ///
  /// This getter ([initialized]) is useful when you want to check the status
  /// of the engine from places in your code that _don't_ do the initialization.
  /// For example, a widget down the widget tree.
  ///
  /// If you need a version of this that is synchronous,
  /// or if you don't care that the engine might be initializing right now
  /// and therefore ready in a moment,
  /// use [isInitialized] instead.
  FutureOr<bool> get initialized {
    if (_initializeCompleter == null) {
      // We are _not_ during initialization. Return synchronously.
      return _isInitialized;
    }

    // We are in the middle of initializing the engine. Wait for that to
    // complete and return `true` if it was successful.
    return _initializeCompleter!.future
        .then((error) => error == PlayerErrors.noError);
  }

  /// Stream audio events
  @Deprecated(
      'Instead of listening to events, just await initialize() and shutdown()')
  StreamController<AudioEvent> audioEvent = StreamController.broadcast();

  /// status of player
  @Deprecated('Use SoLoud.isInitialized (or await SoLoud.initialized) instead')
  bool get isPlayerInited => _isInitialized;

  /// Status of the engine.
  ///
  /// Since the engine is initialized as a part of the
  /// more general initialization process, this field is only an internal
  /// control mechanism. Users should use [initialized] instead.
  ///
  /// The field is useful in [disposeAllSound], which is called from [shutdown]
  /// (so [isInitialized] is already `false` at that point).
  bool _isEngineInitialized = false;

  /// status of capture
  @Deprecated('Use SoLoudCapture.isCaptureInited instead')
  bool get isCaptureInited => SoLoudCapture.instance.isCaptureInited;

  /// Used both in main and audio isolates
  /// should be synchronized with each other
  final List<SoundProps> activeSounds = [];

  /// Wait for the isolate to return after the event has been completed.
  /// The event must be recognized by [event] and [args] sent to
  /// the audio isolate.
  /// ie:
  /// - call [loadFile()] with completeFileName arg
  /// - wait the audio isolate to call the FFI loadFile function
  /// - the audio isolate will then send back the args used in the call and
  ///   eventually the return value of the FFI function
  ///
  Future<dynamic> _waitForEvent(MessageEvents event, Record args) async {
    final completer = Completer<dynamic>();

    await _returnedEvent?.stream.firstWhere((element) {
      final e = element as Map<String, Object?>;

      // if the event with its args are what we are waiting for...
      if ((e['event']! as MessageEvents) != event) return false;
      if ((e['args']! as Record) != args) return false;

      // return the result
      completer.complete(e['return']);
      return true;
    });

    return completer.future;
  }

  /// Initializes the audio engine.
  ///
  /// Use [initialize] instead. This method is simply an alias for [initialize]
  /// for backwards compatibility. It will be removed in a future version.
  @Deprecated('use initialize() instead')
  Future<PlayerErrors> startIsolate() => initialize();

  /// Initializes the audio engine.
  ///
  /// Run this before anything else, and `await` its result. Only when
  /// this method returns [PlayerErrors.noError] will the engine
  /// be ready.
  ///
  /// If you call any other methods (such as [play]) before initialization
  /// completes, those calls will be ignored and you will get
  /// a [PlayerErrors.engineNotInited] back.
  ///
  /// The [timeout] parameter is the maximum time to wait for the engine
  /// to initialize. If the engine doesn't initialize within this time,
  /// the method will return [PlayerErrors.engineInitializationTimedOut].
  /// The default timeout is 10 seconds.
  ///
  /// If [automaticCleanup] is `true`, the temporary directory that
  /// the engine uses for storing sound files will be purged occasionally
  /// (e.g. on shutdown, and on startup in case shutdown never has the chance
  /// to properly finish).
  /// This is especially important when the program plays a lot of
  /// different files during its lifetime (e.g. a music player
  /// loading tracks from the network). For applications and games
  /// that play sounds from assets or from the file system, this is probably
  /// unnecessary, as the amount of data will be finite.
  /// The default is `false`.
  ///
  /// It is safe to call this function even if the engine is currently being
  /// shut down. In that case, the function will wait for [shutdown]
  /// to properly complete before initializing again.
  ///
  /// (This method was formerly called `startIsolate()`.)
  Future<PlayerErrors> initialize({
    Duration timeout = const Duration(seconds: 10),
    bool automaticCleanup = false,
  }) async {
    _log.finest('initialize() called');
    // Start the audio isolate and listen for messages coming from it.
    // Messages are streamed with [_returnedEvent] and processed
    // by [_waitForEvent] when they come.

    if (_isInitialized) {
      _log.severe('initialize() called when the engine is already initialized');
      return PlayerErrors.multipleInitialization;
    }

    if (_initializeCompleter != null) {
      _log.severe('initialize() called while already initializing');
      return PlayerErrors.concurrentInitialization;
    }

    activeSounds.clear();
    final completer = Completer<PlayerErrors>();
    _initializeCompleter = completer;

    if (_shutdownCompleter != null) {
      // We are in the middle of shutting down the engine.
      // We should wait for that to complete before initializing again.
      final success = await _shutdownCompleter!.future;
      if (!success) {
        // The engine failed to shut down. We can't initialize it.
        _log.severe('initialize() called while the engine is shutting down '
            'but the shutdown failed');
        _initializeCompleter = null;
        return PlayerErrors.unknownError;
      }
    }

    _isolateToMainStream = ReceivePort();
    _returnedEvent = StreamController.broadcast();

    _isolateToMainStream?.listen((data) {
      if (data is SendPort) {
        _mainToIsolateStream = data;

        /// finally start the audio engine
        _initEngine().then((value) {
          assert(
              !_isInitialized,
              '_isInitialized should be false at this point. '
              'There might be a bug in the code that tries to prevent '
              'multiple concurrent initializations.');
          if (value == PlayerErrors.noError) {
            // ignore: deprecated_member_use_from_same_package
            audioEvent.add(AudioEvent.isolateStarted);
            _isInitialized = true;

            /// get the visualization flag from the player on C side.
            /// Eventually we can set this as a parameter during the
            /// initialization with some other parameters like `sampleRate`
            _isVisualizationEnabled = getVisualizationEnabled().isEnabled;
          } else {
            _log.severe('_initEngine() failed with error: $value');
            _cleanUpUnsuccessfulInitialization();
          }
          assert(_initializeCompleter == completer,
              '_initializeCompleter has been reassigned during initialization');
          _initializeCompleter = null;
          completer.complete(value);
        });
      } else {
        _log.finest(() => 'main isolate received: $data');
        if (data is StreamSoundEvent) {
          _log.finer(
              () => 'Main isolate received a sound event: ${data.event}  '
                  'handle: ${data.handle}  '
                  'sound: ${data.sound}');

          /// find the sound which received the [SoundEvent] and...
          final sound = activeSounds.firstWhere(
            (sound) => sound.soundHash == data.sound.soundHash,
            orElse: () {
              _log.info(() => 'Received an event for sound with handle: '
                  "${data.handle} but such sound isn't among activeSounds.");
              return SoundProps(SoundHash.invalid());
            },
          );

          /// send the disposed event to listeners and remove the sound
          if (data.event == SoundEvent.soundDisposed) {
            sound.soundEvents.add(data);
            activeSounds.removeWhere(
                (element) => element.soundHash == data.sound.soundHash);
          }

          /// send the handle event to the listeners and remove it
          if (data.event == SoundEvent.handleIsNoMoreValid) {
            /// ...put in its own stream the event, then remove the handle
            if (sound.soundHash.isValid) {
              sound.soundEvents.add(data);
              sound.handlesInternal.removeWhere(
                (handle) {
                  return handle == data.handle;
                },
              );
            }
          }
        } else {
          // if not a StreamSoundEvent, queue this into [_returnedEvent]
          _returnedEvent?.add(data);
        }
      }
    });

    _isolate =
        await Isolate.spawn(audioIsolate, _isolateToMainStream!.sendPort);
    if (_isolate == null) return PlayerErrors.isolateNotStarted;

    _loader.automaticCleanup = automaticCleanup;
    await _loader.initialize();

    return completer.future.timeout(timeout, onTimeout: () {
      _log.severe('initialize() timed out');
      assert(_initializeCompleter == completer,
          '_initializeCompleter has been reassigned');
      _initializeCompleter = null;
      _cleanUpUnsuccessfulInitialization();
      return PlayerErrors.engineInitializationTimedOut;
    });
  }

  /// Used to clean up after an unsuccessful or interrupted initialization.
  void _cleanUpUnsuccessfulInitialization() {
    _isolateToMainStream?.close();
    _isolateToMainStream = null;
    _mainToIsolateStream = null;
    _returnedEvent?.close();
    _returnedEvent = null;
    _isolate?.kill();
    _isolate = null;
    _isEngineInitialized = false;
  }

  /// An alias for [shutdown], for backwards compatibility.
  ///
  /// Use [shutdown] instead. The [stopIsolate] alias will be removed
  /// in a future version.
  @Deprecated('use dispose() instead')
  Future<bool> stopIsolate() => shutdown();

  /// Stops the engine and disposes of all resources, including sounds
  /// and the audio isolate.
  ///
  /// Returns `true` when everything has been disposed. Returns `false`
  /// if there was nothing to dispose (e.g. the engine hasn't ever been
  /// successfully initialized).
  ///
  /// It is safe to call this function even if the engine is currently being
  /// initialized. In that case, the function will wait for [initialize]
  /// to properly complete before shutting down.
  ///
  /// (This method was formerly called `stopIsolate()`.)
  Future<bool> shutdown() async {
    _log.finest('shutdown() called');

    if (_initializeCompleter != null) {
      // We are in the middle of initializing the engine.
      // We should wait for that to complete before disposing.
      assert(!_isInitialized,
          '_isInitialized should be false before initialization completes');
      final error = await _initializeCompleter!.future;
      if (error != PlayerErrors.noError) {
        // The engine failed to initialize. Nothing to do.
        assert(!_isInitialized,
            '_isInitialized should be false when initialization fails');
        return false;
      }
    }

    if (_shutdownCompleter != null) {
      // We are already in the middle of shutting down the engine.
      assert(
          !_isInitialized, '_isInitialized should be false when shutting down');
      return _shutdownCompleter!.future;
    }

    if (!_isInitialized) {
      // The engine isn't initialized.
      _log.warning('shutdown() called when the engine is not initialized');
      return false;
    }

    _isInitialized = false;

    final completer = Completer<bool>();
    _shutdownCompleter = completer;

    await disposeAllSound();
    await _stopLoop();
    // Engine will be disposed below when the audio isolate exits,
    // so just set this variable to false.
    _isEngineInitialized = false;
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.exitIsolate,
        'args': (),
      },
    );
    await _waitForEvent(MessageEvents.exitIsolate, ());
    await _returnedEvent?.close();
    _returnedEvent = null;
    _isolateToMainStream?.close();
    _isolateToMainStream = null;
    _isolate?.kill();
    _isolate = null;
    // ignore: deprecated_member_use_from_same_package
    audioEvent.add(AudioEvent.isolateStopped);

    assert(_shutdownCompleter == completer,
        '_shutdownCompleter has been reassigned');
    _shutdownCompleter = null;

    completer.complete(true);
    return completer.future;
  }

  /// return true if the audio isolate is running
  ///
  @Deprecated('Use isInitialized (or await SoLoud.initialized) instead')
  bool isIsolateRunning() {
    return _isolate != null;
  }

  //////////////////////////////////////////////////
  /// isolate loop events management
  //////////////////////////////////////////////////

  /// Start the isolate loop to catch the end
  /// of sounds (handles) playback or keys
  ///
  /// The loop recursively call itself to check the state of
  /// all active sound handles. Therefore it can cause some lag for
  /// other event calls.
  /// Not starting this will implies not receive [SoundEvent]s,
  /// it will therefore be up to the developer to check
  /// the sound handle validity
  ///
  Future<bool> _startLoop() async {
    _log.finest('_startLoop() called');
    if (_isolate == null || !_isEngineInitialized) return false;

    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.startLoop,
        'args': (),
      },
    );
    await _waitForEvent(MessageEvents.startLoop, ());
    return true;
  }

  /// stop the [SoundEvent]s loop
  ///
  Future<bool> _stopLoop() async {
    _log.finest('_stopLoop() called');
    if (_isolate == null || !_isEngineInitialized) return false;

    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.stopLoop,
        'args': (),
      },
    );
    await _waitForEvent(MessageEvents.stopLoop, ());
    return true;
  }

  // ////////////////////////////////////////////////
  // Below all the methods implemented with FFI for the player
  // ////////////////////////////////////////////////

  /// A deprecated method that manually starts the engine.
  ///
  /// Do not use. The engine is fully started with [initialize].
  /// This method will be removed in a future version.
  @Deprecated('Use initialize() instead')
  Future<PlayerErrors> initEngine() => _initEngine();

  /// Initialize the audio engine.
  ///
  /// Defaults are:
  /// Miniaudio audio backend
  /// sample rate 44100
  /// buffer 2048
  ///
  Future<PlayerErrors> _initEngine() async {
    _log.finest('_initEngine() called');
    if (_isolate == null) return PlayerErrors.isolateNotStarted;
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.initEngine,
        'args': (),
      },
    );
    final ret =
        await _waitForEvent(MessageEvents.initEngine, ()) as PlayerErrors;
    _isEngineInitialized = ret == PlayerErrors.noError;
    _logPlayerError(ret, from: '_initEngine() result');

    /// start also the loop in the audio isolate
    if (_isEngineInitialized) {
      await _startLoop();
    }
    return ret;
  }

  /// A deprecated method that manually disposes the engine
  /// (and only the engine).
  ///
  /// Do not use. The engine is fully disposed within [shutdown].
  /// This method will be removed in a future version.
  @Deprecated('Use shutdown() instead')
  Future<bool> disposeEngine() => _disposeEngine();

  /// Stop the engine
  /// The audio isolate doesn't get killed
  ///
  /// Returns true if success
  ///
  Future<bool> _disposeEngine() async {
    _log.finest('_disposeEngine() called');
    if (_isolate == null || !_isEngineInitialized) return false;

    await disposeAllSound();

    /// first stop the loop
    await _stopLoop();
    _isEngineInitialized = false;

    /// then ask to audio isolate to dispose the engine
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.disposeEngine,
        'args': (),
      },
    );
    await _waitForEvent(MessageEvents.disposeEngine, ());
    return true;
  }

  /// Load a new sound to be played once or multiple times later, from
  /// the file system.
  ///
  /// [completeFileName] the complete file path.
  /// [LoadMode] if `LoadMode.memory`, the whole uncompressed RAW PCM
  /// audio is loaded into memory. Used to prevent gaps or lags
  /// when seeking/starting a sound (less CPU, more memory allocated).
  /// If `LoadMode.disk` is used, the audio data is loaded
  /// from the given file when needed (more CPU, less memory allocated).
  /// See the [seek] note problem when using [LoadMode] = `LoadMode.disk`.
  /// Default is `LoadMode.memory`.
  /// Returns PlayerErrors.noError if success and a new sound.
  ///
  Future<({PlayerErrors error, SoundProps? sound})> loadFile(
    String completeFileName, {
    LoadMode mode = LoadMode.memory,
  }) async {
    if (!isInitialized) {
      _log.severe(() => 'loadFile(): ${PlayerErrors.engineNotInited}');
      return (error: PlayerErrors.engineNotInited, sound: null);
    }
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.loadFile,
        'args': (completeFileName: completeFileName, mode: mode),
      },
    );
    final ret = (await _waitForEvent(
      MessageEvents.loadFile,
      (completeFileName: completeFileName, mode: mode),
    )) as ({PlayerErrors error, SoundProps? sound});
    if (ret.error == PlayerErrors.noError) {
      activeSounds.add(ret.sound!);
    }
    _logPlayerError(ret.error, from: 'loadFile() result');
    return (error: ret.error, sound: ret.sound);
  }

  /// Load a new sound to be played once or multiple times later, from
  /// an asset.
  ///
  /// Provide the [key] of the asset to load (e.g. `assets/sound.mp3`).
  ///
  /// You can provide a custom [assetBundle]. By default, the [rootBundle]
  /// is used.
  ///
  /// Since SoLoud can only play from files, the asset will be copied to
  /// a temporary file, and that file will be used to load the sound.
  ///
  /// Throws a [FlutterError] if the asset is not found.
  ///
  /// Returns [PlayerErrors.noError] if loading was successful,
  /// as well as the new sound. Returns [PlayerErrors.assetLoadFailed]
  /// if there was a problem creating the temporary file.
  /// Returns [PlayerErrors.engineNotInited] if the engine is not initialized.
  Future<({PlayerErrors error, SoundProps? sound})> loadAsset(
    String key, {
    LoadMode mode = LoadMode.memory,
    AssetBundle? assetBundle,
  }) async {
    if (!isInitialized) {
      _log.severe(() => 'loadAsset(): ${PlayerErrors.engineNotInited}');
      return (error: PlayerErrors.engineNotInited, sound: null);
    }

    final file = await _loader.loadAsset(key, assetBundle: assetBundle);

    if (file == null) {
      return (error: PlayerErrors.assetLoadFailed, sound: null);
    }

    return loadFile(file.absolute.path, mode: mode);
  }

  /// Load a new sound to be played once or multiple times later, from
  /// a network URL.
  ///
  /// Provide the [url] of the sound to load.
  ///
  /// Optionally, you can provide your own [httpClient]. This is a good idea
  /// if you're loading several files in a short span of time (such as
  /// on program startup). When no [httpClient] is provided,
  /// a new one will be created (and closed afterwards) for each call.
  ///
  /// Since SoLoud can only play from files, the downloaded data will be
  /// copied to a temporary file, and that file will be used to load the sound.
  ///
  /// Throws [FormatException] if the [url] is invalid.
  /// Throws [SoLoudNetworkException] if the request fails.
  ///
  /// Returns [PlayerErrors.noError] if loading was successful,
  /// as well as the new sound. Returns [PlayerErrors.assetLoadFailed]
  /// if there was a problem creating the temporary file.
  /// Returns [PlayerErrors.engineNotInited] if the engine is not initialized.
  Future<({PlayerErrors error, SoundProps? sound})> loadUrl(
    String url, {
    LoadMode mode = LoadMode.memory,
    http.Client? httpClient,
  }) async {
    if (!isInitialized) {
      _log.severe(() => 'loadUrl(): ${PlayerErrors.engineNotInited}');
      return (error: PlayerErrors.engineNotInited, sound: null);
    }

    final file = await _loader.loadUrl(url, httpClient: httpClient);

    if (file == null) {
      return (error: PlayerErrors.assetLoadFailed, sound: null);
    }

    return loadFile(file.absolute.path, mode: mode);
  }

  /// Load a new waveform to be played once or multiple times later
  ///
  /// [waveform]
  /// [superWave]
  /// [scale]
  /// [detune]
  /// [hash] return hash of the sound
  /// Returns [PlayerErrors.noError] if success
  Future<({PlayerErrors error, SoundProps? sound})> loadWaveform(
    WaveForm waveform,
    bool superWave,
    double scale,
    double detune,
  ) async {
    if (!isInitialized) {
      _log.severe(() => 'loadWaveform(): ${PlayerErrors.engineNotInited}');
      return (error: PlayerErrors.engineNotInited, sound: null);
    }
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.loadWaveform,
        'args': (
          waveForm: waveform.index,
          superWave: superWave,
          scale: scale,
          detune: detune,
        ),
      },
    );
    final ret = (await _waitForEvent(
      MessageEvents.loadWaveform,
      (
        waveForm: waveform.index,
        superWave: superWave,
        scale: scale,
        detune: detune,
      ),
    )) as ({PlayerErrors error, SoundProps? sound});
    if (ret.error == PlayerErrors.noError) {
      activeSounds.add(ret.sound!);
    }
    _logPlayerError(ret.error, from: 'loadWaveform() result');
    return (error: ret.error, sound: ret.sound);
  }

  /// Set the scale of an already loaded waveform identified by [sound]
  ///
  /// [sound] the sound of a waveform
  /// [newWaveform]
  PlayerErrors setWaveform(SoundProps sound, WaveForm newWaveform) {
    if (!isInitialized) {
      _log.severe(() => 'setWaveform(): ${PlayerErrors.engineNotInited}');
      return PlayerErrors.engineNotInited;
    }
    SoLoudController().soLoudFFI.setWaveform(sound.soundHash, newWaveform);
    return PlayerErrors.noError;
  }

  /// Set the scale of an already loaded waveform identified by [sound]
  ///
  /// [sound] the sound of a waveform
  /// [newScale]
  PlayerErrors setWaveformScale(SoundProps sound, double newScale) {
    if (!isInitialized) {
      _log.severe(() => 'setWaveformScale(): ${PlayerErrors.engineNotInited}');
      return PlayerErrors.engineNotInited;
    }
    SoLoudController().soLoudFFI.setWaveformScale(sound.soundHash, newScale);
    return PlayerErrors.noError;
  }

  /// Set the scale of an already loaded waveform identified by [sound]
  ///
  /// [sound] the sound of a waveform
  /// [newDetune]
  PlayerErrors setWaveformDetune(SoundProps sound, double newDetune) {
    if (!isInitialized) {
      _log.severe(() => 'setWaveformDetune(): ${PlayerErrors.engineNotInited}');
      return PlayerErrors.engineNotInited;
    }
    SoLoudController().soLoudFFI.setWaveformDetune(sound.soundHash, newDetune);
    return PlayerErrors.noError;
  }

  /// Set a new frequency of an already loaded waveform identified by [sound]
  ///
  /// [sound] the sound of a waveform
  /// [newFreq]
  PlayerErrors setWaveformFreq(SoundProps sound, double newFreq) {
    if (!isInitialized) {
      _log.severe(() => 'setWaveformFreq(): ${PlayerErrors.engineNotInited}');
      return PlayerErrors.engineNotInited;
    }
    SoLoudController().soLoudFFI.setWaveformFreq(sound.soundHash, newFreq);
    return PlayerErrors.noError;
  }

  /// Set a new frequency of an already loaded waveform identified by [sound]
  ///
  /// [sound] the sound of a waveform
  /// [superwave]
  PlayerErrors setWaveformSuperWave(SoundProps sound, bool superwave) {
    if (!isInitialized) {
      _log.severe(
          () => 'setWaveformSuperWave(): ${PlayerErrors.engineNotInited}');
      return PlayerErrors.engineNotInited;
    }
    SoLoudController().soLoudFFI.setWaveformSuperWave(
          sound.soundHash,
          superwave ? 1 : 0,
        );
    return PlayerErrors.noError;
  }

  /// Speech the given text
  ///
  /// [textToSpeech] the text to be spoken
  /// Returns PlayerErrors.noError if success and a new sound
  ///
  Future<({PlayerErrors error, SoundProps sound})> speechText(
    String textToSpeech,
  ) async {
    if (!isInitialized) {
      _log.severe(() => 'speechText(): ${PlayerErrors.engineNotInited}');
      return (
        error: PlayerErrors.engineNotInited,
        sound: SoundProps(SoundHash.invalid()),
      );
    }
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.speechText,
        'args': (textToSpeech: textToSpeech),
      },
    );
    final ret = (await _waitForEvent(
      MessageEvents.speechText,
      (textToSpeech: textToSpeech),
    )) as ({PlayerErrors error, SoundProps sound});
    _logPlayerError(ret.error, from: 'speechText() result');
    if (ret.error == PlayerErrors.noError) {
      activeSounds.add(ret.sound);
    }
    return (error: ret.error, sound: activeSounds.last);
  }

  /// Play already loaded sound identified by [sound]
  ///
  /// [sound] the sound to play
  /// [volume] 1.0f full volume
  /// [pan] 0.0f centered
  /// [paused] 0 not pause
  /// Returns PlayerErrors.noError if success, the new sound and
  /// the new handle newHandle
  ///
  Future<({PlayerErrors error, SoundProps sound, SoundHandle newHandle})> play(
    SoundProps sound, {
    double volume = 1,
    double pan = 0,
    bool paused = false,
    bool looping = false,
    double loopingStartAt = 0,
  }) async {
    if (!isInitialized) {
      _log.severe(() => 'play(): ${PlayerErrors.engineNotInited}');
      return (
        error: PlayerErrors.engineNotInited,
        sound: sound,
        newHandle: SoundHandle.error(),
      );
    }
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.play,
        'args': (
          soundHash: sound.soundHash,
          volume: volume,
          pan: pan,
          paused: paused,
          looping: looping,
          loopingStartAt: loopingStartAt,
        ),
      },
    );
    final ret = (await _waitForEvent(
      MessageEvents.play,
      (
        soundHash: sound.soundHash,
        volume: volume,
        pan: pan,
        paused: paused,
        looping: looping,
        loopingStartAt: loopingStartAt,
      ),
    )) as ({PlayerErrors error, SoundHandle newHandle});
    _logPlayerError(ret.error, from: 'play()');
    if (ret.error != PlayerErrors.noError) {
      return (
        error: ret.error,
        sound: sound,
        newHandle: SoundHandle.error(),
      );
    }

    try {
      /// add the new handle to the sound
      activeSounds
          .firstWhere((s) => s.soundHash == sound.soundHash)
          .handlesInternal
          .add(ret.newHandle);
      sound.handlesInternal.add(ret.newHandle);
    } catch (e) {
      _log.severe('play(): soundHash ${sound.soundHash} not found', e);
      return (
        error: PlayerErrors.soundHashNotFound,
        sound: sound,
        newHandle: SoundHandle.error(),
      );
    }
    return (
      error: PlayerErrors.noError,
      sound: sound,
      newHandle: ret.newHandle
    );
  }

  /// Pause or unpause already loaded sound identified by [handle]
  ///
  /// [handle] the sound handle
  /// Returns [PlayerErrors.noError] if success
  ///
  PlayerErrors pauseSwitch(SoundHandle handle) {
    if (!isInitialized) {
      _log.severe(() => 'pauseSwitch(): ${PlayerErrors.engineNotInited}');
      return PlayerErrors.engineNotInited;
    }
    SoLoudController().soLoudFFI.pauseSwitch(handle);
    return PlayerErrors.noError;
  }

  /// Pause or unpause already loaded sound identified by [handle]
  ///
  /// [handle] the sound handle
  /// Returns [PlayerErrors.noError] if success
  ///
  PlayerErrors setPause(SoundHandle handle, bool pause) {
    if (!isInitialized) {
      _log.severe(() => 'setPause(): ${PlayerErrors.engineNotInited}');
      return PlayerErrors.engineNotInited;
    }
    SoLoudController().soLoudFFI.setPause(handle, pause ? 1 : 0);
    return PlayerErrors.noError;
  }

  /// Gets the pause state
  ///
  /// [handle] the sound handle
  /// Return [PlayerErrors.noError] on success and true if paused
  ///
  ({PlayerErrors error, bool pause}) getPause(SoundHandle handle) {
    if (!isInitialized) {
      _log.severe(() => 'getPause(): ${PlayerErrors.engineNotInited}');
      return (error: PlayerErrors.engineNotInited, pause: false);
    }
    final ret = SoLoudController().soLoudFFI.getPause(handle);
    return (error: PlayerErrors.noError, pause: ret);
  }

  /// Set a sound's relative play speed.
  /// Setting the value to 0 will cause undefined behavior, likely a crash.
  /// Change the relative play speed of a sample. This changes the effective
  /// sample rate while leaving the base sample rate alone.
  ///
  /// Note that playing a sound at a higher sample rate will require SoLoud
  /// to request more samples from the sound source, which will require more
  /// memory and more processing power. Playing at a slower sample
  /// rate is cheaper.
  ///
  /// [handle] the sound handle
  /// [speed] the new speed
  PlayerErrors setRelativePlaySpeed(SoundHandle handle, double speed) {
    if (!isInitialized) {
      _log.severe(
          () => 'setRelativePlaySpeed(): ${PlayerErrors.engineNotInited}');
      return PlayerErrors.engineNotInited;
    }
    SoLoudController().soLoudFFI.setRelativePlaySpeed(handle, speed);
    return PlayerErrors.noError;
  }

  /// Return the current play speed.
  ///
  /// [handle] the sound handle
  ({PlayerErrors error, double speed}) getRelativePlaySpeed(
      SoundHandle handle) {
    if (!isInitialized) {
      _log.severe(
          () => 'getRelativePlaySpeed(): ${PlayerErrors.engineNotInited}');
      return (error: PlayerErrors.engineNotInited, speed: 1);
    }
    final ret = SoLoudController().soLoudFFI.getRelativePlaySpeed(handle);
    return (error: PlayerErrors.noError, speed: ret);
  }

  /// Stop already loaded sound identified by [handle] and clear it from the
  /// sound handle list
  ///
  /// [handle] the sound handle to stop
  /// Return [PlayerErrors.noError] on success
  ///
  Future<PlayerErrors> stop(SoundHandle handle) async {
    if (!isInitialized) {
      _log.severe(() => 'stop(): ${PlayerErrors.engineNotInited}');
      return PlayerErrors.engineNotInited;
    }
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.stop,
        'args': (handle: handle),
      },
    );
    await _waitForEvent(MessageEvents.stop, (handle: handle));

    /// find a sound with this handle and remove that handle from the list
    for (final sound in activeSounds) {
      sound.handlesInternal.removeWhere((element) => element == handle);
    }
    return PlayerErrors.noError;
  }

  /// Stop all handles of the already loaded sound identified
  /// by soundHash of [sound] and dispose it
  ///
  /// [sound] the sound to clear
  /// Return [PlayerErrors.noError] on success
  ///
  Future<PlayerErrors> disposeSound(SoundProps sound) async {
    if (!isInitialized) {
      _log.severe(() => 'disposeSound(): ${PlayerErrors.engineNotInited}');
      return PlayerErrors.engineNotInited;
    }
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.disposeSound,
        'args': (soundHash: sound.soundHash),
      },
    );
    await _waitForEvent(
        MessageEvents.disposeSound, (soundHash: sound.soundHash));

    /// remove the sound with [soundHash]
    activeSounds.removeWhere(
      (element) {
        return element.soundHash == sound.soundHash;
      },
    );
    return PlayerErrors.noError;
  }

  /// Disposes all sounds already loaded. Complete silence.
  ///
  /// No need to call this method when shutting down the engine.
  /// (It is automatically called from within [shutdown].)
  ///
  /// Return [PlayerErrors.noError] on success
  ///
  Future<PlayerErrors> disposeAllSound() async {
    if (!_isEngineInitialized) {
      _log.severe(() => 'disposeAllSound(): ${PlayerErrors.engineNotInited}');
      return PlayerErrors.engineNotInited;
    }
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.disposeAllSound,
        'args': (),
      },
    );
    await _waitForEvent(MessageEvents.disposeAllSound, ());

    /// remove the sound with [soundHash]
    activeSounds.clear();
    return PlayerErrors.noError;
  }

  /// Query whether a sound is set to loop.
  ///
  /// [handle]
  /// Returns true if flagged for looping.
  ///
  ({PlayerErrors error, bool isLooping}) getLooping(SoundHandle handle) {
    if (!isInitialized) {
      _log.severe(() => 'getLooping(): ${PlayerErrors.engineNotInited}');
      return (error: PlayerErrors.engineNotInited, isLooping: false);
    }
    final ret = SoLoudController().soLoudFFI.getLooping(handle.id);
    return (error: PlayerErrors.noError, isLooping: ret);
  }

  /// This function can be used to set a sample to play on repeat,
  /// instead of just playing once
  ///
  /// [handle] the handle for which enable or disable the loop
  /// [enable]
  /// Return [PlayerErrors.noError] on success
  ///
  PlayerErrors setLooping(SoundHandle handle, bool enable) {
    if (!isInitialized) {
      _log.severe(() => 'setLooping(): ${PlayerErrors.engineNotInited}');
      return PlayerErrors.engineNotInited;
    }
    SoLoudController().soLoudFFI.setLooping(handle, enable);
    return PlayerErrors.noError;
  }

  /// Get sound loop point value.
  ///
  /// [handle]
  /// Returns the time in seconds.
  ///
  ({PlayerErrors error, double time}) getLoopPoint(SoundHandle handle) {
    if (!isInitialized) {
      _log.severe('getLoopPoint(): ${PlayerErrors.engineNotInited}');
      return (error: PlayerErrors.engineNotInited, time: 0.0);
    }
    final time = SoLoudController().soLoudFFI.getLoopPoint(handle.id);
    return (error: PlayerErrors.noError, time: time);
  }

  /// Set sound loop point value.
  ///
  /// [handle]
  /// [time] in seconds.
  /// Return [PlayerErrors.noError] on success
  ///
  PlayerErrors setLoopPoint(SoundHandle handle, double time) {
    if (!isInitialized) {
      _log.severe('setLoopPoint(): ${PlayerErrors.engineNotInited}');
      return PlayerErrors.engineNotInited;
    }
    SoLoudController().soLoudFFI.setLoopPoint(handle, time);
    return PlayerErrors.noError;
  }

  /// Enable or disable visualization.
  /// When enabled it will be possible to get FFT and wave data.
  ///
  /// [enabled]
  /// Return [PlayerErrors.noError] on success
  ///
  PlayerErrors setVisualizationEnabled(bool enabled) {
    if (!isInitialized) {
      _log.severe('setVisualizationEnabled(): ${PlayerErrors.engineNotInited}');
      return PlayerErrors.engineNotInited;
    }
    SoLoudController().soLoudFFI.setVisualizationEnabled(enabled);
    _isVisualizationEnabled = enabled;
    return PlayerErrors.noError;
  }

  /// Get visualization state
  ///
  /// Return PlayerErrors.noError if success and true if enabled
  ({PlayerErrors error, bool isEnabled}) getVisualizationEnabled() {
    if (!isInitialized) {
      _log.severe('setVisualizationEnabled(): ${PlayerErrors.engineNotInited}');
      return (error: PlayerErrors.engineNotInited, isEnabled: false);
    }
    return (
      error: PlayerErrors.engineNotInited,
      isEnabled: SoLoudController().soLoudFFI.getVisualizationEnabled(),
    );
  }

  /// Get the sound length in seconds
  ///
  /// [sound] the sound hash to get the length
  /// Return PlayerErrors.noError if success and sound length in seconds
  ///
  ({PlayerErrors error, double length}) getLength(SoundProps sound) {
    if (!isInitialized) {
      _log.severe(() => 'getLength(): ${PlayerErrors.engineNotInited}');
      return (error: PlayerErrors.engineNotInited, length: 0.0);
    }
    final ret = SoLoudController().soLoudFFI.getLength(sound.soundHash);
    return (error: PlayerErrors.noError, length: ret);
  }

  /// Seek playing in [time] seconds
  ///
  /// [time] the time to seek
  /// [handle] the sound handle
  /// Returns [PlayerErrors.noError] if success
  ///
  /// NOTE: when seeking an MP3 file loaded using `mode`=`LoadMode.disk` the
  /// seek operation is performed but there will be delays. This occurs because
  /// the MP3 codec must compute each frame length to gain a new position.
  /// The problem is explained in souloud_wavstream.cpp
  /// in `WavStreamInstance::seek` function.
  ///
  /// This mode is useful ie for background music, not for a music player
  /// where a seek slider for MP3s is a must.
  /// If you need to seek MP3s without lags, please, use
  /// `mode`=`LoadMode.memory` instead or other supported audio formats!
  ///
  PlayerErrors seek(SoundHandle handle, double time) {
    if (!isInitialized) {
      _log.severe(() => 'seek(): ${PlayerErrors.engineNotInited}');
      return PlayerErrors.engineNotInited;
    }
    final ret = SoLoudController().soLoudFFI.seek(handle, time);
    return PlayerErrors.values[ret];
  }

  /// Get current sound position in seconds
  ///
  /// [handle] the sound handle
  /// Return PlayerErrors.noError if success and position in seconds
  ///
  ({PlayerErrors error, double position}) getPosition(SoundHandle handle) {
    if (!isInitialized) {
      _log.severe(() => 'getPosition(): ${PlayerErrors.engineNotInited}');
      return (error: PlayerErrors.engineNotInited, position: 0.0);
    }
    final ret = SoLoudController().soLoudFFI.getPosition(handle);
    return (error: PlayerErrors.noError, position: ret);
  }

  /// Get current Global volume
  ///
  /// Return PlayerErrors.noError if success and volume
  ///
  ({PlayerErrors error, double volume}) getGlobalVolume() {
    if (!isInitialized) {
      _log.severe(() => 'getGlobalVolume(): ${PlayerErrors.engineNotInited}');
      return (error: PlayerErrors.engineNotInited, volume: 0.0);
    }
    final ret = SoLoudController().soLoudFFI.getGlobalVolume();
    return (error: PlayerErrors.noError, volume: ret);
  }

  /// Get current Global volume
  ///
  /// Return PlayerErrors.noError if success
  ///
  PlayerErrors setGlobalVolume(double volume) {
    if (!isInitialized) {
      _log.severe(() => 'setGlobalVolume(): ${PlayerErrors.engineNotInited}');
      return PlayerErrors.engineNotInited;
    }
    final ret = SoLoudController().soLoudFFI.setGlobalVolume(volume);
    return PlayerErrors.values[ret];
  }

  /// Get current [handle] volume
  ///
  /// Return PlayerErrors.noError if success and volume
  ///
  ({PlayerErrors error, double volume}) getVolume(SoundHandle handle) {
    if (!isInitialized) {
      _log.severe(() => 'getVolume(): ${PlayerErrors.engineNotInited}');
      return (error: PlayerErrors.engineNotInited, volume: 0.0);
    }
    final ret = SoLoudController().soLoudFFI.getVolume(handle);
    return (error: PlayerErrors.noError, volume: ret);
  }

  /// Set [handle] volume
  ///
  /// Return PlayerErrors.noError if success
  ///
  PlayerErrors setVolume(SoundHandle handle, double volume) {
    if (!isInitialized) {
      _log.severe(() => 'setVolume(): ${PlayerErrors.engineNotInited}');
      return PlayerErrors.engineNotInited;
    }
    final ret = SoLoudController().soLoudFFI.setVolume(handle, volume);
    return PlayerErrors.values[ret];
  }

  /// Check if a handle is still valid
  ///
  /// [handle] handle to check
  /// Return PlayerErrors.noError if success and isvalid==true if valid
  ///
  ({PlayerErrors error, bool isValid}) getIsValidVoiceHandle(
      SoundHandle handle) {
    if (!isInitialized) {
      _log.severe(
          () => 'getIsValidVoiceHandle(): ${PlayerErrors.engineNotInited}');
      return (error: PlayerErrors.engineNotInited, isValid: false);
    }
    final ret = SoLoudController().soLoudFFI.getIsValidVoiceHandle(handle);
    return (error: PlayerErrors.noError, isValid: ret);
  }

  /// Return a floats matrix of 256x512
  /// Every row are composed of 256 FFT values plus 256 of wave data.
  /// Every time is called, a new row is stored in the
  /// first row and all the previous rows are shifted
  /// up. The last one will be lost.
  ///
  /// [audioData]
  /// Return [PlayerErrors.noError] if success
  ///
  PlayerErrors getAudioTexture2D(
      ffi.Pointer<ffi.Pointer<ffi.Float>> audioData) {
    if (!isInitialized || audioData == ffi.nullptr) {
      _log.severe(() => 'getAudioTexture2D(): ${PlayerErrors.engineNotInited}');
      return PlayerErrors.engineNotInited;
    }
    if (!_isVisualizationEnabled) {
      return PlayerErrors.visualizationNotEnabled;
    }
    final ret = SoLoudController().soLoudFFI.getAudioTexture2D(audioData);
    _logPlayerError(ret, from: 'getAudioTexture2D() result');
    if (ret != PlayerErrors.noError) {
      return ret;
    }
    if (audioData.value == ffi.nullptr) {
      return PlayerErrors.nullPointer;
    }
    return PlayerErrors.noError;
  }

  /// Smooth FFT data.
  /// When new data is read and the values are decreasing, the new value
  /// will be decreased with an amplitude between the old and the new value.
  /// This will resul on a less shaky visualization.
  ///
  /// [smooth] must be in the [0.0 ~ 1.0] range.
  /// 0 = no smooth
  /// 1 = full smooth
  /// the new value is calculated with:
  /// newFreq = smooth * oldFreq + (1 - smooth) * newFreq
  /// Return [PlayerErrors.noError] if success
  ///
  PlayerErrors setFftSmoothing(double smooth) {
    if (!isInitialized) {
      _log.severe(() => 'setFftSmoothing(): ${PlayerErrors.engineNotInited}');
      return PlayerErrors.engineNotInited;
    }
    SoLoudController().soLoudFFI.setFftSmoothing(smooth);
    return PlayerErrors.noError;
  }

  /////////////////////////////////////////
  /// faders
  /////////////////////////////////////////

  /// Smoothly change the global volume over specified time.
  ///
  PlayerErrors fadeGlobalVolume(double to, double time) {
    if (!isInitialized) {
      _log.severe(() => 'fadeGlobalVolume(): ${PlayerErrors.engineNotInited}');
      return PlayerErrors.engineNotInited;
    }
    final ret = SoLoudController().soLoudFFI.fadeGlobalVolume(to, time);
    return PlayerErrors.values[ret];
  }

  /// Smoothly change a channel's volume over specified time.
  ///
  PlayerErrors fadeVolume(SoundHandle handle, double to, double time) {
    if (!isInitialized) {
      _log.severe(() => 'fadeVolume(): ${PlayerErrors.engineNotInited}');
      return PlayerErrors.engineNotInited;
    }
    final ret = SoLoudController().soLoudFFI.fadeVolume(handle, to, time);
    return PlayerErrors.values[ret];
  }

  /// Smoothly change a channel's pan setting over specified time.
  ///
  PlayerErrors fadePan(SoundHandle handle, double to, double time) {
    if (!isInitialized) {
      _log.severe(() => 'fadePan(): ${PlayerErrors.engineNotInited}');
      return PlayerErrors.engineNotInited;
    }
    final ret = SoLoudController().soLoudFFI.fadePan(handle, to, time);
    return PlayerErrors.values[ret];
  }

  /// Smoothly change a channel's relative play speed over specified time.
  ///
  PlayerErrors fadeRelativePlaySpeed(
      SoundHandle handle, double to, double time) {
    if (!isInitialized) {
      _log.severe(
          () => 'fadeRelativePlaySpeed(): ${PlayerErrors.engineNotInited}');
      return PlayerErrors.engineNotInited;
    }
    final ret =
        SoLoudController().soLoudFFI.fadeRelativePlaySpeed(handle, to, time);
    return PlayerErrors.values[ret];
  }

  /// After specified time, pause the channel.
  ///
  PlayerErrors schedulePause(SoundHandle handle, double time) {
    if (!isInitialized) {
      _log.severe(() => 'schedulePause(): ${PlayerErrors.engineNotInited}');
      return PlayerErrors.engineNotInited;
    }
    final ret = SoLoudController().soLoudFFI.schedulePause(handle, time);
    return PlayerErrors.values[ret];
  }

  /// After specified time, stop the channel.
  ///
  PlayerErrors scheduleStop(SoundHandle handle, double time) {
    if (!isInitialized) {
      _log.severe(() => 'scheduleStop(): ${PlayerErrors.engineNotInited}');
      return PlayerErrors.engineNotInited;
    }
    final ret = SoLoudController().soLoudFFI.scheduleStop(handle, time);
    return PlayerErrors.values[ret];
  }

  /// Set fader to oscillate the volume at specified frequency.
  ///
  PlayerErrors oscillateVolume(
      SoundHandle handle, double from, double to, double time) {
    if (!isInitialized) {
      _log.severe(() => 'oscillateVolume(): ${PlayerErrors.engineNotInited}');
      return PlayerErrors.engineNotInited;
    }
    final ret =
        SoLoudController().soLoudFFI.oscillateVolume(handle, from, to, time);
    return PlayerErrors.values[ret];
  }

  /// Set fader to oscillate the panning at specified frequency.
  ///
  PlayerErrors oscillatePan(
      SoundHandle handle, double from, double to, double time) {
    if (!isInitialized) {
      _log.severe(() => 'oscillatePan(): ${PlayerErrors.engineNotInited}');
      return PlayerErrors.engineNotInited;
    }
    final ret =
        SoLoudController().soLoudFFI.oscillatePan(handle, from, to, time);
    return PlayerErrors.values[ret];
  }

  /// Set fader to oscillate the relative play speed at specified frequency.
  ///
  PlayerErrors oscillateRelativePlaySpeed(
      SoundHandle handle, double from, double to, double time) {
    if (!isInitialized) {
      _log.severe('oscillateRelativePlaySpeed(): '
          '${PlayerErrors.engineNotInited}');
      return PlayerErrors.engineNotInited;
    }
    final ret = SoLoudController()
        .soLoudFFI
        .oscillateRelativePlaySpeed(handle, from, to, time);
    return PlayerErrors.values[ret];
  }

  /// Set fader to oscillate the global volume at specified frequency.
  ///
  PlayerErrors oscillateGlobalVolume(double from, double to, double time) {
    if (!isInitialized) {
      _log.severe(
          () => 'oscillateGlobalVolume(): ${PlayerErrors.engineNotInited}');
      return PlayerErrors.engineNotInited;
    }
    final ret =
        SoLoudController().soLoudFFI.oscillateGlobalVolume(from, to, time);
    return PlayerErrors.values[ret];
  }

  // ////////////////////////////////////////////////
  // Below all the methods implemented with FFI for the capture
  // ////////////////////////////////////////////////

  /// List available input devices. Useful on desktop to choose
  /// which input device to use.
  @Deprecated('Use SoLoudCapture.listCaptureDevices instead '
      '(all capture-related methods were moved to SoLoudCapture class)')
  List<CaptureDevice> listCaptureDevices() =>
      SoLoudCapture.instance.listCaptureDevices();

  /// Initialize input device with [deviceID].
  ///
  /// Return [CaptureErrors.captureNoError] if no error.
  ///
  @Deprecated('Use SoLoudCapture.initialize() instead '
      '(all capture-related methods were moved to SoLoudCapture class)')
  CaptureErrors initCapture({int deviceID = -1}) =>
      SoLoudCapture.instance.initialize();

  /// Get the status of the device.
  ///
  @Deprecated('Use SoLoudCapture.isCaptureInitialized() instead '
      '(all capture-related methods were moved to SoLoudCapture class)')
  bool isCaptureInitialized() => SoLoudCapture.instance.isCaptureInitialized();

  /// Returns true if the device is capturing audio.
  ///
  @Deprecated('Use SoLoudCapture.isCaptureStarted() instead '
      '(all capture-related methods were moved to SoLoudCapture class)')
  bool isCaptureStarted() => SoLoudCapture.instance.isCaptureStarted();

  /// Stop and deinit capture device.
  ///
  /// Return [CaptureErrors.captureNoError] if no error.
  ///
  @Deprecated('Use SoLoudCapture.stopCapture() instead '
      '(all capture-related methods were moved to SoLoudCapture class)')
  CaptureErrors stopCapture() => SoLoudCapture.instance.stopCapture();

  /// Start capturing audio data.
  ///
  /// Return [CaptureErrors.captureNoError] if no error
  ///
  @Deprecated('Use SoLoudCapture.startCapture() instead '
      '(all capture-related methods were moved to SoLoudCapture class)')
  CaptureErrors startCapture() => SoLoudCapture.instance.startCapture();

  /// Return a floats matrix of 256x512
  /// Every row are composed of 256 FFT values plus 256 of wave data
  /// Every time is called, a new row is stored in the
  /// first row and all the previous rows are shifted
  /// up (the last one will be lost).
  ///
  /// Return [CaptureErrors.captureNoError] if no error.
  ///
  @Deprecated('Use SoLoudCapture.getCaptureAudioTexture2D() instead '
      '(all capture-related methods were moved to SoLoudCapture class)')
  CaptureErrors getCaptureAudioTexture2D(
          ffi.Pointer<ffi.Pointer<ffi.Float>> audioData) =>
      SoLoudCapture.instance.getCaptureAudioTexture2D(audioData);

  /// Smooth FFT data.
  ///
  /// When new data is read and the values are decreasing, the new value will be
  /// decreased with an amplitude between the old and the new value.
  /// This will resul on a less shaky visualization.
  /// [smooth] must be in the [0.0 ~ 1.0] range.
  /// 0 = no smooth
  /// 1 = full smooth
  /// the new value is calculated with:
  /// newFreq = smooth * oldFreq + (1 - smooth) * newFreq
  ///
  /// Return [CaptureErrors.captureNoError] if no error.
  ///
  @Deprecated('Use SoLoudCapture.setCaptureFftSmoothing() instead '
      '(all capture-related methods were moved to SoLoudCapture class)')
  CaptureErrors setCaptureFftSmoothing(double smooth) =>
      SoLoudCapture.instance.setCaptureFftSmoothing(smooth);

  /////////////////////////////////////////
  /// Filters
  /////////////////////////////////////////

  /// Check if the given filter is active or not.
  ///
  /// [filterType] filter to check
  /// Returns [PlayerErrors.noError] if no errors and the index of
  /// the given filter (-1 if the filter is not active)
  ///
  ({PlayerErrors error, int index}) isFilterActive(FilterType filterType) {
    final ret = SoLoudController().soLoudFFI.isFilterActive(filterType.index);
    return ret;
  }

  /// Get parameters names of the given filter.
  ///
  /// [filterType] filter to get param names
  /// Returns [PlayerErrors.noError] if no errors and the list of param names
  ///
  ({PlayerErrors error, List<String> names}) getFilterParamNames(
      FilterType filterType) {
    final ret =
        SoLoudController().soLoudFFI.getFilterParamNames(filterType.index);
    _logPlayerError(ret.error, from: 'getFilterParamNames() result');
    return ret;
  }

  /// Add the filter [filterType].
  ///
  /// [filterType] filter to add
  /// Returns [PlayerErrors.noError] if no errors
  ///
  PlayerErrors addGlobalFilter(FilterType filterType) {
    final ret = SoLoudController().soLoudFFI.addGlobalFilter(filterType.index);
    final error = PlayerErrors.values[ret];
    _logPlayerError(error, from: 'addGlobalFilter() result');
    return error;
  }

  /// Remove the filter [filterType].
  ///
  /// [filterType] filter to remove
  /// Returns [PlayerErrors.noError] if no errors
  ///
  PlayerErrors removeGlobalFilter(FilterType filterType) {
    final ret =
        SoLoudController().soLoudFFI.removeGlobalFilter(filterType.index);
    final error = PlayerErrors.values[ret];
    _logPlayerError(error, from: 'removeGlobalFilter() result');
    return error;
  }

  /// Set the effect parameter with id [attributeId]
  /// of [filterType] with [value] value.
  ///
  /// [filterType] filter to modify a param
  /// Returns [PlayerErrors.noError] if no errors
  ///
  PlayerErrors setFxParams(
      FilterType filterType, int attributeId, double value) {
    final ret = SoLoudController()
        .soLoudFFI
        .setFxParams(filterType.index, attributeId, value);
    final error = PlayerErrors.values[ret];
    _logPlayerError(error, from: 'setFxParams() result');
    return error;
  }

  /// Get the effect parameter with id [attributeId] of [filterType].
  ///
  /// [filterType] filter to modify a param
  /// Returns the value of param
  ///
  double getFxParams(FilterType filterType, int attributeId) {
    final ret =
        SoLoudController().soLoudFFI.getFxParams(filterType.index, attributeId);
    return ret;
  }

  // ////////////////////////////////////////////////
  // Below all the methods implemented with FFI for the 3D audio
  // more info: https://solhsa.com/soloud/core3d.html
  //
  // coordinate system is right handed
  //           Y
  //           ^
  //           |
  //           |
  //           |
  //           --------> X
  //          /
  //         /
  //        Z
  // ////////////////////////////////////////////////

  /// play3d() is the 3d version of the play() call.
  /// The listener position is (0, 0, 0).
  ///
  /// [posX], [posY], [posZ] are the audio source position coordinates.
  /// [velX], [velY], [velZ] are the audio source velocity.
  /// [volume]
  /// Return the error if any and a new [newHandle] of this sound
  ///
  Future<({PlayerErrors error, SoundProps sound, SoundHandle newHandle})>
      play3d(
    SoundProps sound,
    double posX,
    double posY,
    double posZ, {
    double velX = 0,
    double velY = 0,
    double velZ = 0,
    double volume = 1,
    bool paused = false,
    bool looping = false,
    double loopingStartAt = 0,
  }) async {
    if (!isInitialized) {
      _log.severe(() => 'play3d(): ${PlayerErrors.engineNotInited}');
      return (
        error: PlayerErrors.engineNotInited,
        sound: sound,
        newHandle: SoundHandle.error(),
      );
    }
    _mainToIsolateStream?.send(
      {
        'event': MessageEvents.play3d,
        'args': (
          soundHash: sound.soundHash,
          posX: posX,
          posY: posY,
          posZ: posZ,
          velX: velX,
          velY: velY,
          velZ: velZ,
          volume: volume,
          paused: paused,
          looping: looping,
          loopingStartAt: loopingStartAt,
        ),
      },
    );
    final ret = (await _waitForEvent(
      MessageEvents.play3d,
      (
        soundHash: sound.soundHash,
        posX: posX,
        posY: posY,
        posZ: posZ,
        velX: velX,
        velY: velY,
        velZ: velZ,
        volume: volume,
        paused: paused,
        looping: looping,
        loopingStartAt: loopingStartAt,
      ),
    )) as ({PlayerErrors error, SoundHandle newHandle});
    _logPlayerError(ret.error, from: 'play3d() result');
    if (ret.error != PlayerErrors.noError) {
      return (
        error: ret.error,
        sound: sound,
        newHandle: SoundHandle.error(),
      );
    }

    try {
      /// add the new handle to the sound
      activeSounds
          .firstWhere((s) => s.soundHash == sound.soundHash)
          .handlesInternal
          .add(ret.newHandle);
      sound.handlesInternal.add(ret.newHandle);
    } catch (e, s) {
      _log.severe(
          () => 'play3d(): soundHash ${sound.soundHash} not found', e, s);
      return (
        error: PlayerErrors.soundHashNotFound,
        sound: sound,
        newHandle: SoundHandle.error(),
      );
    }
    return (
      error: PlayerErrors.noError,
      sound: sound,
      newHandle: ret.newHandle
    );
  }

  /// Since SoLoud has no knowledge of the scale of your coordinates,
  /// you may need to adjust the speed of sound for these effects
  /// to work correctly. The default value is 343, which assumes
  /// that your world coordinates are in meters (where 1 unit is 1 meter),
  /// and that the environment is dry air at around 20 degrees Celsius.
  ///
  void set3dSoundSpeed(double speed) {
    SoLoudController().soLoudFFI.set3dSoundSpeed(speed);
  }

  /// Get the sound speed
  ///
  double get3dSoundSpeed() {
    return SoLoudController().soLoudFFI.get3dSoundSpeed();
  }

  /// You can set the position, at-vector, up-vector and velocity
  /// parameters of the 3d audio listener with one call.
  ///
  void set3dListenerParameters(
      double posX,
      double posY,
      double posZ,
      double atX,
      double atY,
      double atZ,
      double upX,
      double upY,
      double upZ,
      double velocityX,
      double velocityY,
      double velocityZ) {
    SoLoudController().soLoudFFI.set3dListenerParameters(posX, posY, posZ, atX,
        atY, atZ, upX, upY, upZ, velocityX, velocityY, velocityZ);
  }

  /// You can set the position parameter of the 3d audio listener.
  ///
  void set3dListenerPosition(double posX, double posY, double posZ) {
    SoLoudController().soLoudFFI.set3dListenerPosition(posX, posY, posZ);
  }

  /// You can set the "at" vector parameter of the 3d audio listener.
  ///
  void set3dListenerAt(double atX, double atY, double atZ) {
    SoLoudController().soLoudFFI.set3dListenerAt(atX, atY, atZ);
  }

  /// You can set the "up" vector parameter of the 3d audio listener.
  ///
  void set3dListenerUp(double upX, double upY, double upZ) {
    SoLoudController().soLoudFFI.set3dListenerUp(upX, upY, upZ);
  }

  /// You can set the listener's velocity vector parameter.
  ///
  void set3dListenerVelocity(
      double velocityX, double velocityY, double velocityZ) {
    SoLoudController()
        .soLoudFFI
        .set3dListenerVelocity(velocityX, velocityY, velocityZ);
  }

  /// You can set the position and velocity parameters of a live
  /// 3d audio source with one call.
  ///
  void set3dSourceParameters(SoundHandle handle, double posX, double posY,
      double posZ, double velocityX, double velocityY, double velocityZ) {
    SoLoudController().soLoudFFI.set3dSourceParameters(
        handle, posX, posY, posZ, velocityX, velocityY, velocityZ);
  }

  /// You can set the position parameters of a live 3d audio source.
  ///
  void set3dSourcePosition(
      SoundHandle handle, double posX, double posY, double posZ) {
    SoLoudController().soLoudFFI.set3dSourcePosition(handle, posX, posY, posZ);
  }

  /// You can set the velocity parameters of a live 3d audio source.
  ///
  void set3dSourceVelocity(SoundHandle handle, double velocityX,
      double velocityY, double velocityZ) {
    SoLoudController()
        .soLoudFFI
        .set3dSourceVelocity(handle, velocityX, velocityY, velocityZ);
  }

  /// You can set the minimum and maximum distance parameters
  /// of a live 3d audio source.
  ///
  void set3dSourceMinMaxDistance(
      SoundHandle handle, double minDistance, double maxDistance) {
    SoLoudController()
        .soLoudFFI
        .set3dSourceMinMaxDistance(handle, minDistance, maxDistance);
  }

  /// You can change the attenuation model and rolloff factor parameters of
  /// a live 3d audio source.
  /// ```
  /// 0 NO_ATTENUATION        No attenuation
  /// 1 INVERSE_DISTANCE      Inverse distance attenuation model
  /// 2 LINEAR_DISTANCE       Linear distance attenuation model
  /// 3 EXPONENTIAL_DISTANCE  Exponential distance attenuation model
  /// ```
  /// see https://solhsa.com/soloud/concepts3d.html
  ///
  void set3dSourceAttenuation(
    SoundHandle handle,
    int attenuationModel,
    double attenuationRolloffFactor,
  ) {
    SoLoudController().soLoudFFI.set3dSourceAttenuation(
          handle,
          attenuationModel,
          attenuationRolloffFactor,
        );
  }

  /// You can change the doppler factor of a live 3d audio source.
  ///
  void set3dSourceDopplerFactor(SoundHandle handle, double dopplerFactor) {
    SoLoudController()
        .soLoudFFI
        .set3dSourceDopplerFactor(handle, dopplerFactor);
  }

  /// Utility method that logs a [Level.SEVERE] message if [playerError]
  /// is anything other than [PlayerErrors.noError].
  ///
  /// Optionally takes a [from] string, so that it can construct messages
  /// with more context:
  ///
  /// ```dart
  /// _logIfPlayerError(result, from: 'play()');
  /// ```
  ///
  /// The code above may produce a log record such as:
  ///
  /// ```text
  /// [SoLoud] play(): PlayerError.invalidParameter
  /// ```
  void _logPlayerError(PlayerErrors playerError, {String? from}) {
    if (playerError == PlayerErrors.noError) {
      return;
    }

    if (!_log.isLoggable(Level.SEVERE)) {
      // Do not do extra work if the logger isn't listening.
      return;
    }

    final strBuf = StringBuffer();
    if (from != null) {
      strBuf.write('$from: ');
    }
    strBuf.write(playerError.toString());
    _log.severe(strBuf.toString());
  }
}
