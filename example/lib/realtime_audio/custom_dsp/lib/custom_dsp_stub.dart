import 'custom_dsp.dart';

/// Fallback stub for unsupported environments.
CustomDspCallbacks getPlatformDspCallbacks() {
  throw UnsupportedError('Cannot initialize custom DSP callbacks on this platform.');
}
