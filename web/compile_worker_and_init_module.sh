# This script compiles the worker.dart and init_xiph_modules.dart files to JavaScript.

dart compile js -O3 -o worker.dart.js ./worker.dart
echo "worker compiled and copied into 'web' dir!"

dart compile js -O3 -o init_module.dart.js ./init_module.dart
echo "init_module compiled and copied into 'web' dir!"
