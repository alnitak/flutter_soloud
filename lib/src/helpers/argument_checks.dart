/// Throws an [ArgumentError] if [value] is NaN or infinite.
///
/// The audio engine works with finite floats only: a NaN or an infinity
/// passed as a volume, speed, position or parameter ends up as NaN samples
/// in the mix.
void checkFinite(double value, String name) {
  if (!value.isFinite) {
    throw ArgumentError.value(value, name, 'Must be a finite number.');
  }
}
