// ignore_for_file: experimental_member_use

import 'dart:math';

import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud_example/realtime_audio/piano/piano_keyboard.dart';

/// A docked piano panel designed for auditioning VST3 instruments and synths.
/// A rethinked version of PianoKeyboard from soundfont_kit demo app.
///
/// Features:
/// - Responsive keys layout: shrinks down to 2 octaves (key width down to 10 px
///   or less) on narrow screens and dynamically expands octaves on resize to
///   keep key width <= 35 px.
/// - Full window width and a default height of 200 px.
/// - Physical computer keyboard bindings (QWERTY chromatic octave).
/// - Hold / Latch mode toggle.
/// - Octave shifting controls with keyboard shortcuts.
/// - Stop all voices action.
/// - Help modal detailing all keyboard shortcuts.
class PianoPanel extends StatefulWidget {
  const PianoPanel({
    super.key,
    this.instrument,
    this.title,
    this.height = 200,
    this.minOctaves = 2,
    this.maxOctaves = 7,
    this.maxKeyWidth = 35,
    this.onNoteDown,
    this.onNoteUp,
    this.onStopAll,
  });

  /// The active VST3 instrument to send NoteOn and NoteOff events to.
  final Vst3InstrumentPlugin? instrument;

  /// Optional display title (e.g. plugin name).
  final String? title;

  /// Total height of the docked panel (defaults to 200 px).
  final double height;

  /// Minimum number of octaves to display (defaults to 2).
  final int minOctaves;

  /// Maximum number of octaves to display (defaults to 7).
  final int maxOctaves;

  /// Maximum width of a white key before expanding to another octave
  /// (defaults to 35 px).
  final double maxKeyWidth;

  /// Optional custom callback when note is pressed.
  final void Function(int midiKey, int velocity)? onNoteDown;

  /// Optional custom callback when note is released.
  final void Function(int midiKey)? onNoteUp;

  /// Optional custom callback when all notes are stopped.
  final VoidCallback? onStopAll;

  @override
  State<PianoPanel> createState() => _PianoPanelState();
}

class _PianoPanelState extends State<PianoPanel> {
  int _startOctave = 3; // C3 (MIDI 48)
  final Set<int> _activeKeys = <int>{};
  bool _latchNotes = false;

  bool _isDisposed = false;

  @override
  void didUpdateWidget(PianoPanel oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (widget.instrument != oldWidget.instrument) {
      for (final key in _activeKeys) {
        oldWidget.instrument?.noteOff(pitch: key);
      }
      _activeKeys.clear();
    }
  }

  @override
  void dispose() {
    _isDisposed = true;
    _stopAll(updateUi: false);
    super.dispose();
  }

  void _handleNoteDown(int key, int velocity) {
    if (_latchNotes) {
      if (_activeKeys.contains(key)) {
        _activeKeys.remove(key);
        widget.instrument?.noteOff(pitch: key);
        widget.onNoteUp?.call(key);
      } else {
        _activeKeys.add(key);
        final normVel = (velocity / 127.0).clamp(0.05, 1.0);
        widget.instrument?.noteOn(pitch: key, velocity: normVel);
        widget.onNoteDown?.call(key, velocity);
      }
    } else {
      _activeKeys.add(key);
      final normVel = (velocity / 127.0).clamp(0.05, 1.0);
      widget.instrument?.noteOn(pitch: key, velocity: normVel);
      widget.onNoteDown?.call(key, velocity);
    }
    if (mounted && !_isDisposed) {
      setState(() {});
    }
  }

  void _handleNoteUp(int key) {
    if (!_latchNotes) {
      _activeKeys.remove(key);
      widget.instrument?.noteOff(pitch: key);
      widget.onNoteUp?.call(key);
      if (mounted && !_isDisposed) {
        setState(() {});
      }
    }
  }

  void _stopAll({bool updateUi = true}) {
    for (final key in _activeKeys) {
      widget.instrument?.noteOff(pitch: key);
      widget.onNoteUp?.call(key);
    }
    _activeKeys.clear();
    widget.onStopAll?.call();
    if (updateUi && mounted && !_isDisposed) {
      setState(() {});
    }
  }

  void _shiftOctave(int delta, int maxOctave) {
    if (!mounted || _isDisposed) return;
    setState(() {
      _startOctave = (_startOctave + delta).clamp(1, maxOctave);
    });
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    return Container(
      height: widget.height,
      width: double.infinity,
      decoration: BoxDecoration(
        color: theme.colorScheme.surfaceContainerHigh,
        border: Border(top: BorderSide(color: theme.dividerColor, width: 1.5)),
        boxShadow: [
          BoxShadow(
            color: Colors.black.withValues(alpha: 0.25),
            offset: const Offset(0, -3),
            blurRadius: 6,
          ),
        ],
      ),
      child: LayoutBuilder(
        builder: (context, constraints) {
          final availableWidth = constraints.maxWidth;

          // Responsive octave computation:
          // For N octaves, total white keys = 7 * N + 1.
          // We want availableWidth / (7 * N + 1) <= maxKeyWidth
          // => N >= ((availableWidth / maxKeyWidth) - 1) / 7
          final calculatedOctaves =
              ((availableWidth / widget.maxKeyWidth - 1) / 7).ceil();
          final octaves = calculatedOctaves.clamp(
            widget.minOctaves,
            widget.maxOctaves,
          );
          final keyCount = octaves * 12 + 1;

          // Ensure starting octave stays within valid MIDI bounds (0..127)
          final maxStartOctave = max(1, (127 - keyCount + 1 - 12) ~/ 12);
          final effectiveStartOctave = _startOctave.clamp(1, maxStartOctave);
          final startNote = effectiveStartOctave * 12 + 12;

          return Column(
            children: [
              // Header Controls Bar
              Container(
                height: 38,
                padding: const EdgeInsets.symmetric(horizontal: 10),
                decoration: BoxDecoration(
                  color: theme.colorScheme.surfaceContainerHighest,
                  border: Border(
                    bottom: BorderSide(
                      color: theme.dividerColor.withValues(alpha: 0.5),
                    ),
                  ),
                ),
                child: Row(
                  children: [
                    Icon(
                      Icons.piano,
                      size: 20,
                      color: theme.colorScheme.primary,
                    ),
                    const SizedBox(width: 8),
                    Flexible(
                      child: Text(
                        widget.title ?? 'Virtual MIDI Keyboard',
                        style: theme.textTheme.titleSmall?.copyWith(
                          fontWeight: FontWeight.bold,
                        ),
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                      ),
                    ),
                    const SizedBox(width: 6),
                    Container(
                      padding: const EdgeInsets.symmetric(
                        horizontal: 6,
                        vertical: 2,
                      ),
                      decoration: BoxDecoration(
                        color: theme.colorScheme.surfaceContainer,
                        borderRadius: BorderRadius.circular(4),
                      ),
                      child: Text(
                        '$octaves Octaves ($keyCount keys)',
                        style: theme.textTheme.labelSmall?.copyWith(
                          color: theme.colorScheme.onSurfaceVariant,
                          fontSize: 10.5,
                        ),
                      ),
                    ),
                    const Spacer(),

                    // Octave Shift Buttons
                    IconButton(
                      icon: const Icon(Icons.arrow_left, size: 20),
                      visualDensity: VisualDensity.compact,
                      tooltip: 'Octave Down (Left Shift / Left Arrow)',
                      onPressed: effectiveStartOctave > 1
                          ? () => _shiftOctave(-1, maxStartOctave)
                          : null,
                    ),
                    Padding(
                      padding: const EdgeInsets.symmetric(horizontal: 2),
                      child: Text(
                        'C$effectiveStartOctave',
                        style: theme.textTheme.labelMedium?.copyWith(
                          fontWeight: FontWeight.bold,
                          color: theme.colorScheme.primary,
                        ),
                      ),
                    ),
                    IconButton(
                      icon: const Icon(Icons.arrow_right, size: 20),
                      visualDensity: VisualDensity.compact,
                      tooltip: 'Octave Up (Right Shift / Right Arrow)',
                      onPressed: effectiveStartOctave < maxStartOctave
                          ? () => _shiftOctave(1, maxStartOctave)
                          : null,
                    ),

                    const SizedBox(width: 8),

                    // Hold / Latch Toggle
                    FilterChip(
                      avatar: Icon(
                        _latchNotes ? Icons.lock : Icons.lock_open,
                        size: 14,
                        color: _latchNotes
                            ? theme.colorScheme.onPrimaryContainer
                            : null,
                      ),
                      label: const Text(
                        'Hold / Latch',
                        style: TextStyle(fontSize: 11),
                      ),
                      selected: _latchNotes,
                      padding: EdgeInsets.zero,
                      onSelected: (val) {
                        setState(() {
                          _latchNotes = val;
                          if (!_latchNotes) {
                            _stopAll();
                          }
                        });
                      },
                    ),

                    const SizedBox(width: 8),

                    // Stop All Notes Button
                    OutlinedButton.icon(
                      icon: const Icon(Icons.stop, size: 16),
                      label: const Text(
                        'Stop All',
                        style: TextStyle(fontSize: 11),
                      ),
                      style: OutlinedButton.styleFrom(
                        visualDensity: VisualDensity.compact,
                        padding: const EdgeInsets.symmetric(horizontal: 8),
                      ),
                      onPressed: _activeKeys.isNotEmpty || _latchNotes
                          ? _stopAll
                          : null,
                    ),

                    const SizedBox(width: 4),

                    // Help Dialog Button
                    IconButton(
                      icon: const Icon(Icons.help_outline, size: 18),
                      tooltip: 'Keyboard Shortcuts Help',
                      visualDensity: VisualDensity.compact,
                      onPressed: () => showKeyBindingsHelpDialog(context),
                    ),
                  ],
                ),
              ),

              // Piano Keyboard
              Expanded(
                child: Padding(
                  padding: const EdgeInsets.fromLTRB(4, 3, 4, 4),
                  child: PianoKeyboard(
                    startNote: startNote,
                    keyCount: keyCount,
                    activeKeys: _activeKeys,
                    latchMode: _latchNotes,
                    onNoteDown: _handleNoteDown,
                    onNoteUp: _handleNoteUp,
                    onOctaveShift: (delta) =>
                        _shiftOctave(delta, maxStartOctave),
                    onStopAll: _stopAll,
                  ),
                ),
              ),
            ],
          );
        },
      ),
    );
  }
}

/// Displays a modal help dialog listing all physical keyboard shortcuts
/// and piano key bindings.
void showKeyBindingsHelpDialog(BuildContext context) {
  final theme = Theme.of(context);
  showDialog<void>(
    context: context,
    builder: (context) {
      Widget buildKbdBadge(String text) {
        return Container(
          padding: const EdgeInsets.symmetric(horizontal: 7, vertical: 3),
          decoration: BoxDecoration(
            color: theme.colorScheme.surfaceContainerHighest,
            borderRadius: BorderRadius.circular(4),
            border: Border.all(
              color: theme.colorScheme.outlineVariant,
            ),
            boxShadow: [
              BoxShadow(
                color: Colors.black.withValues(alpha: 0.1),
                offset: const Offset(0, 1.5),
                blurRadius: 1,
              ),
            ],
          ),
          child: Text(
            text,
            style: TextStyle(
              fontFamily: 'monospace',
              fontSize: 12,
              fontWeight: FontWeight.bold,
              color: theme.colorScheme.onSurface,
            ),
          ),
        );
      }

      Widget buildRow(Widget shortcut, String description) {
        return Padding(
          padding: const EdgeInsets.symmetric(vertical: 4),
          child: Row(
            children: [
              SizedBox(
                width: 130,
                child: Align(alignment: Alignment.centerLeft, child: shortcut),
              ),
              const SizedBox(width: 12),
              Expanded(
                child: Text(description, style: theme.textTheme.bodyMedium),
              ),
            ],
          ),
        );
      }

      return AlertDialog(
        icon: const Icon(Icons.keyboard, size: 28),
        title: const Text('Keyboard Shortcuts'),
        content: SingleChildScrollView(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                'Navigation & Octaves',
                style: theme.textTheme.labelLarge?.copyWith(
                  fontWeight: FontWeight.bold,
                  color: theme.colorScheme.primary,
                ),
              ),
              const SizedBox(height: 8),
              buildRow(buildKbdBadge('Space'), 'Play root / C note'),
              buildRow(
                buildKbdBadge('◄ Left / LShift'),
                'Shift octave down',
              ),
              buildRow(
                buildKbdBadge('► Right / RShift'),
                'Shift octave up',
              ),
              buildRow(
                buildKbdBadge('Canc / Delete'),
                'Stop all playing voices',
              ),
              const Divider(height: 24),
              Text(
                'Piano Keys (1 Chromatic Octave)',
                style: theme.textTheme.labelLarge?.copyWith(
                  fontWeight: FontWeight.bold,
                  color: theme.colorScheme.primary,
                ),
              ),
              const SizedBox(height: 8),
              buildRow(
                Wrap(
                  spacing: 3,
                  runSpacing: 3,
                  children: [
                    'A',
                    'S',
                    'D',
                    'F',
                    'G',
                    'H',
                    'J',
                    'K',
                  ].map(buildKbdBadge).toList(),
                ),
                'White keys (C, D, E, F, G, A, B, C)',
              ),
              const SizedBox(height: 4),
              buildRow(
                Wrap(
                  spacing: 3,
                  runSpacing: 3,
                  children: [
                    'W',
                    'E',
                    'T',
                    'Y',
                    'U',
                  ].map(buildKbdBadge).toList(),
                ),
                'Black keys (C#, D#, F#, G#, A#)',
              ),
              const Divider(height: 24),
              Text(
                'Touch / Mouse Dynamics',
                style: theme.textTheme.labelLarge?.copyWith(
                  fontWeight: FontWeight.bold,
                  color: theme.colorScheme.primary,
                ),
              ),
              const SizedBox(height: 4),
              Text(
                'Clicking or touching higher on a key produces a softer '
                'velocity (pianissimo), while clicking lower towards the '
                'key edge triggers full velocity (fortissimo).',
                style: theme.textTheme.bodySmall,
              ),
            ],
          ),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(context).pop(),
            child: const Text('Close'),
          ),
        ],
      );
    },
  );
}
