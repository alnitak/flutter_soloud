import 'dart:ui';

import 'package:flutter/material.dart';
import 'package:flutter_soloud_example/controls.dart';
import 'package:flutter_soloud_example/hello_flutter.dart';
import 'package:flutter_soloud_example/page1.dart';
import 'package:flutter_soloud_example/page2.dart';
import 'package:flutter_soloud_example/page3.dart';
import 'package:flutter_soloud_example/page4.dart';
import 'package:flutter_soloud_example/page5.dart';

void main() {
  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      themeMode: ThemeMode.dark,
      darkTheme: ThemeData.dark(),
      scrollBehavior: const MaterialScrollBehavior().copyWith(
          dragDevices: {
            PointerDeviceKind.mouse,
            PointerDeviceKind.touch,
            PointerDeviceKind.stylus,
            PointerDeviceKind.unknown,
          },
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
      length: 6,
      initialIndex: 1,
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
                    Tab(text: 'Spinning audio'),
                    Tab(text: 'wave form'),
                  ],
                ),
              ),
              SizedBox(height: 8),
              Expanded(
                child: TabBarView(
                  physics: NeverScrollableScrollPhysics(),
                  children: [
                    HelloFlutterSoLoud(),
                    Page1(),
                    Page2(),
                    Page3(),
                    Page4(),
                    Page5(),
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
