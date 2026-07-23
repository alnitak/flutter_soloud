import 'package:flutter/material.dart';

/// A visualizer for a pull-buffer stream's progress.
///
/// Draws a horizontal bar where:
/// - the **black** background represents the total audio duration,
/// - the **red** overlay shows the buffered region,
/// - the **yellow** vertical line / thumb marks the current play position.
///
/// The buffered region is a sliding window.
///
/// The bar can be flashed by passing a non-null [flashColor]. The overlay
/// fades out automatically.
///
/// The bar is draggable; while the user is dragging, the position indicator
/// follows the pointer and the playback head only snaps to the dragged value
/// when the gesture ends.
class PullBufferSeekBar extends StatefulWidget {
  const PullBufferSeekBar({
    required this.duration,
    required this.bufferedStart,
    required this.bufferedEnd,
    required this.position,
    required this.onSeek,
    this.flashColor,
    this.height = 24,
    this.thumbRadius = 8,
    this.showTimeLabels = true,
    super.key,
  });

  /// Total duration of the audio. If unknown, use the largest buffered value
  /// seen so far so the bar grows as data arrives.
  final Duration duration;

  /// End of the buffered region.
  final Duration bufferedEnd;

  /// Start of the buffered region.
  final Duration bufferedStart;

  /// Current playback position.
  final Duration position;

  /// Called when the user finishes a seek gesture.
  final ValueChanged<Duration> onSeek;

  /// Optional color used to briefly flash the buffered region.
  final Color? flashColor;

  /// Height of the bar track.
  final double height;

  /// Radius of the draggable position thumb.
  final double thumbRadius;

  /// Whether to show current/total duration labels beneath the bar.
  final bool showTimeLabels;

  @override
  State<PullBufferSeekBar> createState() => _PullBufferSeekBarState();
}

class _PullBufferSeekBarState extends State<PullBufferSeekBar>
    with SingleTickerProviderStateMixin {
  /// Ratio in [0, 1] while the user is dragging, or `null` when not dragging.
  double? _dragRatio;

  late final AnimationController _flashController = AnimationController(
    vsync: this,
    duration: const Duration(milliseconds: 300),
  );

  Color? _activeFlashColor;

  void _onTapDown(TapDownDetails details, BoxConstraints constraints) {
    final ratio =
        (details.localPosition.dx / constraints.maxWidth).clamp(0.0, 1.0);
    widget.onSeek(
      Duration(
        milliseconds:
            (ratio * widget.duration.inMilliseconds.toDouble()).toInt(),
      ),
    );
  }

  double _ratioFor(Duration value) {
    final total = widget.duration.inMilliseconds;
    if (total <= 0) return 0;
    final ms = value.inMilliseconds.clamp(0, total).toDouble();
    return ms / total;
  }

  @override
  void initState() {
    super.initState();
    _flashController.addStatusListener((status) {
      if (status == AnimationStatus.completed && mounted) {
        setState(() => _activeFlashColor = null);
      }
    });
  }

  @override
  void didUpdateWidget(covariant PullBufferSeekBar oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (widget.flashColor != null &&
        widget.flashColor != oldWidget.flashColor) {
      setState(() => _activeFlashColor = widget.flashColor);
      _flashController
        ..reset()
        ..forward();
    }
  }

  @override
  void dispose() {
    _flashController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final bufferStart = _ratioFor(widget.bufferedStart);
    final bufferEnd = _ratioFor(widget.bufferedEnd);
    final playRatio = _dragRatio ?? _ratioFor(widget.position);

    return LayoutBuilder(
      builder: (context, constraints) {
        final flashOpacity = _flashController.isAnimating
            ? (1 - _flashController.value).clamp(0.0, 1.0)
            : (_activeFlashColor != null ? 1.0 : 0.0);
        final bar = GestureDetector(
          behavior: HitTestBehavior.opaque,
          onTapDown: (details) => _onTapDown(details, constraints),
          child: CustomPaint(
            size: Size(constraints.maxWidth, widget.height),
            painter: _SeekBarPainter(
              bufferStart: bufferStart,
              bufferEnd: bufferEnd,
              playRatio: playRatio,
              thumbRadius: widget.thumbRadius,
              flashColor: _activeFlashColor,
              flashOpacity: flashOpacity,
            ),
          ),
        );

        if (!widget.showTimeLabels) return bar;

        return Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            bar,
            const SizedBox(height: 4),
            Row(
              children: [
                Text(
                  '${_formatDuration(widget.position)} / '
                  '${_formatDuration(widget.duration)}',
                ),
                const Spacer(),
                Text(
                  'buffer: ${_formatDuration(widget.bufferedStart)} - '
                  '${_formatDuration(widget.bufferedEnd)}',
                ),
              ],
            ),
          ],
        );
      },
    );
  }

  static String _formatDuration(Duration value) {
    final hours = value.inHours;
    final minutes = value.inMinutes.remainder(60).toString().padLeft(2, '0');
    final seconds = value.inSeconds.remainder(60).toString().padLeft(2, '0');
    if (hours > 0) {
      return '$hours:$minutes:$seconds';
    }
    return '$minutes:$seconds';
  }
}

class _SeekBarPainter extends CustomPainter {
  const _SeekBarPainter({
    required this.bufferStart,
    required this.bufferEnd,
    required this.playRatio,
    required this.thumbRadius,
    required this.flashColor,
    required this.flashOpacity,
  });

  final double bufferStart;
  final double bufferEnd;
  final double playRatio;
  final double thumbRadius;
  final Color? flashColor;
  final double flashOpacity;

  @override
  void paint(Canvas canvas, Size size) {
    final trackRect = Rect.fromLTWH(0, 0, size.width, size.height);
    final trackRadius = size.height / 2;

    // Total audio length in black.
    canvas.drawRRect(
      RRect.fromRectAndRadius(trackRect, Radius.circular(trackRadius)),
      Paint()..color = Colors.black,
    );

    // Buffered region in red.
    if (bufferEnd > bufferStart) {
      final bufferRect = Rect.fromLTWH(
        bufferStart * size.width,
        0,
        (bufferEnd - bufferStart) * size.width,
        size.height,
      );
      canvas.drawRRect(
        RRect.fromRectAndRadius(bufferRect, Radius.circular(trackRadius)),
        Paint()..color = Colors.red,
      );
    }

    // Brief flash overlay.
    if (flashColor != null && flashOpacity > 0) {
      canvas.drawRRect(
        RRect.fromRectAndRadius(trackRect, Radius.circular(trackRadius)),
        Paint()..color = flashColor!.withAlpha((255 * flashOpacity).round()),
      );
    }

    // Play position as a yellow vertical line with a circular thumb.
    final x = playRatio * size.width;
    final thumbCenter = Offset(x, size.height / 2);
    final paint = Paint()
      ..color = Colors.yellow
      ..strokeWidth = 3;

    canvas
      ..drawLine(
        Offset(x, 0),
        Offset(x, size.height),
        paint,
      )
      ..drawCircle(
        thumbCenter,
        thumbRadius,
        paint,
      );
  }

  @override
  bool shouldRepaint(covariant _SeekBarPainter old) {
    return old.bufferStart != bufferStart ||
        old.bufferEnd != bufferEnd ||
        old.playRatio != playRatio ||
        old.flashColor != flashColor ||
        old.flashOpacity != flashOpacity;
  }
}
