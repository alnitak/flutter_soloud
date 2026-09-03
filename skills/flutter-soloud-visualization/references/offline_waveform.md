# Offline waveform reading

From `example/lib/wave_data/wave_data.dart`. Signatures (verified in `lib/src/soloud.dart`):

```dart
Future<Float32List> readSamplesFromFile(
  String completeFileName,
  int numSamplesNeeded, {
  double startTime = 0,
  double endTime = -1,
  bool average = false,
});

Future<Float32List> readSamplesFromMem(
  Uint8List buffer,
  int numSamplesNeeded, {
  double startTime = 0,
  double endTime = -1,
  bool average = false,
});
```

Semantics:

- Returns `numSamplesNeeded` values equally spaced across `[startTime, endTime)` (times in **seconds**, `endTime: -1` = to end of file). Each value is nominally in `[-1.0, 1.0]` but that is not guaranteed.
- With `average: false` each value is the single sample at that position; with `average: true` it is the mean of all samples since the previous position — use `true` for display waveforms.
- The returned list can be **shorter** than `numSamplesNeeded` if `endTime` overshoots the audio length. Never index it assuming the requested length.
- Runs through `compute(...)` (a background isolate on native). **On Web there is no isolate** — `readSamplesFromMem` is synchronous under the hood and can freeze the UI for big files.
- `readSamplesFromFile` is **not available on Web**; always use `readSamplesFromMem` with the encoded bytes there (and it works on native too, so `readSamplesFromMem` is the portable choice).
- No engine init and no `loadMem`/`loadAsset` needed — these decode on their own. Asset bytes: `(await rootBundle.load('assets/a.mp3')).buffer.asUint8List()`.

Portable loading pattern from the example (N = 4 × widget width in px):

```dart
Future<void> _loadPath(int width, PlatformFile file) async {
  if (kIsWeb) {
    // On web you only have the bytes.
    data = await SoLoud.instance.readSamplesFromMem(
      file.bytes!,
      width * 4,
      average: average,
    );
  } else {
    final bytes = File(file.path!).readAsBytesSync();
    data = await SoLoud.instance.readSamplesFromMem(
      bytes,
      width * 4,
      average: average,
    );
  }
}
```

Painter (values are symmetric around the vertical center):

```dart
class WavePainter extends CustomPainter {
  const WavePainter({required this.data});

  final Float32List data;

  @override
  void paint(Canvas canvas, Size size) {
    final barWidth = size.width / data.length;
    final paint = Paint()
      ..color = Colors.yellowAccent
      ..strokeWidth = barWidth;

    for (var i = 0; i < data.length; i++) {
      final barHeight = size.height * data[i] * 2;
      canvas.drawLine(
        Offset(barWidth * i, (size.height - barHeight) / 2),
        Offset(barWidth * i, (size.height + barHeight) / 2),
        paint,
      );
    }
  }

  @override
  bool shouldRepaint(WavePainter oldDelegate) => true;
}
```

Errors thrown (all `SoLoudCppException` subclasses): `SoLoudReadSamplesNoBackendCppException`, `SoLoudReadSamplesFailedToGetDataFormatCppException`, `SoLoudReadSamplesFailedToSeekPcmCppException`, `SoLoudReadSamplesFailedToReadPcmFramesCppException`. Wrap the call in try/catch and treat failure as "unsupported/corrupt file".
