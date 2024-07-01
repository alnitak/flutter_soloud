import 'dart:ffi' as ffi;

import 'package:ffi/ffi.dart';
import 'package:flutter_soloud/src/bindings/audio_data.dart';
import 'package:flutter_soloud/src/bindings/bindings_capture.dart';
import 'package:flutter_soloud/src/enums.dart';

/// FFI bindings to capture with miniaudio.
class FlutterCaptureFfi extends FlutterCapture {
  /// The symbols are looked up in [dynamicLibrary].
  FlutterCaptureFfi(ffi.DynamicLibrary dynamicLibrary)
      : _lookup = dynamicLibrary.lookup;

  /// The symbols are looked up with [lookup].
  FlutterCaptureFfi.fromLookup(
      ffi.Pointer<T> Function<T extends ffi.NativeType>(String symbolName)
          lookup)
      : _lookup = lookup;

  /// Holds the symbol lookup function.
  final ffi.Pointer<T> Function<T extends ffi.NativeType>(String symbolName)
      _lookup;

  @override
  List<CaptureDevice> listCaptureDevices() {
    final ret = <CaptureDevice>[];
    // ignore: omit_local_variable_types
    final ffi.Pointer<ffi.Pointer<ffi.Char>> deviceNames =
        calloc(ffi.sizeOf<ffi.Pointer<ffi.Pointer<ffi.Char>>>() * 50);
    // ignore: omit_local_variable_types
    final ffi.Pointer<ffi.Pointer<ffi.Int>> deviceIsDefault =
        calloc(ffi.sizeOf<ffi.Pointer<ffi.Pointer<ffi.Int>>>() * 50);
    // ignore: omit_local_variable_types
    final ffi.Pointer<ffi.Int> nDevices = calloc();

    _listCaptureDevices(
      deviceNames,
      deviceIsDefault,
      nDevices,
    );

    final ndev = nDevices.value;
    for (var i = 0; i < ndev; i++) {
      final s1 = (deviceNames + i).value;
      final s = s1.cast<Utf8>().toDartString();
      final n1 = (deviceIsDefault + i).value;
      final n = n1.value;
      ret.add(CaptureDevice(s, n == 1));
    }

    /// Free allocated memory done in C.
    /// This work on all platforms but not on win.
    // for (int i = 0; i < ndev; i++) {
    //   calloc.free(devices.elementAt(i).value.ref.name);
    //   calloc.free(devices.elementAt(i).value);
    // }
    _freeListCaptureDevices(
      deviceNames,
      deviceIsDefault,
      ndev,
    );

    calloc
      ..free(deviceNames)
      ..free(nDevices);
    return ret;
  }

  late final _listCaptureDevicesPtr = _lookup<
      ffi.NativeFunction<
          ffi.Void Function(
              ffi.Pointer<ffi.Pointer<ffi.Char>>,
              ffi.Pointer<ffi.Pointer<ffi.Int>>,
              ffi.Pointer<ffi.Int>)>>('listCaptureDevices');
  late final _listCaptureDevices = _listCaptureDevicesPtr.asFunction<
      void Function(ffi.Pointer<ffi.Pointer<ffi.Char>>,
          ffi.Pointer<ffi.Pointer<ffi.Int>>, ffi.Pointer<ffi.Int>)>();

  late final _freeListCaptureDevicesPtr = _lookup<
      ffi.NativeFunction<
          ffi.Void Function(
              ffi.Pointer<ffi.Pointer<ffi.Char>>,
              ffi.Pointer<ffi.Pointer<ffi.Int>>,
              ffi.Int)>>('freeListCaptureDevices');
  late final _freeListCaptureDevices = _freeListCaptureDevicesPtr.asFunction<
      void Function(ffi.Pointer<ffi.Pointer<ffi.Char>>,
          ffi.Pointer<ffi.Pointer<ffi.Int>>, int)>();

  @override
  CaptureErrors initCapture(int deviceID) {
    final e = _initCapture(deviceID);
    return CaptureErrors.values[e];
  }

  late final _initCapturePtr =
      _lookup<ffi.NativeFunction<ffi.Int32 Function(ffi.Int)>>('initCapture');
  late final _initCapture = _initCapturePtr.asFunction<int Function(int)>();

  @override
  void disposeCapture() {
    return _disposeCapture();
  }

  late final _disposeCapturePtr =
      _lookup<ffi.NativeFunction<ffi.Void Function()>>('disposeCapture');
  late final _disposeCapture = _disposeCapturePtr.asFunction<void Function()>();

  @override
  bool isCaptureInited() {
    return _isCaptureInited() == 1;
  }

  late final _isCaptureInitedPtr =
      _lookup<ffi.NativeFunction<ffi.Int Function()>>('isCaptureInited');
  late final _isCaptureInited =
      _isCaptureInitedPtr.asFunction<int Function()>();

  @override
  bool isCaptureStarted() {
    return _isCaptureStarted() == 1;
  }

  late final _isCaptureStartedPtr =
      _lookup<ffi.NativeFunction<ffi.Int Function()>>('isCaptureStarted');
  late final _isCaptureStarted =
      _isCaptureStartedPtr.asFunction<int Function()>();

  @override
  CaptureErrors startCapture() {
    return CaptureErrors.values[_startCapture()];
  }

  late final _startCapturePtr =
      _lookup<ffi.NativeFunction<ffi.Int32 Function()>>('startCapture');
  late final _startCapture = _startCapturePtr.asFunction<int Function()>();

  @override
  CaptureErrors stopCapture() {
    return CaptureErrors.values[_stopCapture()];
  }

  late final _stopCapturePtr =
      _lookup<ffi.NativeFunction<ffi.Int32 Function()>>('stopCapture');
  late final _stopCapture = _stopCapturePtr.asFunction<int Function()>();

  @override
  void getCaptureFft(AudioData fft) {
    return _getCaptureFft(fft.ctrl.samplesWave);
  }

  late final _getCaptureFftPtr = _lookup<
      ffi.NativeFunction<
          ffi.Void Function(
              ffi.Pointer<ffi.Pointer<ffi.Float>>)>>('getCaptureFft');
  late final _getCaptureFft = _getCaptureFftPtr
      .asFunction<void Function(ffi.Pointer<ffi.Pointer<ffi.Float>>)>();

  @override
  void getCaptureWave(AudioData wave) {
    return _getCaptureWave(wave.ctrl.samplesWave!);
  }

  late final _getCaptureWavePtr = _lookup<
      ffi.NativeFunction<
          ffi.Void Function(
              ffi.Pointer<ffi.Pointer<ffi.Float>>)>>('getCaptureWave');
  late final _getCaptureWave = _getCaptureWavePtr
      .asFunction<void Function(ffi.Pointer<ffi.Pointer<ffi.Float>>)>();

  @override
  void getCaptureAudioTexture(AudioData samples) {
    return _getCaptureTexture(samples.ctrl.samples1D);
  }

  late final _getCaptureTexturePtr =
      _lookup<ffi.NativeFunction<ffi.Void Function(ffi.Pointer<ffi.Float>)>>(
          'getCaptureTexture');
  late final _getCaptureTexture =
      _getCaptureTexturePtr.asFunction<void Function(ffi.Pointer<ffi.Float>)>();

  @override
  CaptureErrors getCaptureAudioTexture2D(AudioData samples) {
    final int ret = _getCaptureAudioTexture2D(samples.ctrl.samples2D);
    return CaptureErrors.values[ret];
  }

  late final _getCaptureAudioTexture2DPtr = _lookup<
          ffi.NativeFunction<
              ffi.Int32 Function(ffi.Pointer<ffi.Pointer<ffi.Float>>)>>(
      'getCaptureAudioTexture2D');
  late final _getCaptureAudioTexture2D = _getCaptureAudioTexture2DPtr
      .asFunction<int Function(ffi.Pointer<ffi.Pointer<ffi.Float>>)>();

  double getCaptureTextureValue(int row, int column) {
    return _getCaptureTextureValue(row, column);
  }

  late final _getCaptureTextureValuePtr =
      _lookup<ffi.NativeFunction<ffi.Float Function(ffi.Int, ffi.Int)>>(
          'getCaptureTextureValue');
  late final _getCaptureTextureValue =
      _getCaptureTextureValuePtr.asFunction<double Function(int, int)>();

  @override
  CaptureErrors setCaptureFftSmoothing(double smooth) {
    final int ret = _setCaptureFftSmoothing(smooth);
    return CaptureErrors.values[ret];
  }

  late final _setCaptureFftSmoothingPtr =
      _lookup<ffi.NativeFunction<ffi.Int32 Function(ffi.Float)>>(
          'setCaptureFftSmoothing');
  late final _setCaptureFftSmoothing =
      _setCaptureFftSmoothingPtr.asFunction<int Function(double)>();
}
