import 'package:flutter/material.dart';
import 'package:flutter_soloud_example/controls.dart';
import 'package:flutter_soloud_example/hello_flutter.dart';
import 'package:flutter_soloud_example/page1.dart';
import 'package:flutter_soloud_example/page2.dart';

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
      home: const MyHomePage(),
    );
  }
}

class MyHomePage extends StatelessWidget {
  const MyHomePage({super.key});

  @override
  Widget build(BuildContext context) {
    return const DefaultTabController(
      length: 3,
      initialIndex: 1,
      child: SafeArea(
        child: Scaffold(
          body: Column(
            children: [
              Controls(),
              TabBar(
                isScrollable: true,
                tabs: [
                  Tab(text: 'hello world!'),
                  Tab(text: 'visualizer'),
                  Tab(text: 'multi track'),
                ],
              ),
              SizedBox(height: 8),
              Expanded(
                child: TabBarView(
                  physics: NeverScrollableScrollPhysics(),
                  children: [
                    HelloFlutterSoLoud(),
                    Page1(),
                    Page2(),
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
