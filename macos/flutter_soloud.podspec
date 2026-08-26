#
# To learn more about a Podspec see http://guides.cocoapods.org/syntax/podspec.html.
# Run `pod lib lint flutter_soloud.podspec` to validate before publishing.
#
# The native engine is built by the Dart build hook (hook/build.dart) and
# bundled as a native code asset. This pod only provides the
# FlutterSoloudPlugin class, which observes the FlutterEngine lifecycle.
#
Pod::Spec.new do |s|
  s.name             = 'flutter_soloud'
  s.version          = '0.0.1'
  s.summary          = 'Flutter audio plugin using SoLoud library and FFI'
  s.description      = <<-DESC
Flutter audio plugin using SoLoud library and FFI
                       DESC
  s.homepage         = 'http://example.com'
  s.license          = { :file => '../LICENSE' }
  s.author           = { 'Your Company' => 'email@example.com' }

  s.source           = { :path => '.' }
  # FlutterSoloudPlugin.h lives in the include directory rather than beside its
  # .mm because SwiftPM has no explicit publicHeadersPath and therefore uses the
  # target's default `include` (a symlink to the same directory). Listing it
  # here matters more on macOS than on iOS: the generated registrant is Swift
  # and does a plain `import flutter_soloud`, with no __has_include fallback, so
  # the class has to reach the module's public headers or the build fails.
  s.source_files     = [
    'flutter_soloud/Sources/flutter_soloud/FlutterSoloudPlugin.mm',
    'flutter_soloud/include/FlutterSoloudPlugin.h',
  ]
  s.public_header_files = 'flutter_soloud/include/FlutterSoloudPlugin.h'
  s.dependency 'FlutterMacOS'
  s.platform = :osx, '10.15'

  s.pod_target_xcconfig = {
    'DEFINES_MODULE' => 'YES',
    'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'i386',
    # FlutterSoloudPlugin.mm includes "engine_lifecycle.h" from src/.
    'HEADER_SEARCH_PATHS' => ['$(PODS_TARGET_SRCROOT)/../src'],
    # FlutterSoloudPlugin.mm calls the engine-lifecycle exports, which live in
    # the native code asset built by the Dart build hook and loaded into the
    # app. With `use_frameworks!` this pod becomes a dynamic framework with its
    # own link step, where they would be undefined; dynamic lookup resolves
    # them at load time against the app binary's loaded images.
    'OTHER_LDFLAGS' => '$(inherited) -undefined dynamic_lookup',
   }

  s.swift_version = '5.0'
end
