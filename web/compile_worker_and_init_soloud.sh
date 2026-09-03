# This script compiles the worker.dart and init_xiph_modules.dart files to JavaScript.

dart compile js -O3 -o worker.dart.js ./worker.dart
echo "worker compiled and copied into 'web' dir!"

dart compile js -O3 -o init_soloud.js ./init_soloud.dart
echo "init_soloud compiled and copied into 'web' dir!"
