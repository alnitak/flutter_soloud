import 'dart:async';
import 'dart:developer' as dev;
import 'dart:io';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:logging/logging.dart';

// ignore_for_file: experimental_member_use

/// Mixer Output Capture Example
///
/// This example demonstrates how to capture the master mixer output as a
/// stream of audio data. The captured data can be saved, processed, or
/// streamed to another destination.
///
/// Key concepts:
/// - `SoLoud.instance.startMixerOutputStream` starts capturing the master mix.
/// - `SoLoud.instance.stopMixerOutputStream` stops the capture.
/// - `MixerOutputFormat` selects the output PCM format.
/// - Filters applied to the master bus (like pitch shift) are reflected in
///   the captured output.

void main() async {
  Logger.root.level = kDebugMode ? Level.FINE : Level.INFO;
  Logger.root.onRecord.listen((record) {
    dev.log(
      record.message,
      time: record.time,
      level: record.level.value,
      name: record.loggerName,
      zone: record.zone,
      error: record.error,
      stackTrace: record.stackTrace,
    );
  });

  WidgetsFlutterBinding.ensureInitialized();

  await SoLoud.instance.init();

  runApp(
    const MaterialApp(
      home: MixerCaptureExample(),
    ),
  );
}

/// Main widget for the mixer capture example.
class MixerCaptureExample extends StatefulWidget {
  /// Creates a new mixer capture example widget.
  const MixerCaptureExample({super.key});

  @override
  State<MixerCaptureExample> createState() => _MixerCaptureExampleState();
}

class _MixerCaptureExampleState extends State<MixerCaptureExample> {
  AudioSource? _sound;
  SoundHandle? _handle;
  StreamSubscription<Uint8List>? _captureSubscription;

  var _format = MixerOutputFormat.pcmF32le;
  var _isCapturing = false;
  var _totalBytes = 0;
  var _pitchShift = 1.0;
  String? _outputFilePath;

  @override
  void initState() {
    super.initState();
    _loadSound();
  }

  @override
  void dispose() {
    // Stop the native capture first so the stream can flush its final chunk
    // and the subscription's onDone handler can close the output file.
    SoLoud.instance.stopMixerOutputStream();
    _captureSubscription?.cancel();
    SoLoud.instance.deinit();
    super.dispose();
  }

  Future<void> _loadSound() async {
    _sound = await SoLoud.instance.loadAsset('assets/audio/IveSeenThings.mp3');
    if (mounted) setState(() {});
  }

  void _play() {
    if (_sound == null) return;
    _handle = SoLoud.instance.play(_sound!, looping: true);
  }

  void _stopPlayback() {
    if (_handle != null) {
      SoLoud.instance.stop(_handle!);
      _handle = null;
    }
  }

  Future<void> _pickOutputFile() async {
    final fileName = _suggestedFileName();
    final result = await FilePicker.platform.saveFile(
      dialogTitle: 'Save mixer capture',
      fileName: fileName,
    );

    if (result == null) {
      // User cancelled the picker; do nothing.
      return;
    }

    setState(() => _outputFilePath = result);
  }

  Future<void> _patchWavHeader(String path) async {
    final header = SoLoud.instance.getMixerOutputWavHeader();
    if (header.length != 44) {
      dev.log('WAV header unavailable; skipping patch.');
      return;
    }

    try {
      final raf = File(path).openSync(mode: FileMode.writeOnlyAppend)
        ..setPositionSync(0)
        ..writeFromSync(header);
      await raf.close();
    } on FileSystemException catch (e) {
      dev.log('Failed to patch WAV header: $e');
    }
  }

  String _suggestedFileName() {
    final extension = switch (_format) {
      MixerOutputFormat.pcmF32le => 'f32le.raw',
      MixerOutputFormat.pcmS8 => 's8.raw',
      MixerOutputFormat.pcmS16le => 's16le.raw',
      MixerOutputFormat.pcmS32le => 's32le.raw',
      MixerOutputFormat.opus => 'opus',
      MixerOutputFormat.vorbis => 'ogg',
      MixerOutputFormat.flac => 'flac',
      MixerOutputFormat.wav => 'wav',
    };
    return 'mixer_capture.$extension';
  }

  void _toggleCapture() {
    if (_isCapturing) {
      // Stop the native capture first so the WAV encoder flushes its final
      // chunk into the stream. The stream will close and the subscription's
      // onDone will fire; after that we can cancel the listener.
      SoLoud.instance.stopMixerOutputStream();
      _captureSubscription?.cancel();
      _captureSubscription = null;
      setState(() => _isCapturing = false);
      return;
    }

    _totalBytes = 0;
    final captureFormat = _format;
    final stream = SoLoud.instance.startMixerOutputStream(
      format: captureFormat,
    );

    RandomAccessFile? outputFile;
    if (_outputFilePath != null) {
      try {
        outputFile = File(_outputFilePath!).openSync(mode: FileMode.writeOnly);
      } on FileSystemException catch (e) {
        dev.log('Failed to open output file: $e');
      }
    }

    _captureSubscription = stream.listen(
      (chunk) {
        setState(() => _totalBytes += chunk.length);
        try {
          outputFile?.writeFromSync(chunk);
        } on FileSystemException catch (e) {
          dev.log('Failed to write capture chunk: $e');
        }
      },
      onError: (Object e) => dev.log('Mixer capture error: $e'),
      onDone: () async {
        try {
          await outputFile?.close();

          // WAV writes its header at the start of the stream with placeholder
          // sizes. Patch the first 44 bytes with the final header after the
          // capture stops so the saved file is valid.
          if (captureFormat == MixerOutputFormat.wav &&
              _outputFilePath != null) {
            await _patchWavHeader(_outputFilePath!);
          }
        } on FileSystemException catch (e) {
          dev.log('Failed to close output file: $e');
        }
      },
    );

    setState(() => _isCapturing = true);
  }

  void _setPitchShift(double value) {
    var v = value;
    if (v < 0.1) v = 0.1;
    setState(() => _pitchShift = v);
    if (!SoLoud.instance.filters.pitchShiftFilter.isActive) {
      SoLoud.instance.filters.pitchShiftFilter.activate();
    }
    SoLoud.instance.filters.pitchShiftFilter.shift.value = v;
  }

  String _formatLabel(MixerOutputFormat format) {
    switch (format) {
      case MixerOutputFormat.pcmF32le:
        return 'PCM F32LE';
      case MixerOutputFormat.pcmS8:
        return 'PCM S8';
      case MixerOutputFormat.pcmS16le:
        return 'PCM S16LE';
      case MixerOutputFormat.pcmS32le:
        return 'PCM S32LE';
      case MixerOutputFormat.opus:
      case MixerOutputFormat.vorbis:
      case MixerOutputFormat.flac:
        return 'OGG/$format';
      case MixerOutputFormat.wav:
        return 'WAV';
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Mixer Output Capture')),
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Column(
            spacing: 16,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                spacing: 8,
                children: [
                  ElevatedButton(
                    onPressed: _sound == null ? null : _play,
                    child: const Text('play'),
                  ),
                  ElevatedButton(
                    onPressed: _stopPlayback,
                    child: const Text('stop playback'),
                  ),
                ],
              ),
              Row(
                spacing: 8,
                children: [
                  const Text('Format:'),
                  DropdownButton<MixerOutputFormat>(
                    value: _format,
                    onChanged: _isCapturing
                        ? null
                        : (value) {
                            if (value != null) {
                              setState(() => _format = value);
                            }
                          },
                    items: [
                      for (final format in MixerOutputFormat.values)
                        DropdownMenuItem(
                          value: format,
                          child: Text(_formatLabel(format)),
                        ),
                    ],
                  ),
                ],
              ),
              Row(
                spacing: 8,
                children: [
                  ElevatedButton(
                    onPressed: _toggleCapture,
                    child: Text(
                      _isCapturing ? 'stop capture' : 'start capture',
                    ),
                  ),
                  if (defaultTargetPlatform == TargetPlatform.windows ||
                      defaultTargetPlatform == TargetPlatform.linux ||
                      defaultTargetPlatform == TargetPlatform.macOS)
                    ElevatedButton(
                      onPressed: _isCapturing ? null : _pickOutputFile,
                      child: const Text('choose file'),
                    ),
                ],
              ),
              Text(
                _outputFilePath == null
                    ? 'No output file selected'
                    : 'Saving to: $_outputFilePath',
              ),
              Text('Captured bytes: $_totalBytes'),
              Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const Text('Pitch shift'),
                  Slider(
                    value: _pitchShift,
                    max: 3,
                    label: _pitchShift.toStringAsFixed(2),
                    onChanged: _setPitchShift,
                  ),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }
}
