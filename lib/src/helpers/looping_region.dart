/// Validates a half-open looping region `[start, end)` or frame offsets `[startOffset, endOffset)`.
///
/// A `null` [end] means that the source's natural end is used.
/// [startOffset] and [endOffset] are optional exact frame offsets.
/// [start] / [end] and [startOffset] / [endOffset] are mutually exclusive.
void validateLoopRegion({
  required Duration start,
  Duration? end,
  int? startOffset,
  int? endOffset,
}) {
  if (startOffset != null || endOffset != null) {
    assert(
      start == Duration.zero && end == null,
      'Cannot use both Duration-based looping (loopingStartAt / loopingEndAt) '
      'and frame-offset looping (loopingStartOffsetAt / loopingEndOffsetAt) at the same time.',
    );
    if (startOffset != null && startOffset < 0) {
      throw ArgumentError.value(
        startOffset,
        'loopingStartOffsetAt',
        'Must not be negative.',
      );
    }
    if (endOffset != null && endOffset <= (startOffset ?? 0)) {
      throw ArgumentError.value(
        endOffset,
        'loopingEndOffsetAt',
        'Must be greater than loopingStartOffsetAt.',
      );
    }
    return;
  }

  if (start.isNegative) {
    throw ArgumentError.value(start, 'start', 'Must not be negative.');
  }

  if (end != null && end.compareTo(start) <= 0) {
    throw ArgumentError.value(end, 'end', 'Must be greater than start.');
  }
}
