// ignore_for_file: avoid_print

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

/// Output device example.
///
/// This example uses the default output device and present a dropdown
/// menu to change from all available output devices.
/// All this is made simple just using the `listPlaybackDevices` and
/// `changeDevice` methods.
///
/// To get all output devices use `listPlaybackDevices` which returns
/// a list of `PlaybackDevice`s class. Each items of this class
/// contains the id, whether it's the default device (the one used by the OS)
/// and the name.
/// This method can be called even if the engine has not been initialized.
///
/// At any time it is possible to pass to `changeDevice` a `PlaybackDevice`
/// which will change the output device.
///
/// Note: Android, iOS and Web, only support one output device which is
/// the default.

void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  /// Initialize the player.
  await SoLoud.instance.init();

  runApp(
    const MaterialApp(
      home: HelloFlutterSoLoud(),
    ),
  );
}

/// Simple usecase of flutter_soloud plugin
class HelloFlutterSoLoud extends StatefulWidget {
  const HelloFlutterSoLoud({super.key});

  @override
  State<HelloFlutterSoLoud> createState() => _HelloFlutterSoLoudState();
}

class _HelloFlutterSoLoudState extends State<HelloFlutterSoLoud> {
  late TextEditingController textEditingController;
  late List<PlaybackDevice> devices;
  late PlaybackDevice currentDevice;
  AudioSource? currentSound;
  LinuxAudioBackend selectedBackend = LinuxAudioBackend.auto_;

  @override
  void initState() {
    super.initState();

    devices = SoLoud.instance.listPlaybackDevices();
    assert(devices.isNotEmpty, 'No devices found!');

    currentDevice = devices.firstWhere(
      (d) => d.isDefault,
      orElse: () => devices.first,
    );
    textEditingController = TextEditingController(text: currentDevice.name);
  }

  @override
  void dispose() {
    textEditingController.dispose();
    SoLoud.instance.deinit();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (!SoLoud.instance.isInitialized) return const SizedBox.shrink();

    return Scaffold(
      body: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          spacing: 16,
          children: [
            if (defaultTargetPlatform == TargetPlatform.linux) ...[
              const Text(
                'Linux Audio Backend:',
                style: TextStyle(fontWeight: FontWeight.bold),
              ),
              DropdownMenu<LinuxAudioBackend>(
                initialSelection: selectedBackend,
                onSelected: (backend) async {
                  if (backend == null) return;
                  selectedBackend = backend;
                  await SoLoud.instance.setLinuxAudioBackend(backend);
                  devices = SoLoud.instance.listPlaybackDevices();
                  if (devices.isNotEmpty) {
                    currentDevice = devices.firstWhere(
                      (d) => d.isDefault,
                      orElse: () => devices.first,
                    );
                    textEditingController.text = currentDevice.name;
                  } else {
                    textEditingController.text = 'No devices found';
                  }
                  setState(() {});
                },
                dropdownMenuEntries: const [
                  DropdownMenuEntry(
                    value: LinuxAudioBackend.auto_,
                    label: 'Auto (ALSA -> PulseAudio -> JACK)',
                  ),
                  DropdownMenuEntry(
                    value: LinuxAudioBackend.alsa,
                    label: 'ALSA',
                  ),
                  DropdownMenuEntry(
                    value: LinuxAudioBackend.pulseAudio,
                    label: 'PulseAudio',
                  ),
                  DropdownMenuEntry(
                    value: LinuxAudioBackend.jack,
                    label: 'JACK',
                  ),
                ],
              ),
            ],
            ElevatedButton(
              onPressed: () async {
                devices = SoLoud.instance.listPlaybackDevices();
                final deviceNames = devices.map((d) => d.name).join(', ');
                print('Available devices: $deviceNames');
                setState(() {});
              },
              child: const Text('Refresh available devices'),
            ),
            ElevatedButton(
              onPressed: () async {
                if (currentSound != null) {
                  await SoLoud.instance.disposeAllSources();
                  currentSound = null;
                  print('Playing...');
                  setState(() {});
                } else {
                  currentSound = await SoLoud.instance
                      .loadAsset('assets/audio/8_bit_mentality.mp3');
                  SoLoud.instance.play(
                    currentSound!,
                    looping: true,
                    volume: 0.5,
                  );
                  print('Playing...');
                  setState(() {});
                }
              },
              child: Text(currentSound == null ? 'Play' : 'Stop'),
            ),
            DropdownMenu(
              controller: textEditingController,
              onSelected: (value) async {
                if (value != null && value < devices.length) {
                  await SoLoud.instance.changeDevice(newDevice: devices[value]);
                }
              },
              dropdownMenuEntries: [
                for (var i = 0; i < devices.length; i++)
                  DropdownMenuEntry(
                    value: i,
                    label: '(${devices[i].id}) - ${devices[i].name}',
                  ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}
