/// The extension type for the [AudioData.get2D] method which accepts
/// the [value] value in 0~255 range.
extension type SampleRow._(int value) {
  /// Constructs a valid row with [value].
  SampleRow(this.value)
      : assert(
            value >= 0 && value <= 255,
            'row must in 0~255 included range.');
}

/// The extension type for the [AudioData.get2D] method which accepts
/// the [value] value in 0~511 range.
extension type SampleColumn._(int value) {
  /// Constructs a valid column with [value].
  SampleColumn(this.value)
      : assert(
            value >= 0 && value <= 511,
            'row must in 0~512 included range.');
}

/// The extension type for the [AudioData.get1D] method which accepts
/// the [value] value in 0~511 range.
extension type SampleLinear._(int value) {
  /// Constructs a valid offset with [value].
  SampleLinear(this.value)
      : assert(
            value >= 0 && value <= 511,
            'offset must in 0~512 included range.');
}

/// The extension type for the [AudioData.getWave]
/// method which accepts the [value] value in 0~255 range.
extension type SampleWave._(int value) {
  /// Constructs a valid offset with [value].
  SampleWave(this.value)
      : assert(
            value >= 0 && value <= 255,
            'offset must in 0~255 included range.');
}
