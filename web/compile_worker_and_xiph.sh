# This script compiles the worker.dart and init_xiph_modules.dart files to JavaScript.

dart compile js -O3 -o worker.dart.js ./worker.dart

dart compile js -O3 -o init_xiph_modules.dart.js ./init_xiph_modules.dart
