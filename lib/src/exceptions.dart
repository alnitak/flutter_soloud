/// Exception thrown when a network request (in `SoLoud.loadUrl()`) fails.
class SoLoudNetworkException implements Exception {
  /// Constructs the exception.
  const SoLoudNetworkException(this.message, {required this.statusCode});

  /// The message describing the failure.
  final String message;

  /// The HTTP status code of the failure.
  final int statusCode;

  @override
  String toString() => '$message (HTTP $statusCode)';
}
