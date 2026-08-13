/// Validates a half-open looping region `[start, end)`.
///
/// A `null` [end] means that the source's natural end is used.
void validateLoopRegion({required Duration start, Duration? end}) {
  if (start.isNegative) {
    throw ArgumentError.value(start, 'start', 'Must not be negative.');
  }

  if (end != null && end.compareTo(start) <= 0) {
    throw ArgumentError.value(end, 'end', 'Must be greater than start.');
  }
}
