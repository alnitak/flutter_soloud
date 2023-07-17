import 'package:flutter/material.dart';
import 'package:flutter_soloud_example/controls.dart';
import 'package:flutter_soloud_example/page2.dart';

import 'package:flutter_soloud_example/page1.dart';

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
      length: 2,
      child: Scaffold(
        body: Column(
          children: [
            Controls(),
            
            SizedBox(
                height: 40,
                child: TabBar(
                  isScrollable: true,
                  tabs: [
                    Tab(text: 'visualizer'),
                    Tab(text: 'test'),
                  ],
                ),
              ),

              Expanded(
                child: TabBarView(
                  physics: NeverScrollableScrollPhysics(),
                  children: [
                    Page1(),
                    Page2(),
                  ],
                ),
              ),
          ],
        ),
      ),
    );
  }
}
