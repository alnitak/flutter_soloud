import 'dart:developer' as dev;
import 'dart:ui';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud_example/controls.dart';
import 'package:flutter_soloud_example/page_3d_audio.dart';
import 'package:flutter_soloud_example/page_hello_flutter.dart';
import 'package:flutter_soloud_example/page_multi_track.dart';
import 'package:flutter_soloud_example/page_visualizer.dart';
import 'package:flutter_soloud_example/page_waveform.dart';
import 'package:logging/logging.dart';

void main() {
  // The `flutter_soloud` package logs everything
  // (from severe warnings to fine debug messages)
  // using the standard `package:logging`.
  // You can listen to the logs as shown below.
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

  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      themeMode: ThemeMode.dark,
      darkTheme: ThemeData.dark(useMaterial3: true),
      scrollBehavior: const MaterialScrollBehavior().copyWith(
        // enable mouse dragging
        dragDevices: PointerDeviceKind.values.toSet(),
      ),
      home: const MyHomePage(),
    );
  }
}

class MyHomePage extends StatelessWidget {
  const MyHomePage({super.key});

  @override
  Widget build(BuildContext context) {
    return const DefaultTabController(
      length: 5,
      initialIndex: 4,
      child: SafeArea(
        child: Scaffold(
          body: Column(
            children: [
              Controls(),
              SingleChildScrollView(
                scrollDirection: Axis.horizontal,
                child: TabBar(
                  isScrollable: true,
                  tabs: [
                    Tab(text: 'hello world!'),
                    Tab(text: 'visualizer'),
                    Tab(text: 'multi track'),
                    Tab(text: '3D audio'),
                    Tab(text: 'wave form'),
                  ],
                ),
              ),
              SizedBox(height: 8),
              Expanded(
                child: TabBarView(
                  physics: NeverScrollableScrollPhysics(),
                  children: [
                    PageHelloFlutterSoLoud(),
                    PageVisualizer(),
                    PageMultiTrack(),
                    Page3DAudio(),
                    PageWaveform(),
                  ],
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
