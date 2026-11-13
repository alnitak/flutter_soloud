// The build hook's prebuilt-archive cache (hook/build.dart), against archives
// served from a local HTTP server.
//
// An install must be all or nothing. An extraction cut short -- a truncated
// download, a build killed part-way, a full disk -- leaves the members
// before the cut intact and the one being written truncated. The cache's
// validators only look for the files a build needs, so an extraction into
// the cache itself left them all in place, the last one truncated, for every
// later build to accept.
//
// An install that another invocation of the hook finishes while this one
// downloads must be kept, not replaced under a build that may be reading it.
// An install gets the mode the umask gives a new directory. What builds
// killed part-way staged is pruned once it has not changed for an hour.
@TestOn('vm')
library;

import 'dart:io';
import 'dart:math';

import 'package:code_assets/code_assets.dart';
import 'package:test/test.dart';

import '../hook/build.dart' as hook;

const _xiphLibs = ['FLAC', 'opus', 'vorbisfile', 'vorbisenc', 'vorbis', 'ogg'];

/// Serves the archives in [archives] by file name, counting the requests.
class _ArchiveServer {
  _ArchiveServer._(this._server) {
    _server.listen(_handle);
  }

  static Future<_ArchiveServer> start() async =>
      _ArchiveServer._(await HttpServer.bind(InternetAddress.loopbackIPv4, 0));

  final HttpServer _server;
  final Map<String, List<int>> archives = {};
  final List<String> requests = [];

  /// Run once, on the next request, before it is answered: what happens
  /// elsewhere while that download is in flight.
  Future<void> Function()? whileServing;

  String get baseUrl => 'http://127.0.0.1:${_server.port}';

  Future<void> _handle(HttpRequest request) async {
    final name = request.uri.pathSegments.last;
    requests.add(name);
    final during = whileServing;
    whileServing = null;
    await during?.call();
    final body = archives[name];
    if (body == null) {
      request.response.statusCode = HttpStatus.notFound;
    } else {
      request.response.add(body);
    }
    await request.response.close();
  }

  Future<void> close() => _server.close(force: true);
}

void main() {
  late Directory scratch;
  late Directory packageRoot;
  late _ArchiveServer server;
  final random = Random(42);
  var archiveCount = 0;

  setUp(() async {
    scratch = Directory.systemTemp.createTempSync('soloud_hook_test_');
    packageRoot = Directory('${scratch.path}/package')..createSync();
    server = await _ArchiveServer.start();
    hook.XiphLink.debugBaseUrl = server.baseUrl;
  });

  tearDown(() async {
    hook.XiphLink.debugBaseUrl = null;
    await server.close();
    scratch.deleteSync(recursive: true);
  });

  /// Random, so that gzip cannot shrink it and a cut at a fraction of the
  /// archive lands in a predictable member.
  List<int> randomBytes(int length) =>
      List<int>.generate(length, (_) => random.nextInt(256));

  /// [files] (relative path -> contents) packed, in order, into a .tar.gz.
  Future<List<int>> tarGz(Map<String, List<int>> files) async {
    final source = Directory('${scratch.path}/archive${archiveCount++}')
      ..createSync();
    for (final entry in files.entries) {
      File('${source.path}/${entry.key}')
        ..parent.createSync(recursive: true)
        ..writeAsBytesSync(entry.value);
    }
    final archive = File('${source.path}.tar.gz');
    final result = await Process.run(
      'tar',
      ['-czf', archive.path, '-C', source.path, ...files.keys],
      // No AppleDouble members for extended attributes on macOS.
      environment: {'COPYFILE_DISABLE': '1'},
    );
    expect(result.exitCode, 0, reason: '${result.stderr}');
    return archive.readAsBytesSync();
  }

  /// The first [fraction] of [archive], as a transfer cut short leaves it.
  List<int> cut(List<int> archive, double fraction) =>
      archive.sublist(0, (archive.length * fraction).floor());

  Directory prebuildDir(String subDir) =>
      Directory('${packageRoot.path}/.dart_tool/flutter_soloud/$subDir');

  /// No staging directory is left beside the installs under [dir].
  void expectNoStagingLeft(Directory dir) {
    final left = dir
        .listSync(recursive: true)
        .map((e) => e.path.substring(dir.path.length))
        .where((path) => path.contains('.staging-'));
    expect(left, isEmpty);
  }

  /// [dir] has the mode the umask gives a new directory, as an extraction in
  /// place gave it, not the private mode of a temporary directory.
  void expectNewDirectoryMode(Directory dir) {
    if (Platform.isWindows) return;
    final probe = Directory('${scratch.path}/mode_probe')..createSync();
    final expected = probe.statSync().modeString();
    probe.deleteSync();
    expect(dir.statSync().modeString(), expected, reason: dir.path);
  }

  /// Sets the modification time of each of [entities] two hours back, with
  /// touch: dart:io cannot set a directory's.
  Future<void> ageTwoHours(List<FileSystemEntity> entities) async {
    final then = DateTime.now().subtract(const Duration(hours: 2));
    String two(int n) => n.toString().padLeft(2, '0');
    final stamp =
        '${then.year}${two(then.month)}${two(then.day)}'
        '${two(then.hour)}${two(then.minute)}';
    final result = await Process.run('touch', [
      '-t',
      stamp,
      for (final entity in entities) entity.path,
    ]);
    expect(result.exitCode, 0, reason: '${result.stderr}');
  }

  group('Xiph libraries', () {
    bool macosValidator(Directory dir) =>
        _xiphLibs.every((lib) => File('${dir.path}/lib$lib.a').existsSync());

    Future<Directory> ensureMacos() =>
        hook.XiphLink.debugEnsurePrebuildPlatform(
          packageRoot: packageRoot.uri,
          os: OS.macOS,
          arch: Architecture.x64,
          subDir: 'macos',
          archiveName: 'xiph-macos.tar.gz',
          validator: macosValidator,
        );

    test('an extraction cut short is never accepted', () async {
      final contents = {for (final lib in _xiphLibs) lib: randomBytes(65536)};
      final full = await tarGz({
        for (final lib in _xiphLibs) 'lib$lib.a': contents[lib]!,
      });
      // Inside the last member: every library the validator looks for is
      // created, and the last is truncated.
      server.archives['xiph-macos.tar.gz'] = cut(full, 0.97);
      final cacheDir = prebuildDir('xiph/prebuild/macos');

      await expectLater(ensureMacos(), throwsA(anything));
      expect(
        cacheDir.existsSync(),
        isFalse,
        reason: 'the failed extraction was left in the cache',
      );

      // The next build downloads the archive again rather than using what
      // the failed extraction left.
      await expectLater(ensureMacos(), throwsA(anything));
      expect(server.requests, hasLength(2));

      server.archives['xiph-macos.tar.gz'] = full;
      final dir = await ensureMacos();
      expect(dir.path, cacheDir.path);
      expect(server.requests, hasLength(3));
      for (final lib in _xiphLibs) {
        expect(
          File('${dir.path}/lib$lib.a').readAsBytesSync(),
          contents[lib],
          reason: 'lib$lib.a',
        );
      }
      expectNoStagingLeft(prebuildDir('xiph'));
      expectNewDirectoryMode(dir);

      // Installed for good: no further download.
      await ensureMacos();
      expect(server.requests, hasLength(3));
    });

    test('an install another build finishes meanwhile is kept', () async {
      List<int> lib(int seed) => List<int>.filled(1024, seed);
      final theirs = await tarGz({
        for (final l in _xiphLibs) 'lib$l.a': lib(1),
      });
      final ours = await tarGz({for (final l in _xiphLibs) 'lib$l.a': lib(2)});
      // While our download is in flight, another build downloads the same
      // archive and installs it. It may be reading it by the time ours is
      // extracted, so ours must not replace it.
      server.archives['xiph-macos.tar.gz'] = theirs;
      server.whileServing = () async {
        await ensureMacos();
        server.archives['xiph-macos.tar.gz'] = ours;
      };

      final dir = await ensureMacos();
      expect(server.requests, hasLength(2));
      expect(
        File('${dir.path}/libogg.a').readAsBytesSync(),
        lib(1),
        reason: "the other build's install was replaced",
      );
      expectNoStagingLeft(prebuildDir('xiph'));
    });

    test('staging directories that killed builds left are pruned', () async {
      server.archives['xiph-macos.tar.gz'] = await tarGz({
        for (final lib in _xiphLibs) 'lib$lib.a': randomBytes(1024),
      });
      final parent = prebuildDir('xiph/prebuild')..createSync(recursive: true);
      // What a build killed mid-download leaves: a partial archive.
      Future<Directory> leftover(String name, {required bool old}) async {
        final dir = Directory('${parent.path}/$name');
        final inner = Directory('${dir.path}/archive');
        final partial = File('${inner.path}/dl_1.tar.gz')
          ..createSync(recursive: true)
          ..writeAsBytesSync([1, 2, 3]);
        // Aged once it is all written: writing into a directory sets its
        // time.
        if (old) await ageTwoHours([partial, inner, dir]);
        return dir;
      }

      // Pruned when a build downloads...
      final stale = await leftover('.staging-stale', old: true);
      final live = await leftover('.staging-live', old: false);
      await ensureMacos();
      expect(server.requests, hasLength(1));
      expect(stale.existsSync(), isFalse, reason: 'a stale leftover was kept');
      expect(
        live.existsSync(),
        isTrue,
        reason: "another build's staging directory was deleted",
      );

      // ...and when it finds everything installed.
      final staleToo = await leftover('.staging-stale-too', old: true);
      await ensureMacos();
      expect(server.requests, hasLength(1));
      expect(staleToo.existsSync(), isFalse);
      expect(live.existsSync(), isTrue);
    });

    test(
      'a cache that an extraction in place left is downloaded again',
      () async {
        // What an extraction in place, cut short, leaves where every library
        // the validator looks for was reached: no install marker.
        final cacheDir = prebuildDir('xiph/prebuild/macos')
          ..createSync(recursive: true);
        for (final lib in _xiphLibs) {
          File('${cacheDir.path}/lib$lib.a').writeAsBytesSync([0]);
        }
        final contents = {for (final lib in _xiphLibs) lib: randomBytes(1024)};
        server.archives['xiph-macos.tar.gz'] = await tarGz({
          for (final lib in _xiphLibs) 'lib$lib.a': contents[lib]!,
        });

        final dir = await ensureMacos();
        expect(server.requests, hasLength(1));
        expect(File('${dir.path}/libogg.a').readAsBytesSync(), contents['ogg']);
      },
    );

    group('Android', () {
      bool androidValidator(Directory dir) =>
          _xiphLibs.every((lib) => File('${dir.path}/lib$lib.so').existsSync());

      Future<Directory> ensureAbi(String abi) =>
          hook.XiphLink.debugEnsurePrebuildPlatform(
            packageRoot: packageRoot.uri,
            os: OS.android,
            arch: abi == 'arm64-v8a' ? Architecture.arm64 : Architecture.x64,
            subDir: 'android/$abi',
            archiveName: 'xiph-android.tar.gz',
            validator: androidValidator,
          );

      /// The Android archive's layout: one directory per ABI.
      Map<String, List<int>> androidArchive(int size) => {
        'flutter_soloud_prebuild_version.txt': '1.1.0\n'.codeUnits,
        for (final abi in ['arm64-v8a', 'x86_64'])
          for (final lib in _xiphLibs) '$abi/lib$lib.so': randomBytes(size),
      };

      test('an extraction cut short installs no ABI', () async {
        final files = androidArchive(32768);
        final full = await tarGz(files);
        // Inside x86_64's last library.
        server.archives['xiph-android.tar.gz'] = cut(full, 0.97);

        await expectLater(ensureAbi('arm64-v8a'), throwsA(anything));
        await expectLater(
          ensureAbi('x86_64'),
          throwsA(anything),
          reason: "x86_64's truncated libraries were accepted",
        );
        expect(server.requests, hasLength(2));
        expect(
          prebuildDir('xiph/prebuild/android/arm64-v8a').existsSync(),
          isFalse,
        );
        expect(
          prebuildDir('xiph/prebuild/android/x86_64').existsSync(),
          isFalse,
        );

        // One download installs every ABI in the archive.
        server.archives['xiph-android.tar.gz'] = full;
        final arm64 = await ensureAbi('arm64-v8a');
        final x64 = await ensureAbi('x86_64');
        expect(server.requests, hasLength(3));
        expect(
          File('${arm64.path}/libogg.so').readAsBytesSync(),
          files['arm64-v8a/libogg.so'],
        );
        expect(
          File('${x64.path}/libogg.so').readAsBytesSync(),
          files['x86_64/libogg.so'],
        );
        expectNoStagingLeft(prebuildDir('xiph'));
      });

      test('an installed ABI is left alone', () async {
        final first = androidArchive(1024);
        server.archives['xiph-android.tar.gz'] = await tarGz(first);
        await ensureAbi('arm64-v8a');
        prebuildDir('xiph/prebuild/android/x86_64').deleteSync(recursive: true);

        final second = androidArchive(1024);
        server.archives['xiph-android.tar.gz'] = await tarGz(second);
        final x64 = await ensureAbi('x86_64');
        expect(server.requests, hasLength(2));
        expect(
          File('${x64.path}/libogg.so').readAsBytesSync(),
          second['x86_64/libogg.so'],
        );
        expect(
          File(
            '${prebuildDir('xiph/prebuild/android/arm64-v8a').path}/libogg.so',
          ).readAsBytesSync(),
          first['arm64-v8a/libogg.so'],
        );
      });
    });

    test('headers: an extraction cut short is never accepted', () async {
      final last = randomBytes(262144);
      final full = await tarGz({
        'ogg/ogg.h': '// ogg\n'.codeUnits,
        'ogg/config_types.h': '// config\n'.codeUnits,
        'vorbis/codec.h': '// vorbis\n'.codeUnits,
        'opus/opus.h': '// opus\n'.codeUnits,
        'FLAC/all.h': '// flac\n'.codeUnits,
        'FLAC/format.h': last,
      });
      server.archives['xiph-include.tar.gz'] = cut(full, 0.97);
      Future<Directory> ensure() =>
          hook.XiphLink.debugEnsurePrebuildInclude(packageRoot.uri, 'latest');

      await expectLater(ensure(), throwsA(anything));
      await expectLater(
        ensure(),
        throwsA(anything),
        reason: 'headers missing FLAC/format.h were accepted',
      );

      server.archives['xiph-include.tar.gz'] = full;
      final dir = await ensure();
      expect(File('${dir.path}/FLAC/format.h').readAsBytesSync(), last);
      expect(server.requests, hasLength(3));
      expectNoStagingLeft(prebuildDir('xiph'));
      expectNewDirectoryMode(dir);
    });
  });

  group('VST3', () {
    test('library: an extraction cut short is never accepted', () async {
      final library = randomBytes(262144);
      final full = await tarGz({'libvst3_hosting.a': library});
      server.archives['vst3-macos.tar.gz'] = cut(full, 0.97);
      Future<Directory> ensure() => hook.Vst3Link.debugEnsureVst3Platform(
        packageRoot: packageRoot.uri,
        os: OS.macOS,
        arch: Architecture.x64,
        subDir: 'macos',
        archiveName: 'vst3-macos.tar.gz',
        validator: (dir) => File('${dir.path}/libvst3_hosting.a').existsSync(),
      );

      await expectLater(ensure(), throwsA(anything));
      await expectLater(
        ensure(),
        throwsA(anything),
        reason: 'a truncated libvst3_hosting.a was accepted',
      );

      server.archives['vst3-macos.tar.gz'] = full;
      final dir = await ensure();
      expect(File('${dir.path}/libvst3_hosting.a').readAsBytesSync(), library);
      expect(server.requests, hasLength(3));
      expectNoStagingLeft(prebuildDir('vst3'));
      expectNewDirectoryMode(dir);
    });

    test('headers: an extraction cut short is never accepted', () async {
      final last = randomBytes(262144);
      final full = await tarGz({
        'pluginterfaces/vst/ivstaudioprocessor.h': '// processor\n'.codeUnits,
        'public.sdk/source/vst/hosting/module.h': '// module\n'.codeUnits,
        'public.sdk/source/vst/hosting/plugprovider.h': last,
      });
      server.archives['vst3-include.tar.gz'] = cut(full, 0.97);
      Future<Directory> ensure() =>
          hook.Vst3Link.debugEnsureVst3Include(packageRoot.uri, 'latest');

      await expectLater(ensure(), throwsA(anything));
      await expectLater(
        ensure(),
        throwsA(anything),
        reason: 'headers missing plugprovider.h were accepted',
      );

      server.archives['vst3-include.tar.gz'] = full;
      final dir = await ensure();
      expect(
        File(
          '${dir.path}/public.sdk/source/vst/hosting/plugprovider.h',
        ).readAsBytesSync(),
        last,
      );
      expect(server.requests, hasLength(3));
      expectNoStagingLeft(prebuildDir('vst3'));
      expectNewDirectoryMode(dir);
    });
  });
}
