#
# To learn more about a Podspec see http://guides.cocoapods.org/syntax/podspec.html.
# Run `pod lib lint flutter_soloud.podspec` to validate before publishing.
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
    'flutter_soloud/Sources/flutter_soloud/*.{h,mm}',
    'flutter_soloud/include/FlutterSoloudPlugin.h',
  ]
  s.public_header_files = 'flutter_soloud/include/FlutterSoloudPlugin.h'
    # flutter_soloud.mm is the SwiftPM wrapper that includes the full C++
  # implementation. CocoaPods builds the same implementation through the
  # CMake script phase below, so compiling the wrapper here defines duplicate
  # symbols when the app also force-loads libflutter_soloud_plugin.a.
  s.exclude_files = 'flutter_soloud/Sources/flutter_soloud/flutter_soloud.mm'
  s.dependency 'FlutterMacOS'
  s.platform = :osx, '10.15'

  # Declare vendored Xiph libraries (only when not disabled)
  s.vendored_libraries = 'flutter_soloud/libs/lib*.a' unless ENV['NO_XIPH_LIBS'] == '1'

  # Check if we should disable Xiph libs support (must exist and be '1')
  disable_xiph_libs = !ENV['NO_XIPH_LIBS'].nil? && ENV['NO_XIPH_LIBS'] == '1'
  
  local_lib_path = '$(PODS_TARGET_SRCROOT)/flutter_soloud/libs'
  local_include_path = '$(PODS_TARGET_SRCROOT)/flutter_soloud/include'

  # Path to the plugin's source root from PODS_ROOT (available in app target context)
  plugin_root = '${PODS_ROOT}/../Flutter/ephemeral/.symlinks/plugins/flutter_soloud/macos'

  preprocessor_definitions = ['$(inherited)']
  if disable_xiph_libs
    preprocessor_definitions << 'NO_XIPH_LIBS'
  end
  preprocessor_definitions << 'SIGNALSMITH_USE_PFFFT'

  # Build the plugin's native code using CMake with release optimizations.
  # The built library IS declared as an output file: the app target
  # force_loads it, and Xcode's new build system validates force_load'd
  # inputs when planning the build — without a declared producer, the first
  # (clean) build fails with "Build input file cannot be found" before this
  # phase has ever run, while the second build succeeds because the library
  # already exists on disk. :always_out_of_date makes the phase run on every
  # build anyway (CMake's incremental tracking makes it a fast no-op when no
  # source file changed), so plugin source edits never link stale native
  # code — the problem declaring only :output_files would cause.
  build_script = <<-SCRIPT
    # Backward-compatibility warning for renamed env variable
    if [ -n "$NO_OPUS_OGG_LIBS" ]; then
      echo "warning: NO_OPUS_OGG_LIBS is set. This has no effect because the setting has been renamed to NO_XIPH_LIBS. In your command line invocations and build scripts, simply replace all occurrences of NO_OPUS_OGG_LIBS (old) with NO_XIPH_LIBS (new)."
    fi

    # Xcode's build environment has a restricted PATH that may not include cmake.
    # Add common locations where cmake might be installed before checking.
    export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

    # Check for CMake availability
    if ! command -v cmake &> /dev/null; then
      echo "Error: CMake is not installed. Please install CMake to build flutter_soloud."
      echo "  - On macOS: brew install cmake"
      echo "  - Or visit: https://cmake.org/download/"
      exit 1
    fi

    # Build flutter_soloud with CMake
    #{disable_xiph_libs ? 'export NO_XIPH_LIBS=1' : 'unset NO_XIPH_LIBS'}
    bash "${PODS_TARGET_SRCROOT}/build_cmake.sh"
  SCRIPT

  s.script_phase = {
    :name => 'Build flutter_soloud with CMake',
    :script => build_script,
    :execution_position => :before_compile,
    :output_files => ['${PODS_TARGET_SRCROOT}/cmake_build/macosx/libflutter_soloud_plugin.a'],
    :always_out_of_date => '1',
  }

  # pod_target_xcconfig: settings for the pod's own compilation target.
  # HEADER_SEARCH_PATHS and LIBRARY_SEARCH_PATHS are needed here for compilation.
  s.pod_target_xcconfig = { 
    'HEADER_SEARCH_PATHS' => [
      local_include_path,
      '$(PODS_TARGET_SRCROOT)/../src',
      '$(PODS_TARGET_SRCROOT)/../src/soloud/include',
      '${PODS_ROOT}/abseil',
    ],
    'GCC_PREPROCESSOR_DEFINITIONS' => preprocessor_definitions.join(' '),
    'DEFINES_MODULE' => 'YES', 
    'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'i386',
    "CLANG_CXX_LANGUAGE_STANDARD" => "c++17",
    "CLANG_CXX_LIBRARY" => "libc++",
    'LIBRARY_SEARCH_PATHS' => [
      '$(PODS_TARGET_SRCROOT)/cmake_build/macosx',
      local_lib_path,
    ],
    'VALID_ARCHS' => 'x86_64 arm64',
    # FlutterSoloudPlugin.mm calls the engine-lifecycle exports, which live in
    # libflutter_soloud_plugin.a -- force-loaded into the *app* target, not into
    # this pod. With the default static pod there is no link step here and the
    # references resolve when the app links. With `use_frameworks!` this pod
    # becomes a dynamic framework with its own link step, and they would be
    # undefined; dynamic lookup resolves them at load time against the app
    # binary, which is the same approach Package.swift already takes.
    #
    # Deliberately NOT linking another copy of libflutter_soloud_plugin.a here:
    # that would give the framework its own SoLoud engine and lifecycle state,
    # separate from the app's.
    'OTHER_LDFLAGS' => '$(inherited) -undefined dynamic_lookup',
   }

  # user_target_xcconfig: settings propagated to the APP target's linker.
  # -force_load must be here because it's the app binary that needs the FFI symbols,
  # not the pod's static library (which ignores linker flags).
  # We use PODS_ROOT-based paths because PODS_TARGET_SRCROOT is not available
  # in the app target's context.
  force_load_lib = "-force_load #{plugin_root}/cmake_build/macosx/libflutter_soloud_plugin.a"
  
  # With vendored_libraries declared above, CocoaPods handles xiph lib linking automatically.
  # We only need the library search path for the cmake_build output and ensure inherited flags.
  xiph_flags = disable_xiph_libs ? '' : '-logg -lopus -lvorbis -lvorbisenc -lvorbisfile -lFLAC'

  s.user_target_xcconfig = {
    'OTHER_LDFLAGS' => "$(inherited) #{force_load_lib} #{xiph_flags} -lc++",
    'LIBRARY_SEARCH_PATHS' => "$(inherited) \"#{plugin_root}/cmake_build/macosx\" \"#{plugin_root}/flutter_soloud/libs\"",
    # Fix for FFI symbol stripping on macOS Release builds
    'STRIP_STYLE' => 'debugging',
    'DEBUG_INFORMATION_FORMAT' => 'dwarf-with-dsym',
  }

  s.swift_version = '5.0'
  s.osx.framework  = ['AudioToolbox', 'AVFAudio']

end
