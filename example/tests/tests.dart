import 'dart:async';
import 'dart:ui';

import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

import 'tests/all_tests.dart';

enum TestStatus {
  none,
  passed,
  failed,
  running,
}

class _Test {
  _Test({
    required this.entry,
    // ignore: unused_element_parameter
    this.status = TestStatus.none,
  });

  final TestEntry entry;
  TestStatus status;
}

/// A GUI for tests.
///
/// Run this with `flutter run tests/tests.dart`.
void main() {
  WidgetsFlutterBinding.ensureInitialized();

  runApp(
    MaterialApp(
      themeMode: ThemeMode.dark,
      darkTheme: ThemeData.dark(useMaterial3: true),
      scrollBehavior: const MaterialScrollBehavior().copyWith(
        // enable mouse dragging
        dragDevices: PointerDeviceKind.values.toSet(),
      ),
      home: const Padding(
        padding: EdgeInsets.all(8),
        child: MyHomePage(),
      ),
    ),
  );
}

class MyHomePage extends StatefulWidget {
  const MyHomePage({super.key});

  @override
  State<MyHomePage> createState() => _MyHomePageState();
}

class _MyHomePageState extends State<MyHomePage> {
  final output = StringBuffer();
  final textEditingController = TextEditingController();
  late final List<_Test> tests;
  TestEntry? selectedTest;
  bool isRunningAll = false;

  @override
  void initState() {
    super.initState();
    tests = allTests.map((e) => _Test(entry: e)).toList();
    tests.sort((a, b) => a.entry.name.compareTo(b.entry.name));
    selectedTest = tests.first.entry;
  }

  @override
  void dispose() {
    textEditingController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return SafeArea(
      child: Scaffold(
        body: Column(
          children: [
            _buildControls(),
            const SizedBox(height: 8),
            Expanded(
              child: Stack(
                children: [
                  TextField(
                    controller: textEditingController,
                    style: const TextStyle(color: Colors.black, fontSize: 12),
                    expands: true,
                    maxLines: null,
                    readOnly: true,
                    decoration: const InputDecoration(
                      fillColor: Colors.white,
                      filled: true,
                    ),
                  ),
                  Align(
                    alignment: Alignment.topRight,
                    child: Padding(
                      padding: const EdgeInsets.all(8),
                      child: ColoredBox(
                        color: Colors.black26,
                        child: IconButton(
                          color: Colors.black,
                          icon: const Icon(Icons.restore),
                          onPressed: () {
                            textEditingController.clear();
                            output.clear();
                          },
                        ),
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildControls() {
    return Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        // Run All button
        SizedBox(
          width: double.infinity,
          child: FilledButton.icon(
            onPressed: isRunningAll ? null : _runAllTests,
            icon: isRunningAll
                ? const SizedBox(
                    width: 18,
                    height: 18,
                    child: CircularProgressIndicator(strokeWidth: 2),
                  )
                : const Icon(Icons.play_arrow),
            label: const Text('Run All Tests'),
          ),
        ),
        const SizedBox(height: 12),
        // Single test selector
        Row(
          children: [
            Expanded(
              child: InputDecorator(
                decoration: const InputDecoration(
                  labelText: 'Select a single test',
                  border: OutlineInputBorder(),
                  contentPadding: EdgeInsets.symmetric(horizontal: 12),
                ),
                child: DropdownButtonHideUnderline(
                  child: DropdownButton<TestEntry>(
                    value: selectedTest,
                    isDense: true,
                    isExpanded: true,
                    items: tests.map((test) {
                      return DropdownMenuItem<TestEntry>(
                        value: test.entry,
                        child: Row(
                          children: [
                            _StatusDot(status: test.status),
                            const SizedBox(width: 8),
                            Expanded(child: Text(test.entry.name)),
                          ],
                        ),
                      );
                    }).toList(),
                    onChanged: isRunningAll
                        ? null
                        : (value) {
                            if (value != null) {
                              setState(() {
                                selectedTest = value;
                              });
                            }
                          },
                  ),
                ),
              ),
            ),
            const SizedBox(width: 8),
            ElevatedButton.icon(
              onPressed: isRunningAll || selectedTest == null
                  ? null
                  : () => _runSingleTest(selectedTest!),
              icon: const Icon(Icons.play_arrow),
              label: const Text('Run'),
            ),
          ],
        ),
        const SizedBox(height: 8),
        // Summary chips
        Wrap(
          spacing: 8,
          runSpacing: 4,
          children: [
            _buildChip(
              label: 'Total: ${tests.length}',
              color: Colors.grey,
            ),
            _buildChip(
              label: 'Passed: '
                  '${tests.where((t) => t.status == TestStatus.passed).length}',
              color: Colors.green,
            ),
            _buildChip(
              label: 'Failed: '
                  '${tests.where((t) => t.status == TestStatus.failed).length}',
              color: Colors.red,
            ),
          ],
        ),
      ],
    );
  }

  Widget _buildChip({required String label, required Color color}) {
    return Chip(
      label: Text(label, style: const TextStyle(fontSize: 12)),
      backgroundColor: color.withValues(alpha: 0.15),
      side: BorderSide(color: color.withValues(alpha: 0.5)),
      padding: EdgeInsets.zero,
      visualDensity: VisualDensity.compact,
    );
  }

  Future<void> _runAllTests() async {
    setState(() {
      isRunningAll = true;
      output.clear();
      for (final test in tests) {
        test.status = TestStatus.none;
      }
    });

    for (var i = 0; i < tests.length; i++) {
      await _runTestByIndex(i);
    }

    setState(() {
      isRunningAll = false;
    });
  }

  Future<void> _runSingleTest(TestEntry entry) async {
    final index = tests.indexWhere((t) => t.entry == entry);
    if (index >= 0) {
      await _runTestByIndex(index);
    }
  }

  Future<void> _runTestByIndex(int index) async {
    tests[index].status = TestStatus.running;
    if (mounted) setState(() {});

    // Ensure clean state before running test
    // (in case previous test didn't clean up properly)
    try {
      if (SoLoud.instance.isInitialized) {
        SoLoud.instance.deinit();
      }
    } catch (_) {
      // Ignore - may not be initialized
    }

    await runZonedGuarded<Future<void>>(
      () async {
        final result = await tests[index].entry.run();
        output
          ..write('===== RUNNING "${tests[index].entry.name}" =====\n')
          ..write(result)
          ..write('===== PASSED! =====\n\n')
          ..writeln();
        tests[index].status = TestStatus.passed;
        _updateOutput();
      },
      (error, stack) {
        // Ensure cleanup even if test failed
        try {
          if (SoLoud.instance.isInitialized) {
            SoLoud.instance.deinit();
          }
        } catch (_) {
          // Ignore cleanup errors
        }

        output
          ..write('== TEST "${tests[index].entry.name}" FAILED with '
              'the following error(s) ==')
          ..writeln()
          ..writeAll([error, stack], '\n\n')
          ..writeln()
          ..writeln();
        tests[index].status = TestStatus.failed;
        _updateOutput();
      },
    );
  }

  void _updateOutput() {
    textEditingController.text = output.toString();
    debugPrint(output.toString());
    if (mounted) setState(() {});
  }
}

class _StatusDot extends StatelessWidget {
  const _StatusDot({required this.status});

  final TestStatus status;

  @override
  Widget build(BuildContext context) {
    final color = switch (status) {
      TestStatus.passed => Colors.green,
      TestStatus.failed => Colors.red,
      TestStatus.running => Colors.yellow,
      TestStatus.none => Colors.transparent,
    };

    return Container(
      width: 10,
      height: 10,
      decoration: BoxDecoration(
        color: color,
        shape: BoxShape.circle,
        border:
            status == TestStatus.none ? Border.all(color: Colors.grey) : null,
      ),
    );
  }
}
