import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

void main() {
  runApp(const WavPlayerApp());
}

class WavPlayerApp extends StatelessWidget {
  const WavPlayerApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'flutter_soloud 4.0.11',
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: Colors.teal),
      ),
      home: const WavPlayerPage(packageVersion: '^4.0.11'),
    );
  }
}

class WavPlayerPage extends StatefulWidget {
  const WavPlayerPage({super.key, required this.packageVersion});

  final String packageVersion;

  @override
  State<WavPlayerPage> createState() => _WavPlayerPageState();
}

class _WavPlayerPageState extends State<WavPlayerPage> {
  late final Future<AudioSource> _loadWavFuture;

  @override
  void initState() {
    super.initState();
    _loadWavFuture = _initAndLoad();
  }

  Future<AudioSource> _initAndLoad() async {
    await SoLoud.instance.init();
    return SoLoud.instance.loadAsset('assets/example.wav');
  }

  Future<void> _play() async {
    final source = await _loadWavFuture;
    SoLoud.instance.play(source);
  }

  @override
  void dispose() {
    SoLoud.instance.deinit();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: Text('flutter_soloud ${widget.packageVersion}')),
      body: Center(
        child: FutureBuilder<AudioSource>(
          future: _loadWavFuture,
          builder: (context, snapshot) {
            final loading = snapshot.connectionState != ConnectionState.done;
            final error = snapshot.error;

            return Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                FilledButton.icon(
                  onPressed: loading || error != null ? null : _play,
                  icon: const Icon(Icons.play_arrow),
                  label: const Text('Play WAV'),
                ),
                const SizedBox(height: 16),
                if (loading) const Text('Loading WAV...'),
                if (error != null) Text('Error: $error'),
              ],
            );
          },
        ),
      ),
    );
  }
}
