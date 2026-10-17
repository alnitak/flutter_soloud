// ignore_for_file: experimental_member_use

import 'dart:async';
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

/// Example 4: Desktop VST3 Plugin Rack (Audio Effects, Synths & Native GUI)
///
/// Demonstrates:
/// - Scanning `.vst3` bundles (`Vst3Host.scanBundle`).
/// - Attaching a VST3 Audio Effect (`Fx`) to a Global `CustomFilterSlot`
///   (`soloud.filters.customFilters[0]`) with dry/wet control and C++ parameter
///   fades (`mapToSlotParameter`).
/// - Loading a VST3 Virtual Instrument (`Instrument|Synth`) as a playable
///   SoLoud `AudioSource` and triggering real-time MIDI notes (`noteOn` /
///   `noteOff`).
/// - Opening the plugin's official native desktop editor GUI window
///   (`openEditorWindow`) and observing live parameter edits via
///   `onGuiParameterChanged`.
/// - Saving and restoring binary preset states (`saveState` / `restoreState`).
///
/// Note: Requires `enable_vst3: true` under
/// `hooks.user_defines.flutter_soloud` in the app's `pubspec.yaml`.
void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await SoLoud.instance.init(sampleRate: 48000, bufferSize: 256);
  runApp(const MaterialApp(home: Vst3PluginRackExample()));
}

class Vst3PluginRackExample extends StatefulWidget {
  const Vst3PluginRackExample({super.key});

  @override
  State<Vst3PluginRackExample> createState() => _Vst3PluginRackExampleState();
}

class _Vst3PluginRackExampleState extends State<Vst3PluginRackExample> {
  final SoLoud _soloud = SoLoud.instance;
  final Vst3Host _vstHost = Vst3Host();
  final TextEditingController _pathController = TextEditingController(
    text:
        '/Volumes/NVME/Users/deimos/Library/Audio/Plug-Ins/VST3/flutter_reverb.vst3',
  );

  Vst3EffectPlugin? _loadedEffect;
  Vst3InstrumentPlugin? _loadedSynth;
  String _loadedPluginName = 'VST3 Plugin';
  AudioSource? _testSound;
  SoundHandle? _testToneHandle;
  Uint8List? _savedPreset;
  StreamSubscription<Vst3ParamEditEvent>? _guiSub;
  String _statusMessage = 'Ready.';
  double _effectWet = 0.8;
  bool _latchNotes = false;
  final Set<int> _latchedPitches = <int>{};

  @override
  void dispose() {
    unawaited(_guiSub?.cancel());
    _loadedEffect?.dispose();
    for (final p in _latchedPitches) {
      _loadedSynth?.noteOff(pitch: p);
    }
    _latchedPitches.clear();
    _loadedSynth?.dispose();
    if (_testSound != null) {
      unawaited(_soloud.disposeSource(_testSound!));
    }
    _pathController.dispose();
    super.dispose();
  }

  void _scanAndLoad(String bundlePath, {String? classUid}) {
    if (!_vstHost.isSupported) {
      setState(() {
        _statusMessage =
            'VST3 hosting is disabled. Set enable_vst3: true under '
            'hooks.user_defines.flutter_soloud in pubspec.yaml.';
      });
      return;
    }

    try {
      final classes = _vstHost.scanBundle(bundlePath);
      if (classes.isEmpty) {
        setState(() {
          _statusMessage = 'No VST3 audio classes found in $bundlePath';
        });
        return;
      }

      unawaited(_guiSub?.cancel());
      _loadedEffect?.dispose();
      _loadedEffect = null;
      for (final p in _latchedPitches) {
        _loadedSynth?.noteOff(pitch: p);
      }
      _latchedPitches.clear();
      _loadedSynth?.dispose();
      _loadedSynth = null;

      final targetClass = classUid != null
          ? classes.firstWhere(
              (c) => c.classUid == classUid,
              orElse: () => classes.first,
            )
          : classes.first;

      if (targetClass.isInstrument) {
        final synth = _vstHost.loadInstrument(
          bundlePath: bundlePath,
          classUid: targetClass.classUid,
        )..setTransport(bpm: 120);
        _soloud.play(synth.audioSource, volume: 0.7);
        _guiSub = synth.onGuiParameterChanged.listen((evt) {
          final disp = synth.getParameterDisplay(evt.paramId);
          setState(() {
            _statusMessage = 'GUI edited param ${evt.paramId} -> $disp';
          });
        });
        setState(() {
          _loadedSynth = synth;
          _loadedPluginName = targetClass.name;
          _statusMessage = 'Loaded VST3 Instrument: ${targetClass.name} '
              '(${synth.parameters.length} parameters)';
        });
      } else {
        final slot = _soloud.filters.customFilters[0];
        final effect = _vstHost.loadEffect(
          bundlePath: bundlePath,
          classUid: targetClass.classUid,
          slot: slot,
        )..setTransport(bpm: 120);
        slot.wet.value = _effectWet;
        _guiSub = effect.onGuiParameterChanged.listen((evt) {
          final disp = effect.getParameterDisplay(evt.paramId);
          setState(() {
            _statusMessage = 'GUI edited param ${evt.paramId} -> $disp';
          });
        });
        setState(() {
          _loadedEffect = effect;
          _loadedPluginName = targetClass.name;
          _statusMessage = 'Loaded VST3 Effect: ${targetClass.name} '
              '(${effect.parameters.length} parameters)';
        });
      }
    } on Object catch (e) {
      setState(() {
        _statusMessage = 'Error loading VST3 bundle: $e';
      });
    }
  }

  List<_DiscoveredPluginEntry> _scanInstalledPlugins() {
    final searchDirs = <String>{..._vstHost.standardSearchPaths};
    if (Platform.isMacOS) {
      final home = Platform.environment['HOME'];
      if (home != null && home.isNotEmpty) {
        searchDirs.add('$home/Library/Audio/Plug-Ins/VST3');
      }
      final user = Platform.environment['USER'];
      if (user != null && user.isNotEmpty) {
        searchDirs.add('/Users/$user/Library/Audio/Plug-Ins/VST3');
      }
    }

    final discoveredBundles = <String, String>{}; // bundlePath -> searchDir
    for (final dirPath in searchDirs) {
      final dir = Directory(dirPath);
      if (!dir.existsSync()) continue;
      _findBundles(dir, 3, dirPath, discoveredBundles);
    }

    final entries = <_DiscoveredPluginEntry>[];
    final home = Platform.environment['HOME'] ?? '';

    for (final item in discoveredBundles.entries) {
      final bundlePath = item.key;
      final searchDir = item.value;
      final bundleName = bundlePath.split(Platform.pathSeparator).last;
      final isUser = (home.isNotEmpty && bundlePath.startsWith(home)) ||
          bundlePath.contains('/Users/');

      var classes = const <Vst3ClassInfo>[];
      if (_vstHost.isSupported) {
        try {
          classes = _vstHost.scanBundle(bundlePath);
        } catch (_) {}
      }

      if (classes.isEmpty) {
        entries.add(
          _DiscoveredPluginEntry(
            bundlePath: bundlePath,
            bundleName: bundleName,
            classInfo: null,
            searchDirectory: searchDir,
            isUserLocation: isUser,
          ),
        );
      } else {
        for (final c in classes) {
          entries.add(
            _DiscoveredPluginEntry(
              bundlePath: bundlePath,
              bundleName: bundleName,
              classInfo: c,
              searchDirectory: searchDir,
              isUserLocation: isUser,
            ),
          );
        }
      }
    }

    entries.sort((a, b) {
      if (a.isInstrument != b.isInstrument) {
        return a.isInstrument ? -1 : 1;
      }
      return a.displayName.toLowerCase().compareTo(b.displayName.toLowerCase());
    });

    return entries;
  }

  void _findBundles(
    Directory dir,
    int maxDepth,
    String rootSearchDir,
    Map<String, String> out,
  ) {
    if (maxDepth <= 0 || !dir.existsSync()) return;
    try {
      final entities = dir.listSync();
      for (final entity in entities) {
        final path = entity.path;
        final base = path.split(Platform.pathSeparator).last;
        if (base.startsWith('.')) continue;

        if (path.toLowerCase().endsWith('.vst3')) {
          out.putIfAbsent(path, () => rootSearchDir);
        } else if (entity is Directory) {
          _findBundles(entity, maxDepth - 1, rootSearchDir, out);
        }
      }
    } catch (_) {}
  }

  Future<void> _showPluginPickerDialog() async {
    if (!_vstHost.isSupported) {
      setState(() {
        _statusMessage =
            'VST3 hosting is disabled. Set enable_vst3: true under '
            'hooks.user_defines.flutter_soloud in pubspec.yaml.';
      });
      return;
    }

    final plugins = _scanInstalledPlugins();

    if (!mounted) return;

    if (plugins.isEmpty) {
      await showDialog<void>(
        context: context,
        builder: (ctx) => AlertDialog(
          title: const Text('No VST3 Plugins Found'),
          content: Text(
            'No .vst3 plugins were found in standard search paths:\n\n'
            '${_vstHost.standardSearchPaths.map((p) => '• $p').join('\n')}\n\n'
            'Place your .vst3 plugins in one of these directories or enter a '
            'custom path manually.',
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(ctx),
              child: const Text('OK'),
            ),
          ],
        ),
      );
      return;
    }

    await showDialog<void>(
      context: context,
      builder: (ctx) => _Vst3PluginPickerDialog(
        plugins: plugins,
        onSelected: (entry) {
          _pathController.text = entry.bundlePath;
          _scanAndLoad(
            entry.bundlePath,
            classUid: entry.classInfo?.classUid,
          );
        },
      ),
    );
  }

  Future<void> _toggleTestTone() async {
    if (_testToneHandle != null &&
        _soloud.getIsValidVoiceHandle(_testToneHandle!)) {
      await _soloud.stop(_testToneHandle!);
      setState(() => _testToneHandle = null);
      return;
    }
    _testSound ??= await _soloud.loadAsset('assets/audio/sample-MP3.mp3');
    final handle = _soloud.play(_testSound!, volume: 0.4, looping: true);
    setState(() => _testToneHandle = handle);
  }

  Vst3PluginBase? get _activePlugin => _loadedEffect ?? _loadedSynth;

  @override
  Widget build(BuildContext context) {
    final plugin = _activePlugin;
    return Scaffold(
      appBar: AppBar(title: const Text('Desktop VST3 Plugin Rack')),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          Card(
            child: Padding(
              padding: const EdgeInsets.all(12),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'VST3 Supported: ${_vstHost.isSupported} | '
                    'Search Paths: ${_vstHost.standardSearchPaths.join(', ')}',
                    style: Theme.of(context).textTheme.bodySmall,
                  ),
                  const SizedBox(height: 8),
                  TextField(
                    controller: _pathController,
                    decoration: const InputDecoration(
                      labelText: '.vst3 Bundle Path',
                      border: OutlineInputBorder(),
                    ),
                  ),
                  const SizedBox(height: 8),
                  Wrap(
                    spacing: 8,
                    runSpacing: 8,
                    children: [
                      ElevatedButton.icon(
                        onPressed: _showPluginPickerDialog,
                        icon: const Icon(Icons.manage_search),
                        label: const Text('Scan Installed Plugins...'),
                      ),
                      OutlinedButton.icon(
                        onPressed: () =>
                            _scanAndLoad(_pathController.text.trim()),
                        icon: const Icon(Icons.input),
                        label: const Text('Load Path'),
                      ),
                      ElevatedButton.icon(
                        onPressed: _toggleTestTone,
                        icon: Icon(
                          _testToneHandle != null
                              ? Icons.stop
                              : Icons.play_arrow,
                        ),
                        label: Text(
                          _testToneHandle != null ? 'Stop Sound' : 'Play Sound',
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 8),
                  Text(_statusMessage),
                ],
              ),
            ),
          ),
          if (plugin != null) ...[
            const SizedBox(height: 12),
            Card(
              child: Padding(
                padding: const EdgeInsets.all(12),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Wrap(
                      spacing: 8,
                      runSpacing: 8,
                      children: [
                        if (plugin.hasEditor)
                          ElevatedButton.icon(
                            onPressed: () {
                              if (plugin.isEditorOpen) {
                                plugin.closeEditorWindow();
                              } else {
                                plugin.openEditorWindow(
                                  title: _loadedPluginName,
                                );
                              }
                              setState(() {});
                            },
                            icon: const Icon(Icons.open_in_new),
                            label: Text(
                              plugin.isEditorOpen
                                  ? 'Close Native VST3 GUI'
                                  : 'Open Native VST3 GUI',
                            ),
                          ),
                        OutlinedButton(
                          onPressed: () {
                            _savedPreset = plugin.saveState();
                            setState(() {
                              final len = _savedPreset!.length;
                              _statusMessage = 'Saved preset ($len bytes)';
                            });
                          },
                          child: const Text('Save State'),
                        ),
                        OutlinedButton(
                          onPressed: _savedPreset == null
                              ? null
                              : () {
                                  plugin.restoreState(_savedPreset!);
                                  setState(() {
                                    _statusMessage = 'Restored preset state.';
                                  });
                                },
                          child: const Text('Restore State'),
                        ),
                      ],
                    ),
                    if (_loadedEffect != null) ...[
                      const SizedBox(height: 8),
                      Row(
                        children: [
                          const Text('Slot 0 Dry/Wet:'),
                          Expanded(
                            child: Slider(
                              value: _effectWet,
                              onChanged: (v) {
                                setState(() => _effectWet = v);
                                _soloud.filters.customFilters[0].wet.value = v;
                              },
                            ),
                          ),
                          Text(_effectWet.toStringAsFixed(2)),
                        ],
                      ),
                    ],
                    if (_loadedSynth != null) ...[
                      const SizedBox(height: 8),
                      Wrap(
                        spacing: 8,
                        runSpacing: 8,
                        crossAxisAlignment: WrapCrossAlignment.center,
                        children: [
                          for (final note in const [
                            (60, 'C4'),
                            (63, 'Eb4'),
                            (67, 'G4'),
                            (72, 'C5'),
                          ])
                            Listener(
                              behavior: HitTestBehavior.opaque,
                              onPointerDown: (_) {
                                if (!_latchedPitches.contains(note.$1)) {
                                  _loadedSynth!.noteOn(
                                    pitch: note.$1,
                                    velocity: 0.85,
                                  );
                                  if (_latchNotes) {
                                    setState(() {
                                      _latchedPitches.add(note.$1);
                                    });
                                  }
                                } else {
                                  _loadedSynth!.noteOff(pitch: note.$1);
                                  setState(() {
                                    _latchedPitches.remove(note.$1);
                                  });
                                }
                              },
                              onPointerUp: (_) {
                                if (!_latchNotes) {
                                  _loadedSynth!.noteOff(pitch: note.$1);
                                }
                              },
                              onPointerCancel: (_) {
                                if (!_latchNotes) {
                                  _loadedSynth!.noteOff(pitch: note.$1);
                                }
                              },
                              child: Chip(
                                avatar: _latchedPitches.contains(note.$1)
                                    ? const Icon(
                                        Icons.volume_up,
                                        size: 16,
                                        color: Colors.deepPurple,
                                      )
                                    : null,
                                backgroundColor:
                                    _latchedPitches.contains(note.$1)
                                        ? Colors.deepPurple
                                            .withValues(alpha: 0.25)
                                        : null,
                                label: Text(
                                  _latchedPitches.contains(note.$1)
                                      ? 'Playing ${note.$2}'
                                      : 'Hold ${note.$2}',
                                  style: TextStyle(
                                    fontWeight:
                                        _latchedPitches.contains(note.$1)
                                            ? FontWeight.bold
                                            : FontWeight.normal,
                                  ),
                                ),
                              ),
                            ),
                          InkWell(
                            borderRadius: BorderRadius.circular(8),
                            onTap: () {
                              setState(() {
                                _latchNotes = !_latchNotes;
                                if (!_latchNotes) {
                                  for (final p in _latchedPitches) {
                                    _loadedSynth?.noteOff(pitch: p);
                                  }
                                  _latchedPitches.clear();
                                }
                              });
                            },
                            child: Padding(
                              padding: const EdgeInsets.symmetric(
                                horizontal: 6,
                                vertical: 2,
                              ),
                              child: Row(
                                mainAxisSize: MainAxisSize.min,
                                children: [
                                  Checkbox(
                                    value: _latchNotes,
                                    onChanged: (val) {
                                      setState(() {
                                        _latchNotes = val ?? false;
                                        if (!_latchNotes) {
                                          for (final p in _latchedPitches) {
                                            _loadedSynth?.noteOff(pitch: p);
                                          }
                                          _latchedPitches.clear();
                                        }
                                      });
                                    },
                                  ),
                                  const Text('Hold continuously'),
                                ],
                              ),
                            ),
                          ),
                          if (_latchedPitches.isNotEmpty)
                            OutlinedButton.icon(
                              onPressed: () {
                                for (final p in _latchedPitches) {
                                  _loadedSynth?.noteOff(pitch: p);
                                }
                                setState(_latchedPitches.clear);
                              },
                              icon: const Icon(Icons.stop, size: 16),
                              label: const Text('Stop All Notes'),
                              style: OutlinedButton.styleFrom(
                                visualDensity: VisualDensity.compact,
                              ),
                            ),
                        ],
                      ),
                    ],
                    const Divider(),
                    Text(
                      'Parameters (showing first '
                      '${plugin.parameters.take(12).length} of '
                      '${plugin.parameters.length}):',
                      style: Theme.of(context).textTheme.titleSmall,
                    ),
                    for (final param in plugin.parameters.take(12))
                      Row(
                        children: [
                          SizedBox(
                            width: 180,
                            child: Text(
                              param.title,
                              overflow: TextOverflow.ellipsis,
                            ),
                          ),
                          Expanded(
                            child: Slider(
                              value: plugin
                                  .getParameterNormalized(param.paramId)
                                  .clamp(0.0, 1.0),
                              onChanged: (v) {
                                plugin.setParameterNormalized(param.paramId, v);
                                setState(() {});
                              },
                            ),
                          ),
                          SizedBox(
                            width: 120,
                            child: Text(
                              '${plugin.getParameterDisplay(param.paramId)} '
                              '${param.units}',
                              overflow: TextOverflow.ellipsis,
                            ),
                          ),
                        ],
                      ),
                  ],
                ),
              ),
            ),
          ],
        ],
      ),
    );
  }
}

class _DiscoveredPluginEntry {
  const _DiscoveredPluginEntry({
    required this.bundlePath,
    required this.bundleName,
    required this.classInfo,
    required this.searchDirectory,
    required this.isUserLocation,
  });

  final String bundlePath;
  final String bundleName;
  final Vst3ClassInfo? classInfo;
  final String searchDirectory;
  final bool isUserLocation;

  String get displayName => classInfo?.name ?? bundleName;
  bool get isInstrument => classInfo?.isInstrument ?? false;
  bool get isEffect => classInfo?.isEffect ?? true;
  String get category =>
      classInfo?.subCategories ?? (isInstrument ? 'Instrument' : 'Fx');
}

class _Vst3PluginPickerDialog extends StatefulWidget {
  const _Vst3PluginPickerDialog({
    required this.plugins,
    required this.onSelected,
  });

  final List<_DiscoveredPluginEntry> plugins;
  final ValueChanged<_DiscoveredPluginEntry> onSelected;

  @override
  State<_Vst3PluginPickerDialog> createState() =>
      _Vst3PluginPickerDialogState();
}

class _Vst3PluginPickerDialogState extends State<_Vst3PluginPickerDialog> {
  String _filter = '';
  int _categoryIndex = 0; // 0: All, 1: Synths, 2: Effects

  @override
  Widget build(BuildContext context) {
    final filtered = widget.plugins.where((p) {
      if (_categoryIndex == 1 && !p.isInstrument) return false;
      if (_categoryIndex == 2 && !p.isEffect) return false;
      if (_filter.isEmpty) return true;
      final q = _filter.toLowerCase();
      return p.displayName.toLowerCase().contains(q) ||
          p.bundleName.toLowerCase().contains(q) ||
          p.category.toLowerCase().contains(q) ||
          p.bundlePath.toLowerCase().contains(q);
    }).toList();

    final synthsCount = widget.plugins.where((p) => p.isInstrument).length;
    final effectsCount = widget.plugins.where((p) => p.isEffect).length;

    return AlertDialog(
      title: Row(
        children: [
          const Icon(Icons.extension),
          const SizedBox(width: 8),
          const Text('Installed VST3 Plugins'),
          const Spacer(),
          Chip(
            label: Text('${widget.plugins.length} available'),
            visualDensity: VisualDensity.compact,
          ),
        ],
      ),
      content: SizedBox(
        width: 600,
        height: 480,
        child: Column(
          children: [
            TextField(
              decoration: InputDecoration(
                prefixIcon: const Icon(Icons.search),
                hintText: 'Filter by plugin name or category...',
                border: const OutlineInputBorder(),
                isDense: true,
                suffixIcon: _filter.isNotEmpty
                    ? IconButton(
                        icon: const Icon(Icons.clear),
                        onPressed: () => setState(() => _filter = ''),
                      )
                    : null,
              ),
              onChanged: (v) => setState(() => _filter = v.trim()),
            ),
            const SizedBox(height: 8),
            Row(
              children: [
                ChoiceChip(
                  label: Text('All (${widget.plugins.length})'),
                  selected: _categoryIndex == 0,
                  onSelected: (s) {
                    if (s) setState(() => _categoryIndex = 0);
                  },
                ),
                const SizedBox(width: 8),
                ChoiceChip(
                  label: Text('Synths ($synthsCount)'),
                  selected: _categoryIndex == 1,
                  onSelected: (s) {
                    if (s) setState(() => _categoryIndex = 1);
                  },
                ),
                const SizedBox(width: 8),
                ChoiceChip(
                  label: Text('Effects ($effectsCount)'),
                  selected: _categoryIndex == 2,
                  onSelected: (s) {
                    if (s) setState(() => _categoryIndex = 2);
                  },
                ),
              ],
            ),
            const SizedBox(height: 8),
            Expanded(
              child: filtered.isEmpty
                  ? Center(
                      child: Text(
                        'No plugins match "$_filter".',
                        style: Theme.of(context).textTheme.bodyMedium,
                      ),
                    )
                  : ListView.separated(
                      itemCount: filtered.length,
                      separatorBuilder: (_, __) => const Divider(height: 1),
                      itemBuilder: (ctx, i) {
                        final item = filtered[i];
                        final isSynth = item.isInstrument;

                        return ListTile(
                          contentPadding: const EdgeInsets.symmetric(
                            horizontal: 8,
                            vertical: 4,
                          ),
                          leading: CircleAvatar(
                            backgroundColor: isSynth
                                ? Colors.deepPurple.withValues(alpha: 0.15)
                                : Colors.teal.withValues(alpha: 0.15),
                            child: Icon(
                              isSynth ? Icons.piano : Icons.tune,
                              color: isSynth ? Colors.deepPurple : Colors.teal,
                            ),
                          ),
                          title: Text(
                            item.displayName,
                            style: const TextStyle(
                              fontWeight: FontWeight.bold,
                            ),
                          ),
                          subtitle: Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: [
                              Text(
                                '${item.bundleName} • ${item.category}',
                                style: const TextStyle(fontSize: 12),
                              ),
                              Text(
                                item.bundlePath,
                                style: TextStyle(
                                  fontSize: 10,
                                  color: Theme.of(context)
                                      .textTheme
                                      .bodySmall
                                      ?.color,
                                ),
                                maxLines: 1,
                                overflow: TextOverflow.ellipsis,
                              ),
                            ],
                          ),
                          trailing: Wrap(
                            spacing: 4,
                            children: [
                              Chip(
                                label: Text(
                                  isSynth ? 'Synth' : 'Effect',
                                  style: TextStyle(
                                    fontSize: 11,
                                    fontWeight: FontWeight.w600,
                                    color: isSynth
                                        ? Colors.deepPurple
                                        : Colors.teal,
                                  ),
                                ),
                                padding: EdgeInsets.zero,
                                visualDensity: VisualDensity.compact,
                              ),
                              Chip(
                                label: Text(
                                  item.isUserLocation ? 'User' : 'System',
                                  style: const TextStyle(fontSize: 11),
                                ),
                                padding: EdgeInsets.zero,
                                visualDensity: VisualDensity.compact,
                              ),
                            ],
                          ),
                          onTap: () {
                            Navigator.pop(ctx);
                            widget.onSelected(item);
                          },
                        );
                      },
                    ),
            ),
          ],
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(context),
          child: const Text('Cancel'),
        ),
      ],
    );
  }
}
