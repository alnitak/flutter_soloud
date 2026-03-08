import 'package:flutter/material.dart';
import 'package:desktop_drop/desktop_drop.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:path/path.dart' as p;

void main() {
  runApp(const DesktopDropDemoApp());
}

class DesktopDropDemoApp extends StatelessWidget {
  const DesktopDropDemoApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Desktop Drop Demo',
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: Colors.blue),
      ),
      home: const DesktopDropPage(),
    );
  }
}

class DesktopDropPage extends StatefulWidget {
  const DesktopDropPage({super.key});

  @override
  State<DesktopDropPage> createState() => _DesktopDropPageState();
}

class _DesktopDropPageState extends State<DesktopDropPage> {
  bool _isDragOver = false;
  final List<DropItem> _files = <DropItem>[];

  final SoLoud _soloud = SoLoud.instance;

  void _onDrop(List<DropItem> files) {
    setState(() {
      _isDragOver = false;
      _files
        ..clear()
        ..addAll(files);
    });
  }

  void _clearFiles() {
    setState(_files.clear);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
        title: const Text('desktop_drop Demo'),
        actions: [
          TextButton(
            onPressed: _files.isEmpty ? null : _clearFiles,
            child: const Text('clear'),
          ),
        ],
      ),
      body: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          children: [
            ElevatedButton(
              onPressed: _soloud.init,
              child: const Text('init'),
            ),
            Expanded(
              child: DropTarget(
                onDragEntered: (_) => setState(() => _isDragOver = true),
                onDragExited: (_) => setState(() => _isDragOver = false),
                onDragDone: (details) => _onDrop(details.files),
                child: AnimatedContainer(
                  duration: const Duration(milliseconds: 120),
                  width: double.infinity,
                  decoration: BoxDecoration(
                    color: _isDragOver
                        ? Colors.blue.withValues(alpha: 0.12)
                        : Colors.grey.withValues(alpha: 0.08),
                    border: Border.all(
                      color: _isDragOver ? Colors.blue : Colors.grey,
                      width: 2,
                    ),
                    borderRadius: BorderRadius.circular(12),
                  ),
                  child: _files.isEmpty
                      ? const Center(
                          child: Text(
                            'Drag the file here\n(Windows desktop support)',
                            textAlign: TextAlign.center,
                          ),
                        )
                      : ListView.separated(
                          padding: const EdgeInsets.all(12),
                          itemCount: _files.length,
                          separatorBuilder: (_, __) => const Divider(height: 1),
                          itemBuilder: (_, i) => ListTile(
                            dense: true,
                            leading: const Icon(
                              Icons.insert_drive_file_outlined,
                            ),
                            title: Text(_files[i].name),
                            onTap: () async {
                              await _soloud.disposeAllSources();
                              final s = await _soloud.loadFile(  
                                _files[i].path,
                                mode: LoadMode.disk,
                              );
                              await _soloud.play(s);
                            },
                          ),
                        ),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

// import 'dart:developer' as dev;

// import 'package:flutter/foundation.dart';
// import 'package:flutter/material.dart';
// import 'package:flutter_soloud/flutter_soloud.dart';
// import 'package:logging/logging.dart';

// void main() async {
//   // The `flutter_soloud` package logs everything
//   // (from severe warnings to fine debug messages)
//   // using the standard `package:logging`.
//   // You can listen to the logs as shown below.
//   Logger.root.level = kDebugMode ? Level.FINE : Level.INFO;
//   Logger.root.onRecord.listen((record) {
//     dev.log(
//       record.message,
//       time: record.time,
//       level: record.level.value,
//       name: record.loggerName,
//       zone: record.zone,
//       error: record.error,
//       stackTrace: record.stackTrace,
//     );
//   });

//   WidgetsFlutterBinding.ensureInitialized();

//   /// Initialize the player.
//   await SoLoud.instance.init();

//   runApp(
//     const MaterialApp(
//       home: HelloFlutterSoLoud(),
//     ),
//   );
// }

// /// Simple usecase of flutter_soloud plugin
// class HelloFlutterSoLoud extends StatefulWidget {
//   const HelloFlutterSoLoud({super.key});

//   @override
//   State<HelloFlutterSoLoud> createState() => _HelloFlutterSoLoudState();
// }

// class _HelloFlutterSoLoudState extends State<HelloFlutterSoLoud> {
//   AudioSource? currentSound;

//   @override
//   void dispose() {
//     SoLoud.instance.deinit();
//     super.dispose();
//   }

//   @override
//   Widget build(BuildContext context) {
//     if (!SoLoud.instance.isInitialized) return const SizedBox.shrink();

//     return Scaffold(
//       body: Center(
//         child: ElevatedButton(
//           onPressed: () async {
//             await SoLoud.instance.disposeAllSources();

//             if (kIsWeb) {
//               /// load the audio file using [LoadMode.disk] (better for the
//               /// Web platform).
//               currentSound = await SoLoud.instance.loadAsset(
//                 'assets/audio/8_bit_mentality.mp3',
//                 mode: LoadMode.disk,
//               );
//             } else {
//               /// load the audio file
//               currentSound = await SoLoud.instance
//                   .loadAsset('assets/audio/8_bit_mentality.mp3');
//             }

//             /// play it
//             await SoLoud.instance.play(currentSound!);
//           },
//           child: const Text(
//             'play asset',
//             textAlign: TextAlign.center,
//           ),
//         ),
//       ),
//     );
//   }
// }
