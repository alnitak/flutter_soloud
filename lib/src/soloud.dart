// ignore_for_file: require_trailing_commas, avoid_positional_boolean_parameters

import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:flutter_soloud/src/audio_source.dart';
import 'package:flutter_soloud/src/bindings/audio_data.dart';
import 'package:flutter_soloud/src/bindings/bindings_player.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:flutter_soloud/src/exceptions/exceptions.dart';
import 'package:flutter_soloud/src/filter_params.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';
import 'package:flutter_soloud/src/utils/loader.dart';
import 'package:http/http.dart' as http;
import 'package:logging/logging.dart';
import 'package:meta/meta.dart';

/// The main class to call all the audio methods that play sounds.
///
/// This class has a singleton [instance] which represents the (also singleton)
/// instance of the SoLoud (C++) engine.
interface class SoLoud {
  /// The private constructor of [SoLoud]. This prevents developers from
  /// instantiating new instances.
  SoLoud._();

  static final Logger _log = Logger('flutter_soloud.SoLoud');

  final _controller = SoLoudController();

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

  /// A helper for loading files that aren't on disk.
  final SoLoudLoader _loader = SoLoudLoader();

  /// The backing private field for [isInitialized].
  // TODO(filip): related to `get initialized`
  // ignore: prefer_final_fields
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
  // TODO(filip): related to `get initialized`. This line below is the old one.
  bool get isInitialized => _isInitialized;
  // TODO(filip): this line below is the new one I leaved to let the
  /// plugin to work.
  // bool get isInitialized => _controller.soLoudFFI.isInited();

  /// The completer for an initialization in progress.
  ///
  /// This is `null` when the engine is not currently being initialized.
  // TODO(filip): related to `get initialized`.
  Completer<void>? _initializeCompleter;

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
  /// or when there wasn't ever a call to [init] at all (`false`).
  ///
  /// If the engine is in the middle of initializing, the future will complete
  /// when the initialization is done. It will be `true` if the initialization
  /// was successful, and `false` if it failed. The future will never throw.
  ///
  /// It is _not_ needed to await this future after a call to [init].
  /// The [init] method already returns a future, and it is the
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
  // TODO(filip): reimplement to satisfy the `Loader` timeout in the `init()`
  FutureOr<bool> get initialized {
    if (_initializeCompleter == null) {
      // We are _not_ during initialization. Return synchronously.
      return _isInitialized;
    }

    // We are in the middle of initializing the engine. Wait for that to
    // complete and return `true` if it was successful.
    return _initializeCompleter!.future
        .then((_) => true, onError: (_) => false);
  }

  /// Backing of [activeSounds].
  final List<AudioSource> _activeSounds = [];

  /// The sounds that are _currently being loaded_.
  Iterable<AudioSource> get activeSounds => _activeSounds;

  /// Completers for the [loadFile] method
  @internal
  final Map<String, Completer<AudioSource>> loadedFileCompleters = {};

  /// Completers for the [stop] method
  @internal
  final Map<SoundHandle, Completer<void>> voiceEndedCompleters = {};

  /// Initializes the audio engine.
  ///
  /// Run this before anything else, and `await` its result in a try/catch.
  /// Only when this method returns without throwing exceptions will the engine
  /// be ready.
  ///
  /// If you call any other methods (such as [play]) before initialization
  /// completes, those calls will be ignored and you will get
  /// a [SoLoudNotInitializedException] exception.
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
  /// [sampleRate] The sample rate represents the number of samples used, per
  /// second. Typical sample rates are 8000Hz, 22050Hz, 44100Hz and 48000Hz.
  /// Higher the sample rates mean clearer sound, but also bigger files, more
  /// memory and higher processing power requirements.
  /// [bufferSize] Audio latency generally means the time it takes from
  /// triggering a sound to the sound actually coming out of the speakers.
  /// The smaller the latency, the better.
  /// Unfortunately, there's always some latency. The primary source of
  /// latency (that a programmer can have any control over) is the size of
  /// audio buffer. Generally speaking, the smaller the buffer, the lower the
  /// latency, but at the same time, the smaller the buffer, the more likely the
  /// system hits buffer underruns (ie, the play head marches on but there's no
  /// data ready to be played) and the sound breaks down horribly.
  /// [channels] mono, stereo, quad, 5.1, 7.1.
  Future<void> init({
    // TODO(filip): remove deprecation?
    @Deprecated('timeout is not used anymore.')
    Duration timeout = const Duration(seconds: 10),
    bool automaticCleanup = false,
    int sampleRate = 44100,
    int bufferSize = 2048,
    Channels channels = Channels.stereo,
  }) async {
    _log.finest('init() called');

    // if `!isInitialized` but the engine is initialized in native, therefore
    // the developer may have carried out a hot reload which does not imply
    // the release of the native player.
    // Just deinit the engine to be re-inited later.
    if (isInitialized) {
      _log.warning('init() called when the native player is already '
          'initialized. This is expected after a hot restart but not '
          "otherwise. If you see this in production logs, there's probably "
          'a bug in your code. You may have neglected to deinit() SoLoud '
          'during the current lifetime of the app.');
      deinit();
    }

    _activeSounds.clear();

    // Initialize native callbacks
    _initializeNativeCallbacks();

    final error = _controller.soLoudFFI.initEngine(
      sampleRate,
      bufferSize,
      channels,
    );
    _logPlayerError(error, from: 'initialize() result');
    if (error == PlayerErrors.noError) {
      _isInitialized = true;

      /// get the visualization flag from the player on C side.
      /// Eventually we can set this as a parameter during the
      /// initialization with some other parameters like `sampleRate`
      _isVisualizationEnabled = getVisualizationEnabled();

      // Initialize [SoLoudLoader]
      _loader.automaticCleanup = automaticCleanup;

      await _loader.initialize();
    } else {
      _log.severe('initialize() failed with error: $error');
    }
  }

  /// Stops the engine and disposes of all resources, including sounds.
  ///
  /// This method is meant to be called when exiting the app. For example
  /// within the `dispose()` of the uppermost widget in the tree
  /// or inside "AppLifecycleListener.onExitRequested".
  void deinit() {
    _log.finest('deinit() called');

    _isInitialized = false;
    _controller.soLoudFFI.disposeAllSound();
    _controller.soLoudFFI.deinit();
    _activeSounds.clear();
  }

  /// Get the [AudioSource] which own the [handle]
  AudioSource? _isHandlePresent(SoundHandle handle) {
    for (final sound in _activeSounds) {
      if (sound.handlesInternal.contains(handle)) return sound;
    }
    return null;
  }

  /// Initialize native callbacks.
  /// Here, we are listening for voice handles becoming invalid
  /// when they are stopped/ended from anywhere or when a file has been loaded.
  ///
  /// Currently available:
  ///   - [SoLoudController().soLoudFFI.voiceEndedEvents]
  ///   - [SoLoudController().soLoudFFI.fileLoadedEvents]
  ///
  /// These events are coming from `FlutterSoLoudFfi`. The callbacks
  /// `_voiceEndedCallback` and `_fileLoadedCallback` are called from CPP.
  /// From within these callbacks a new stream event is added and listened here.
  // TODO(filip): 'setDartEventCallbacks()' can be called more then once,
  // please take a look at the listeners if you find a better way
  // to manage them only once.
  void _initializeNativeCallbacks() {
    // Initialize callbacks.
    _controller.soLoudFFI.setDartEventCallbacks();

    // Listen when a handle becomes invalid becaus has been stopped/ended
    if (!_controller.soLoudFFI.voiceEndedEventController.hasListener) {
      _controller.soLoudFFI.voiceEndedEvents.listen((handle) {
        // Remove this UNIQUE [handle] from the `AudioSource` that own it.

        final soundHandleFound = _isHandlePresent(SoundHandle(handle));

        if (soundHandleFound != null) {
          soundHandleFound.soundEventsController.add((
            event: SoundEventType.handleIsNoMoreValid,
            sound: soundHandleFound,
            handle: SoundHandle(handle),
          ));

          /// Remove this handle from the list
          soundHandleFound.handlesInternal.removeWhere((element) {
            _log.finest('Voice ended event received. Removing handle $handle');
            return element.id == handle;
          });

          if (soundHandleFound.handles.isEmpty) {
            // All instances of the sound have finished.
            soundHandleFound.allInstancesFinishedController.add(null);
          }
          voiceEndedCompleters[SoundHandle(handle)]?.complete();
        }
      });
    }

    // Listen when a file has been loaded
    if (!_controller.soLoudFFI.fileLoadedEventsController.hasListener) {
      _controller.soLoudFFI.fileLoadedEvents.listen((result) {
        final exists =
            loadedFileCompleters.containsKey(result['completeFileName']);
        if (exists) {
          final error = PlayerErrors.values[result['error'] as int];
          final completeFileName = result['completeFileName'] as String;
          final hash = result['hash'] as int;

          _logPlayerError(error, from: 'loadFile() result');
          final newSound = AudioSource(SoundHash(hash));
          if (error == PlayerErrors.noError) {
            _activeSounds.add(newSound);
            loadedFileCompleters[result['completeFileName']]!
                .complete(newSound);
          } else if (error == PlayerErrors.fileAlreadyLoaded) {
            _log.warning(() => "Sound '$completeFileName' was already loaded. "
                'Prefer loading only once, and reusing the loaded sound '
                'when playing.');
            final alreadyLoaded = _activeSounds
                    .where((sound) => sound.soundHash == newSound.soundHash)
                    .length ==
                1;
            assert(
                alreadyLoaded,
                'Sound is already loaded but missing from _activeSounds. '
                'This is probably a bug in flutter_soloud, please file.');
            // If we are here, the file has been already loaded but there is
            // no corrispondence int the local list of sounds. Add it to
            // the list safely because the cpp loadFile() compute the hash
            // using the file name.
            _activeSounds.add(newSound);
            loadedFileCompleters[result['completeFileName']]
                ?.complete(newSound);
          } else {
            throw SoLoudCppException.fromPlayerError(error);
          }
        }
      });
    }

    // Listen player state changes. Not doing much now.
    // This doesn't work on Android. See "ma_device_notification_proc"
    // in miniaudio.h. Only `started` and `stopped` are working.
    // Leaving this commented out for futher investigation.
    // if (!_controller.soLoudFFI.stateChangedController.hasListener) {
    //   _controller.soLoudFFI.stateChangedEvents.listen((newState) {
    //     _log.fine(() => 'Audio engine state changed: $newState');
    //   });
    // }
  }

  // ////////////////////////////////////////////////
  // Below all the methods implemented with FFI for the player
  // ////////////////////////////////////////////////

  /// Load a new sound to be played once or multiple times later, from
  /// the file system.
  ///
  /// Provide the complete [path] of the file to be played.
  ///
  /// When [mode] is [LoadMode.memory], the whole uncompressed RAW PCM
  /// audio is loaded into memory. Used to prevent gaps or lags
  /// when seeking/starting a sound (less CPU, more memory allocated).
  /// If [LoadMode.disk] is used instead, the audio data is loaded
  /// from the given file when needed (more CPU, less memory allocated).
  /// See the [seek] note problem when using [LoadMode.disk].
  /// The default is [LoadMode.memory].
  ///
  /// Returns the new sound as [AudioSource].
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  ///
  /// If the file is already loaded, this is a no-op (but a warning
  /// will be produced in the log).
  Future<AudioSource> loadFile(
    String path, {
    LoadMode mode = LoadMode.memory,
  }) async {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }

    _controller.soLoudFFI.loadFile(path, mode);

    final completer = Completer<AudioSource>();
    loadedFileCompleters.addAll({
      path: completer,
    });

    return completer.future.whenComplete(() {
      loadedFileCompleters.removeWhere((key, __) => key == path);
    });
  }

  /// Load a new sound to be played once or multiple times later, from
  /// a buffer.
  ///
  /// Provide a [path] of the file to be used as a reference to distinguis
  /// this [buffer].
  ///
  /// The [buffer] represents the bytes of a supported audio file (not
  /// RAW data).
  /// It could be also a simple WAV format sequence of manually generated bytes.
  ///
  /// When [mode] is [LoadMode.memory], the whole uncompressed RAW PCM
  /// audio is loaded into memory. Used to prevent gaps or lags
  /// when seeking/starting a sound (less CPU, more memory allocated).
  /// If [LoadMode.disk] is used instead, the audio data is loaded
  /// from the given file when needed (more CPU, less memory allocated).
  /// See the [seek] note problem when using [LoadMode.disk].
  /// The default is [LoadMode.memory].
  /// IMPORTANT: [LoadMode.memory] used the on web platform could cause UI
  /// freeze problems.
  ///
  /// This is the only choice to load a file when using this plugin on the web
  /// because browsers cannot read directly files from the loal storage.
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  Future<AudioSource> loadMem(
    String path,
    Uint8List buffer, {
    LoadMode mode = LoadMode.memory,
  }) async {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }

    final completer = Completer<AudioSource>();
    loadedFileCompleters.addAll({
      path: completer,
    });

    final ret = _controller.soLoudFFI.loadMem(path, buffer, mode);

    /// There is not a callback in cpp that is supposed to add the
    /// "load file event". Manually send this event to have only one
    /// place to do this "loaded" job.
    _controller.soLoudFFI.fileLoadedEventsController.add({
      'error': ret.error.index,
      'completeFileName': path,
      'hash': ret.soundHash.hash,
    });

    return completer.future.whenComplete(() {
      loadedFileCompleters.removeWhere((key, __) => key == path);
    });
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
  /// Throws a [SoLoudTemporaryFolderFailedException] if there was a problem
  /// creating the temporary file that the asset will be copied to.
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  ///
  /// Returns the new sound as [AudioSource].
  ///
  /// If the file is already loaded, this is a no-op (but a warning
  /// will be produced in the log).
  Future<AudioSource> loadAsset(
    String key, {
    LoadMode mode = LoadMode.memory,
    AssetBundle? assetBundle,
  }) async {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }

    final newAudioSource = await _loader.loadAsset(
      key,
      mode,
      assetBundle: assetBundle,
    );

    return newAudioSource;
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
  /// Throws [SoLoudNetworkStatusCodeException] if the request fails
  /// with a non-`200` status code.
  /// Throws a [SoLoudTemporaryFolderFailedException] if there was a problem
  /// creating the temporary file that the asset will be copied to.
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  ///
  /// Returns the new sound as [AudioSource].
  ///
  /// If the file is already loaded, this is a no-op (but a warning
  /// will be produced in the log).
  Future<AudioSource> loadUrl(
    String url, {
    LoadMode mode = LoadMode.memory,
    http.Client? httpClient,
  }) async {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }

    final newAudioSource =
        await _loader.loadUrl(url, mode, httpClient: httpClient);

    return newAudioSource;
  }

  /// Load a new waveform to be played once or multiple times later.
  ///
  /// Specify the type of the waveform (such as sine or square or saw)
  /// with [waveform].
  ///
  /// You must also specify if the waveform should be a [superWave],
  /// and what the superwave's [scale] and [detune] should be.
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  ///
  /// Returns the new sound as [AudioSource].
  Future<AudioSource> loadWaveform(
    WaveForm waveform,
    bool superWave,
    double scale,
    double detune,
  ) async {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    final ret = _controller.soLoudFFI.loadWaveform(
      waveform,
      superWave,
      scale,
      detune,
    );

    if (ret.error == PlayerErrors.noError) {
      final newSound = AudioSource(ret.soundHash);
      _activeSounds.add(newSound);
      return newSound;
    }
    _logPlayerError(ret.error, from: 'loadWaveform() result');
    throw SoLoudCppException.fromPlayerError(ret.error);
  }

  /// Set a waveform type to the given sound: see [WaveForm] enum.
  ///
  /// Provide the [sound] for which to change the waveform type,
  /// and the new [newWaveform].
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  void setWaveform(AudioSource sound, WaveForm newWaveform) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    _controller.soLoudFFI.setWaveform(sound.soundHash, newWaveform);
  }

  /// If this sound is a `superWave` you can change the scale at runtime.
  ///
  /// Provide the [sound] for which to change the scale,
  /// and the new [newScale].
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  void setWaveformScale(AudioSource sound, double newScale) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    _controller.soLoudFFI.setWaveformScale(sound.soundHash, newScale);
  }

  /// If this sound is a `superWave` you can change the detune at runtime.
  ///
  /// Provide the [sound] for which to change the detune,
  /// and the new [newDetune].
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  void setWaveformDetune(AudioSource sound, double newDetune) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    _controller.soLoudFFI.setWaveformDetune(sound.soundHash, newDetune);
  }

  /// Set the frequency of the given waveform sound.
  ///
  /// Provide the [sound] for which to change the scale,
  /// and the new [newFrequency].
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  void setWaveformFreq(AudioSource sound, double newFrequency) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    _controller.soLoudFFI.setWaveformFreq(sound.soundHash, newFrequency);
  }

  /// Set the given waveform sound's super wave flag.
  ///
  /// Provide the [sound] for which to change the flag,
  /// and the new [superwave] value.
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  void setWaveformSuperWave(AudioSource sound, bool superwave) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    _controller.soLoudFFI.setWaveformSuperWave(
      sound.soundHash,
      superwave ? 1 : 0,
    );
  }

  /// Create a new audio source from the given [textToSpeech].
  ///
  /// Returns the new sound as [AudioSource].
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  Future<AudioSource> speechText(String textToSpeech) async {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    final ret = _controller.soLoudFFI.speechText(textToSpeech);

    _logPlayerError(ret.error, from: 'speechText() result');
    if (ret.error == PlayerErrors.noError) {
      final newSound = AudioSource(SoundHash.random());
      _activeSounds.add(newSound);
      return newSound;
    }
    throw SoLoudCppException.fromPlayerError(ret.error);
  }

  /// Play an already-loaded sound identified by [sound]. Creates a new
  /// playing instance of the sound, and returns its [SoundHandle].
  ///
  /// You can provide the [volume], where `1.0` is full volume and `0.0`
  /// is silent. Defaults to `1.0`.
  ///
  /// You can provide [pan] for the sound, with `0.0` centered,
  /// `-1.0` fully left, and `1.0` fully right. Defaults to `0.0`.
  ///
  /// Set [paused] to `true` if you want the new sound instance to
  /// start paused. This is helpful if you want to change some attributes
  /// of the sound instance before you play it. For example, you could
  /// call [setRelativePlaySpeed] or [setProtectVoice] on the sound before
  /// un-pausing it.
  ///
  /// To play a looping sound, set [paused] to `true`. You can also
  /// define the region to loop by setting [loopingStartAt]
  /// (which defaults to the beginning of the sound otherwise).
  /// There is no way to set the end of the looping region — it will
  /// always be the end of the [sound].
  ///
  /// Returns the [SoundHandle] of the new sound instance.
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  Future<SoundHandle> play(
    AudioSource sound, {
    double volume = 1,
    double pan = 0,
    bool paused = false,
    bool looping = false,
    Duration loopingStartAt = Duration.zero,
  }) async {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    final ret = _controller.soLoudFFI.play(
      sound.soundHash,
      volume: volume,
      pan: pan,
      paused: paused,
      looping: looping,
      loopingStartAt: loopingStartAt,
    );
    _logPlayerError(ret.error, from: 'play()');
    if (ret.error != PlayerErrors.noError) {
      throw SoLoudCppException.fromPlayerError(ret.error);
    }

    final filtered =
        _activeSounds.where((s) => s.soundHash == sound.soundHash).toSet();
    if (filtered.isEmpty) {
      _log.severe(() => 'play(): soundHash ${sound.soundHash} not found');
      throw SoLoudSoundHashNotFoundDartException(sound.soundHash);
    }

    assert(filtered.length == 1, 'Duplicate sounds found');
    for (final activeSound in filtered) {
      activeSound.handlesInternal.add(ret.newHandle);
    }

    return ret.newHandle;
  }

  /// Pause or unpause a currently playing sound identified by [handle].
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  void pauseSwitch(SoundHandle handle) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    _controller.soLoudFFI.pauseSwitch(handle);
  }

  /// Pause or unpause a currently playing sound identified by [handle].
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  void setPause(SoundHandle handle, bool pause) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    _controller.soLoudFFI.setPause(handle, pause ? 1 : 0);
  }

  /// Gets the pause state of a currently playing sound identified by [handle].
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  bool getPause(SoundHandle handle) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    return _controller.soLoudFFI.getPause(handle);
  }

  /// Set a sound's relative play speed.
  ///
  /// Provide the currently playing sound instance via its [handle],
  /// and the new [speed].
  ///
  /// Setting the speed value to `0` will cause undefined behavior,
  /// likely a crash. The lower limit is clamped to 0.05 silently.
  ///
  /// This changes the effective sample rate
  /// while leaving the base sample rate alone.
  /// Note that playing a sound at a higher sample rate will require SoLoud
  /// to request more samples from the sound source, which will require more
  /// memory and more processing power. Playing at a slower sample
  /// rate is cheaper.
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  void setRelativePlaySpeed(SoundHandle handle, double speed) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    _controller.soLoudFFI.setRelativePlaySpeed(handle, speed);
  }

  /// Get a sound's relative play speed. Provide the sound instance via
  /// its [handle].
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  double getRelativePlaySpeed(SoundHandle handle) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    return _controller.soLoudFFI.getRelativePlaySpeed(handle);
  }

  /// Stop a currently playing sound identified by [handle]
  /// and clear it from the sound handle list.
  ///
  /// This does _not_ dispose the audio source. Use [disposeSource] for that.
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  Future<void> stop(SoundHandle handle) async {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    final completer = Completer<void>();
    voiceEndedCompleters[handle] = completer;

    _controller.soLoudFFI.stop(handle);

    return completer.future
        .timeout(const Duration(milliseconds: 300))
        .onError((e, s) {
      _log.severe('stop() takes too much time for handle $handle. '
          'This is not expected but not blocking. Worth to file a bug with '
          'a simple reproducible code.');
      voiceEndedCompleters[handle]?.complete();
    }).whenComplete(() {
      voiceEndedCompleters.removeWhere((key, __) => key == handle);
    });
  }

  /// Stops all handles of the already loaded [source], and reclaims memory.
  ///
  /// After an audio source has been disposed in this way,
  /// do not attempt to play it.
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  Future<void> disposeSource(AudioSource source) async {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    _controller.soLoudFFI.disposeSound(source.soundHash);

    if (!source.soundEventsController.isClosed) {
      source.soundEventsController.add((
        event: SoundEventType.soundDisposed,
        sound: source,
        handle: const SoundHandle.error(),
      ));
    }
    await source.soundEventsController.close();

    /// remove the sound with [soundHash]
    _activeSounds.removeWhere(
      (element) {
        return element.soundHash == source.soundHash;
      },
    );
  }

  /// Disposes all audio sources that are currently loaded.
  /// Also stops all sound instances if anything is playing.
  ///
  /// No need to call this method when shutting down the engine.
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  Future<void> disposeAllSources() async {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    _controller.soLoudFFI.disposeAllSound();

    for (final sound in _activeSounds) {
      sound.soundEventsController.add((
        event: SoundEventType.soundDisposed,
        sound: sound,
        handle: const SoundHandle.error(),
      ));
      // TODO(filiph): Close these in parallel using `Future.wait()`
      await sound.soundEventsController.close();
    }

    /// remove all sounds
    _activeSounds.clear();
  }

  /// Query whether a sound (supplied via [handle]) is set to loop.
  ///
  /// Returns `true` if the sound is flagged for looping.
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  bool getLooping(SoundHandle handle) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    return _controller.soLoudFFI.getLooping(handle);
  }

  /// Set the looping flag of a currently playing sound, provided via
  /// its [handle].
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  void setLooping(SoundHandle handle, bool enable) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    _controller.soLoudFFI.setLooping(handle, enable);
  }

  /// Get the loop point value of a currently playing sound, provided via
  /// its [handle].
  ///
  /// Returns the timestamp of the loop point as a [Duration].
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  Duration getLoopPoint(SoundHandle handle) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    return _controller.soLoudFFI.getLoopPoint(handle);
  }

  /// Set the loop point of a currently playing sound, provided via
  /// its [handle].
  ///
  /// Specify the loop point with [time] (a [Duration]).
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  void setLoopPoint(SoundHandle handle, Duration time) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    _controller.soLoudFFI.setLoopPoint(handle, time);
  }

  /// Enable or disable visualization.
  ///
  /// When enabled it will be possible to get FFT and wave data.
  ///
  /// [enabled] whether to set the visualization or not.
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  void setVisualizationEnabled(bool enabled) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    _controller.soLoudFFI.setVisualizationEnabled(enabled);
    _isVisualizationEnabled = enabled;
  }

  /// Get visualization state.
  ///
  /// Return true if enabled.
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  bool getVisualizationEnabled() {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    // ignore: join_return_with_assignment
    _isVisualizationEnabled = _controller.soLoudFFI.getVisualizationEnabled();
    return _isVisualizationEnabled;
  }

  /// Get the length of a loaded audio [source].
  ///
  /// Returns the length as a [Duration].
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  Duration getLength(AudioSource source) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    return _controller.soLoudFFI.getLength(source.soundHash);
  }

  /// Seek a currently playing sound instance, provided via its [handle].
  /// Specify the [time] (as a [Duration]) to which you want to
  /// move the play head.
  ///
  /// For example, seeking to `Duration(milliseconds: 200)` means that
  /// you want to move the play head to a point 200 milliseconds into
  /// the audio source. Seeking to [Duration.zero] means "go to the beginning
  /// of the sound".
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  ///
  /// NOTE: when seeking an MP3 file loaded using [LoadMode.disk], the
  /// seek operation is performed but there will be a delay. This occurs because
  /// the MP3 codec must compute each frame length to gain a new position.
  /// The problem is explained in `souloud_wavstream.cpp`,
  /// in the `WavStreamInstance::seek` function.
  /// Therefore, [LoadMode.disk] is useful for things like the background music,
  /// and not for things like a music player where the user
  /// expects being able to seek anywhere inside a playing track immediately.
  /// If you need to seek MP3s without lags, please, use
  /// [LoadMode.memory] instead, or use other supported audio formats.
  void seek(SoundHandle handle, Duration time) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    final ret = _controller.soLoudFFI.seek(handle, time);
    final error = PlayerErrors.values[ret];
    if (error != PlayerErrors.noError) {
      _log.severe(() => 'seek(): $error');
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  /// Get the current sound position of a sound instance (provided via its
  /// [handle]).
  ///
  /// Returns the position as a [Duration]. For example,
  /// `Duration(milliseconds: 200)` means that the play head is currently
  /// 200 milliseconds into the audio source.
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  Duration getPosition(SoundHandle handle) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    return _controller.soLoudFFI.getPosition(handle);
  }

  /// Gets the current global volume.
  ///
  /// Return the volume as a [double], with `0.0` meaning silence
  /// and `1.0` meaning full volume.
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  ///
  /// Note that if you `setGlobalVolume()` to `0.8` and then
  /// `getGlobalVolume()`, you might get a slightly different number,
  /// such as `0.800000042353`.
  /// This is expected since the internal audio engine uses float
  /// instead of double, and so there are rounding errors.
  double getGlobalVolume() {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    return _controller.soLoudFFI.getGlobalVolume();
  }

  /// Sets the global volume which affects all sounds.
  ///
  /// The value of [volume] can range from `0.0` (meaning everything is muted)
  /// to `1.0` (meaning full volume).
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  ///
  /// Note that if you `setGlobalVolume()` to `0.8` and then
  /// `getGlobalVolume()`, you might get a slightly different number,
  /// such as `0.800000042353`.
  /// This is expected since the internal audio engine uses float
  /// instead of double, and so there are rounding errors.
  void setGlobalVolume(double volume) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    final ret = _controller.soLoudFFI.setGlobalVolume(volume);
    final error = PlayerErrors.values[ret];
    if (error != PlayerErrors.noError) {
      _log.severe(() => 'setGlobalVolume(): $error');
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  /// Get the volume of the currently playing sound instance, provided
  /// via its [handle].
  ///
  /// Returns the volume as a [double], where `0.0` means the sound is muted,
  /// and `1.0` means its playing at full volume.
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  ///
  /// Note that if you `setVolume()` to `0.8` and then `getVolume()`, you might
  /// get a slightly different number, such as `0.800000042353`.
  /// This is expected since the internal audio engine uses float
  /// instead of double, and so there are rounding errors.
  double getVolume(SoundHandle handle) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    return _controller.soLoudFFI.getVolume(handle);
  }

  /// Set the volume for a currently playing sound instance, provided
  /// via its [handle].
  ///
  /// The value of [volume] can range from `0.0` (meaning the sound is muted)
  /// to `1.0` (meaning it should play at full volume).
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  ///
  /// Note that if you `setVolume()` to `0.8` and then `getVolume()`, you might
  /// get a slightly different number, such as `0.800000042353`.
  /// This is expected since the internal audio engine uses float
  /// instead of double, and so there are rounding errors.
  void setVolume(SoundHandle handle, double volume) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    _controller.soLoudFFI.setVolume(handle, volume);
  }

  /// Get a sound's current pan setting.
  ///
  /// [handle] the sound handle.
  /// Returns the range of the pan values is -1 to 1, where -1 is left, 0 is
  /// middle and and 1 is right.
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  ///
  /// Note that if you `setPan()` to `0.8` and then `getPan()`, you might
  /// get a slightly different number, such as `0.800000042353`.
  /// This is expected since the internal audio engine uses float
  /// instead of double, and so there are rounding errors.
  double getPan(SoundHandle handle) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    return SoLoudController().soLoudFFI.getPan(handle);
  }

  /// Set a sound's current pan setting.
  ///
  /// [handle] the sound handle.
  /// [pan] the range of the pan values is -1 to 1, where -1 is left, 0 is
  /// middle and and 1 is right.
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  ///
  /// Note that if you `setPan()` to `0.8` and then `getPan()`, you might
  /// get a slightly different number, such as `0.800000042353`.
  /// This is expected since the internal audio engine uses float
  /// instead of double, and so there are rounding errors.
  void setPan(SoundHandle handle, double pan) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    assert(
      pan >= -1 && pan <= 1,
      'The pan argument must be in range -1 to 1 inclusive!',
    );
    return SoLoudController().soLoudFFI.setPan(handle, pan.clamp(-1, 1));
  }

  /// Set the left/right volumes directly.
  /// Note that this does not affect the value returned by getPan.
  ///
  /// [handle] the sound handle.
  /// [panLeft] value for the left pan. Must be >= -1 and <= 1.
  /// [panRight] value for the right pan. Must be >= -1 and <= 1.
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  void setPanAbsolute(SoundHandle handle, double panLeft, double panRight) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    assert(
      panLeft >= -1 && panLeft <= 1,
      'The panLeft argument must be in range -1 to 1 inclusive!',
    );
    assert(
      panRight >= -1 && panRight <= 1,
      'The panRight argument must be in range -1 to 1 inclusive!',
    );
    return SoLoudController().soLoudFFI.setPanAbsolute(
          handle,
          panLeft.clamp(-1, 1),
          panRight.clamp(-1, 1),
        );
  }

  /// Check if the [handle] is still valid.
  ///
  /// Returns `true` if the sound instance identified by its [handle] is
  /// currently playing or paused. Returns `false` if it's been stopped
  /// or if it finished playing.
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  bool getIsValidVoiceHandle(SoundHandle handle) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    return _controller.soLoudFFI.getIsValidVoiceHandle(handle);
  }

  /// Returns the number of concurrent sounds that are playing at the moment.
  int getActiveVoiceCount() {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    return _controller.soLoudFFI.getActiveVoiceCount();
  }

  /// Returns the number of concurrent sounds that are playing a
  /// specific audio source.
  int countAudioSource(AudioSource audioSource) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    return _controller.soLoudFFI.countAudioSource(audioSource.soundHash);
  }

  /// Returns the number of voices the application has told SoLoud to play.
  int getVoiceCount() {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    return _controller.soLoudFFI.getVoiceCount();
  }

  /// Get a sound's protection state.
  ///
  /// See [setProtectVoice] for details]
  bool getProtectVoice(SoundHandle handle) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    return _controller.soLoudFFI.getProtectVoice(handle);
  }

  /// Sets a sound instance's protection state.
  ///
  /// The sound is specified via its [handle].
  ///
  /// Normally, if you try to play more sounds than there are voices
  /// (a.k.a. "channels"),
  /// SoLoud will kill off the oldest playing sound to make room.
  /// This is normally okay _except_ when you have background music
  /// or ambience playing.
  /// These sounds will likely be the oldest playing sounds, and you don't
  /// want them to be stopped just because there's a lot of sound effects
  /// playing at the same time.
  ///
  /// You can solve this by protecting the sound instance.
  /// Normally, you'd want to call [setProtectVoice] on all long-running,
  /// looping or somehow especially important audio.
  ///
  /// If all voices are protected, the result is undefined.
  /// The number of protected entries is inclusive in the
  /// maximum number of active voices [getMaxActiveVoiceCount].
  /// For example, when having max active voice count set to 16, and
  /// you want to play 20 other sounds, the protected voice will still play
  /// but you will hear only 15 of the other 20.
  void setProtectVoice(SoundHandle handle, bool protect) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    _controller.soLoudFFI.setProtectVoice(handle, protect);
  }

  /// Gets the current maximum active voice count.
  int getMaxActiveVoiceCount() {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    return _controller.soLoudFFI.getMaxActiveVoiceCount();
  }

  /// Sets the current maximum active voice count.
  ///
  /// If voice count is higher than the maximum active voice count,
  /// SoLoud will pick the ones with the highest volume to actually play.
  ///
  /// NOTE: The number of concurrent voices is limited, as having unlimited
  /// voices would cause performance issues, and could lead unnecessary
  /// clipping. The default number of maximum concurrent voices is 16,
  /// but this can be adjusted at runtime.
  ///
  /// The hard maximum count is 4095, but if more are
  /// required, SoLoud can be modified to support more. But seriously, if you
  /// need more than 4095 sounds playing _at once_,
  /// you're probably going to need some serious changes anyway.
  void setMaxActiveVoiceCount(int maxVoiceCount) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    _controller.soLoudFFI.setMaxActiveVoiceCount(maxVoiceCount);
  }

  /// Return a floats matrix of 256x512.
  /// Every row are composed of 256 FFT values plus 256 of wave data.
  /// Every time is called, a new row is stored in the
  /// first row and all the previous rows are shifted up. The last
  /// one will be lost.
  ///
  /// [audioData] this is the list where data is stored. It can be read like
  /// any other list. For example the first row can be read like this:
  /// `audioData[0...255]` this range represents FFT values for the first row
  /// `audioData[256...512]` this range represents wave data for the first row
  /// multiply the index by the row number you want to query.
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  /// Throws [SoLoudVisualizationNotEnabledException] if the visualization
  /// flag is not enableb. Please, Use `setVisualizationEnabled(true)`
  /// when needed.
  /// Throws [SoLoudNullPointerException] something is going wrong with the
  /// player engine. Please, open an issue on
  /// [GitHub](https://github.com/alnitak/flutter_soloud/issues) providing
  /// a simple working example.
  @experimental
  @Deprecated('Please use AudioData class instead.')
  void getAudioTexture2D(AudioData audioData) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    if (!_isVisualizationEnabled) {
      throw const SoLoudVisualizationNotEnabledException();
    }
    final error = _controller.soLoudFFI.getAudioTexture2D(audioData);
    _logPlayerError(error, from: 'getAudioTexture2D() result');
    if (error != PlayerErrors.noError) {
      throw SoLoudCppException.fromPlayerError(error);
    }
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
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  @experimental
  void setFftSmoothing(double smooth) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    _controller.soLoudFFI.setFftSmoothing(smooth);
  }

  /////////////////////////////////////////
  /// voice groups
  /////////////////////////////////////////

  /// Used to create a new voice group. Returns 0 if not successful.
  SoundHandle createVoiceGroup() {
    final ret = _controller.soLoudFFI.createVoiceGroup();
    if (ret.isError) throw const SoLoudCreateVoiceGroupDartException();
    return ret;
  }

  /// Deallocates the voice group. Does not stop the voices attached to the
  /// voice group.
  ///
  /// [handle] the group handle to destroy.
  void destroyVoiceGroup(SoundHandle handle) {
    return _controller.soLoudFFI.destroyVoiceGroup(handle);
  }

  /// Adds voice handle to the voice group. The voice handles can still be
  /// used separate from the group.
  /// [voiceGroupHandle] the group handle to add the new [voiceHandles].
  /// [voiceHandles] voice handle to add to the [voiceGroupHandle].
  void addVoicesToGroup(
    SoundHandle voiceGroupHandle,
    List<SoundHandle> voiceHandles,
  ) {
    return _controller.soLoudFFI.addVoicesToGroup(
      voiceGroupHandle,
      voiceHandles,
    );
  }

  /// Checks if the handle is a valid voice group. Does not care if the
  /// voice group is empty.
  ///
  /// [handle] the group handle to check.
  /// Return true if [handle] is a group handle.
  bool isVoiceGroup(SoundHandle handle) {
    return _controller.soLoudFFI.isVoiceGroup(handle);
  }

  /// Checks whether a voice group is empty. SoLoud automatically trims
  /// the voice groups of voices that have ended, so the group may be
  /// empty even though you've added valid voice handles to it.
  ///
  /// [handle] group handle to check.
  /// Return true if the group handle doesn't have any voices.
  bool isVoiceGroupEmpty(SoundHandle handle) {
    return _controller.soLoudFFI.isVoiceGroupEmpty(handle);
  }

  // ///////////////////////////////////////
  // faders
  // //////////////////////////////////////

  /// Smoothly changes the global volume to the value of [to]
  /// over specified [time].
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  void fadeGlobalVolume(double to, Duration time) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    final error = _controller.soLoudFFI.fadeGlobalVolume(to, time);
    if (error != PlayerErrors.noError) {
      _log.severe(() => 'fadeGlobalVolume(): $error');
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  /// Smoothly changes a single sound instance's volume
  /// to the value of [to] over the specified [time].
  ///
  /// The sound instance is provided via its [handle].
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  void fadeVolume(SoundHandle handle, double to, Duration time) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    final error = _controller.soLoudFFI.fadeVolume(handle, to, time);
    if (error != PlayerErrors.noError) {
      _log.severe(() => 'fadeVolume(): $error');
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  /// Smoothly changes a currently playing sound's pan setting
  /// to the value of [to] over specified [time].
  ///
  /// The sound instance is provided via its [handle].
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  void fadePan(SoundHandle handle, double to, Duration time) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    final error = _controller.soLoudFFI.fadePan(handle, to, time);
    if (error != PlayerErrors.noError) {
      _log.severe(() => 'fadePan(): $error');
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  /// Smoothly changes a currently playing sound's relative play speed
  /// to the value of [to] over specified [time].
  ///
  /// The sound instance is provided via its [handle].
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  void fadeRelativePlaySpeed(SoundHandle handle, double to, Duration time) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    final error = _controller.soLoudFFI.fadeRelativePlaySpeed(handle, to, time);
    if (error != PlayerErrors.noError) {
      _log.severe(() => 'fadeRelativePlaySpeed(): $error');
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  /// Waits the specified [time], then pauses the currently playing sound.
  ///
  /// The sound instance is provided via its [handle].
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  void schedulePause(SoundHandle handle, Duration time) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    final error = _controller.soLoudFFI.schedulePause(handle, time);
    if (error != PlayerErrors.noError) {
      _log.severe(() => 'schedulePause(): $error');
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  /// Waits the specified [time], then stops the currently playing sound.
  ///
  /// The sound instance is provided via its [handle].
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  void scheduleStop(SoundHandle handle, Duration time) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    final error = _controller.soLoudFFI.scheduleStop(handle, time);
    if (error != PlayerErrors.noError) {
      _log.severe(() => 'scheduleStop(): $error');
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  /// Sets fader to oscillate the volume at specified frequency.
  ///
  /// The sound instance is specified via its [handle].
  ///
  /// The value of [from] is the lowest value for the oscillation.
  /// The value of [to] is the highest value for the oscillation.
  /// The specified [time] is the period of oscillation.
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  void oscillateVolume(
      SoundHandle handle, double from, double to, Duration time) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    final error = _controller.soLoudFFI.oscillateVolume(handle, from, to, time);
    if (error != PlayerErrors.noError) {
      _log.severe(() => 'oscillateVolume(): $error');
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  /// Sets oscillation of the pan at specified frequency.
  ///
  /// The sound instance is specified via its [handle].
  ///
  /// The value of [from] is the leftmost value for the oscillation.
  /// The value of [to] is the rightmost value for the oscillation.
  /// The specified [time] is the period of oscillation.
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  void oscillatePan(SoundHandle handle, double from, double to, Duration time) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    final error = _controller.soLoudFFI.oscillatePan(handle, from, to, time);
    if (error != PlayerErrors.noError) {
      _log.severe(() => 'oscillatePan(): $error');
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  /// Sets oscillation of the play speed at specified frequency.
  ///
  /// The sound instance is specified via its [handle].
  ///
  /// The value of [from] is the lowest value for the oscillation.
  /// The value of [to] is the highest value for the oscillation.
  /// The specified [time] is the period of oscillation.
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  void oscillateRelativePlaySpeed(
      SoundHandle handle, double from, double to, Duration time) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    final error = _controller.soLoudFFI
        .oscillateRelativePlaySpeed(handle, from, to, time);
    if (error != PlayerErrors.noError) {
      _log.severe(() => 'oscillateRelativePlaySpeed(): $error');
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  /// Set fader to oscillate the global volume at specified frequency.
  ///
  /// The value of [from] is the lowest value for the oscillation.
  /// The value of [to] is the highest value for the oscillation.
  /// The specified [time] is the period of oscillation.
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  void oscillateGlobalVolume(double from, double to, Duration time) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    final error = _controller.soLoudFFI.oscillateGlobalVolume(from, to, time);
    if (error != PlayerErrors.noError) {
      _log.severe(() => 'oscillateGlobalVolume(): $error');
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  /// Fade a parameter of a filter.
  ///
  /// it fades the global filter.
  /// [filterType] filter to modify a param.
  /// [attributeId] the attribute index to fade.
  /// [to] value the attribute should go in [time] duration.
  /// [time] the fade slope duration.
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  void fadeGlobalFilterParameter(
    FilterType filterType,
    int attributeId,
    double to,
    Duration time,
  ) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    final error = _controller.soLoudFFI.fadeFilterParameter(
      filterType,
      attributeId,
      to,
      time.toDouble(),
    );
    if (error != PlayerErrors.noError) {
      _log.severe(() => 'fadeFilterParameter(): $error');
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  /// Oscillate a parameter of a filter.
  ///
  /// it fades the global filter.
  /// [filterType] filter to modify a param.
  /// [attributeId] the attribute index to fade.
  /// [from] the starting value the attribute sould start to oscillate.
  /// [to] the ending value the attribute sould end to oscillate.
  /// [time] the fade slope duration.
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  void oscillateGlobalFilterParameter(
    FilterType filterType,
    int attributeId,
    double from,
    double to,
    Duration time,
  ) {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }
    final error = _controller.soLoudFFI.oscillateFilterParameter(
      filterType,
      attributeId,
      from,
      to,
      time.toDouble(),
    );
    if (error != PlayerErrors.noError) {
      _log.severe(() => 'oscillateFilterParameter(): $error');
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  // ///////////////////////////////////////
  // / Global filters
  // ///////////////////////////////////////

  /// Checks whether the given [filterType] is active.
  ///
  /// Returns `-1` if the filter is not active. Otherwise, returns
  /// the index of the given filter.
  int isFilterActive(FilterType filterType) {
    final ret = _controller.soLoudFFI.isFilterActive(filterType);
    if (ret.error != PlayerErrors.noError) {
      _log.severe(() => 'isFilterActive(): ${ret.error}');
      throw SoLoudCppException.fromPlayerError(ret.error);
    }
    return ret.index;
  }

  /// Gets parameters of the given [filterType].
  ///
  /// Returns the list of param names.
  List<String> getFilterParamNames(FilterType filterType) {
    final ret = _controller.soLoudFFI.getFilterParamNames(filterType);
    if (ret.error != PlayerErrors.noError) {
      _log.severe(() => 'getFilterParamNames(): ${ret.error}');
      throw SoLoudCppException.fromPlayerError(ret.error);
    }
    return ret.names;
  }

  /// Adds a [filterType] to all sounds.
  ///
  /// Throws [SoLoudMaxFilterNumberReachedException] when the max number of
  ///     concurrent filter is reached (default max filter is 8).
  /// Throws [SoLoudFilterAlreadyAddedException] when trying to add a filter
  ///     that has already been added.
  void addGlobalFilter(FilterType filterType) {
    final error = _controller.soLoudFFI.addFilter(filterType);
    if (error != PlayerErrors.noError) {
      _log.severe(() => 'addGlobalFilter(): $error');
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  /// Removes [filterType] from all sounds.
  void removeGlobalFilter(FilterType filterType) {
    final error = _controller.soLoudFFI.removeFilter(filterType);
    if (error != PlayerErrors.noError) {
      _log.severe(() => 'removeGlobalFilter(): $error');
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  /// Set the effect parameter with id [attributeId] of [filterType]
  /// with [value] value.
  ///
  /// Specify the [attributeId] of the parameter (which you can learn from
  /// [getFilterParamNames]), and its new [value].
  ///
  /// applyed to the global filter.
  /// [filterType] filter to modify a param.
  /// Returns [PlayerErrors.noError] if no errors.
  void setGlobalFilterParameter(
    FilterType filterType,
    int attributeId,
    double value,
  ) {
    final error = _controller.soLoudFFI.setFilterParams(
      filterType,
      attributeId,
      value,
    );
    if (error != PlayerErrors.noError) {
      _log.severe(() => 'setFxParams(): $error');
      throw SoLoudCppException.fromPlayerError(error);
    }
  }

  /// Set the effect parameter with id [attributeId] of [filterType]
  /// with [value] value.
  @Deprecated('Please use setGlobalFilterParameter class instead.')
  void setFilterParameter(
    FilterType filterType,
    int attributeId,
    double value,
  ) =>
      setGlobalFilterParameter(filterType, attributeId, value);

  /// Get the effect parameter value with id [attributeId] of [filterType].
  ///
  /// Specify the [attributeId] of the parameter (which you can learn from
  /// [getFilterParamNames]).
  ///
  /// it gets the global filter value.
  /// [filterType] the filter to modify a parameter.
  /// Returns the value of the parameter.
  double getGlobalFilterParameter(
    FilterType filterType,
    int attributeId,
  ) {
    final ret = _controller.soLoudFFI.getFilterParams(
      filterType,
      attributeId,
    );

    _logPlayerError(ret.error, from: 'getGlobalFilterParameter()');
    if (ret.error != PlayerErrors.noError) {
      throw SoLoudCppException.fromPlayerError(ret.error);
    }
    return ret.value;
  }

  /// Get the effect parameter value with id [attributeId] of [filterType].
  @Deprecated('Please use getGlobalFilterParameter class instead.')
  double getFilterParameter(
    FilterType filterType,
    int attributeId, {
    SoundHandle handle = const SoundHandle(0),
  }) =>
      getGlobalFilterParameter(filterType, attributeId);

  // ////////////////////////////////////////////////
  // Below all the methods implemented with FFI for the 3D audio
  // more info: https://solhsa.com/soloud/core3d.html
  // ////////////////////////////////////////////////

  /// This function is the 3D version of the [play] call.
  ///
  /// The coordinate system is right handed.
  ///
  /// ```text
  ///           Y
  ///           ^
  ///           |
  ///           |
  ///           |
  ///           --------> X
  ///          /
  ///         /
  ///        Z
  /// ```
  ///
  /// The listener position is `(0, 0, 0)` by default but can be changed
  /// with [set3dListenerParameters].
  ///
  /// The parameters [posX], [posY] and [posZ] are the audio source's
  /// position coordinates.
  ///
  /// The parameters [velX], [velY] and [velZ] are the audio source's velocity.
  /// Defaults to `(0, 0, 0)`.
  ///
  /// The rest of the parameters are equivalent to the non-3D version of this
  /// method ([play]).
  ///
  /// Returns the [SoundHandle] of this new sound.
  ///
  /// Throws [SoLoudNotInitializedException] if the engine is not initialized.
  Future<SoundHandle> play3d(
    AudioSource sound,
    double posX,
    double posY,
    double posZ, {
    double velX = 0,
    double velY = 0,
    double velZ = 0,
    double volume = 1,
    bool paused = false,
    bool looping = false,
    Duration loopingStartAt = Duration.zero,
  }) async {
    if (!isInitialized) {
      throw const SoLoudNotInitializedException();
    }

    final ret = _controller.soLoudFFI.play3d(
      sound.soundHash,
      posX,
      posY,
      posZ,
      velX: velX,
      velY: velY,
      velZ: velZ,
      volume: volume,
      paused: paused,
      looping: looping,
      loopingStartAt: loopingStartAt,
    );

    _logPlayerError(ret.error, from: 'play3d()');
    if (ret.error != PlayerErrors.noError) {
      throw SoLoudCppException.fromPlayerError(ret.error);
    }

    final filtered =
        _activeSounds.where((s) => s.soundHash == sound.soundHash).toSet();
    if (filtered.isEmpty) {
      _log.severe(() => 'play3d(): soundHash ${sound.soundHash} not found');
      throw SoLoudSoundHashNotFoundDartException(sound.soundHash);
    }

    assert(filtered.length == 1, 'Duplicate sounds found');
    for (final activeSound in filtered) {
      activeSound.handlesInternal.add(ret.newHandle);
    }
    sound.handlesInternal.add(ret.newHandle);
    return ret.newHandle;
  }

  /// Since SoLoud has no knowledge of the scale of your coordinates,
  /// you may need to adjust the speed of sound for these effects
  /// to work correctly. The default value is 343, which assumes
  /// that your world coordinates are in meters (where 1 unit is 1 meter),
  /// and that the environment is dry air at around 20 degrees Celsius.
  void set3dSoundSpeed(double speed) {
    _controller.soLoudFFI.set3dSoundSpeed(speed);
  }

  /// Gets the speed of sound.
  ///
  /// See [set3dSoundSpeed] for details.
  double get3dSoundSpeed() {
    return _controller.soLoudFFI.get3dSoundSpeed();
  }

  /// Sets the position, at-vector, up-vector and velocity
  /// parameters of the 3D audio listener with one call.
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
    _controller.soLoudFFI.set3dListenerParameters(posX, posY, posZ, atX, atY,
        atZ, upX, upY, upZ, velocityX, velocityY, velocityZ);
  }

  /// Sets the position parameter of the 3D audio listener.
  void set3dListenerPosition(double posX, double posY, double posZ) {
    _controller.soLoudFFI.set3dListenerPosition(posX, posY, posZ);
  }

  /// Sets the at-vector (i.e. position) parameter of the 3D audio listener.
  void set3dListenerAt(double atX, double atY, double atZ) {
    _controller.soLoudFFI.set3dListenerAt(atX, atY, atZ);
  }

  /// Sets the up-vector parameter of the 3D audio listener.
  void set3dListenerUp(double upX, double upY, double upZ) {
    _controller.soLoudFFI.set3dListenerUp(upX, upY, upZ);
  }

  /// Sets the 3D listener's velocity vector.
  void set3dListenerVelocity(
      double velocityX, double velocityY, double velocityZ) {
    _controller.soLoudFFI
        .set3dListenerVelocity(velocityX, velocityY, velocityZ);
  }

  /// Sets the position and velocity parameters of a live
  /// 3D audio source with one call.
  ///
  /// The sound instance is provided via its [handle].
  void set3dSourceParameters(SoundHandle handle, double posX, double posY,
      double posZ, double velocityX, double velocityY, double velocityZ) {
    _controller.soLoudFFI.set3dSourceParameters(
        handle, posX, posY, posZ, velocityX, velocityY, velocityZ);
  }

  /// Sets the position of a live 3D audio source.
  void set3dSourcePosition(
      SoundHandle handle, double posX, double posY, double posZ) {
    _controller.soLoudFFI.set3dSourcePosition(handle, posX, posY, posZ);
  }

  /// Set the velocity parameter of a live 3D audio source.
  void set3dSourceVelocity(SoundHandle handle, double velocityX,
      double velocityY, double velocityZ) {
    _controller.soLoudFFI
        .set3dSourceVelocity(handle, velocityX, velocityY, velocityZ);
  }

  /// Sets the minimum and maximum distance parameters
  /// of a live 3D audio source.
  void set3dSourceMinMaxDistance(
      SoundHandle handle, double minDistance, double maxDistance) {
    _controller.soLoudFFI
        .set3dSourceMinMaxDistance(handle, minDistance, maxDistance);
  }

  /// You can change the attenuation model and rolloff factor parameters of
  /// a live 3D audio source.
  ///
  /// ```
  /// 0 NO_ATTENUATION        No attenuation
  /// 1 INVERSE_DISTANCE      Inverse distance attenuation model
  /// 2 LINEAR_DISTANCE       Linear distance attenuation model
  /// 3 EXPONENTIAL_DISTANCE  Exponential distance attenuation model
  /// ```
  ///
  /// See https://solhsa.com/soloud/concepts3d.html.
  void set3dSourceAttenuation(
    SoundHandle handle,
    int attenuationModel,
    double attenuationRolloffFactor,
  ) {
    _controller.soLoudFFI.set3dSourceAttenuation(
      handle,
      attenuationModel,
      attenuationRolloffFactor,
    );
  }

  /// Sets the doppler factor of a live 3D audio source.
  void set3dSourceDopplerFactor(SoundHandle handle, double dopplerFactor) {
    _controller.soLoudFFI.set3dSourceDopplerFactor(handle, dopplerFactor);
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
