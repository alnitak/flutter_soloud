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
  s.source_files = 'flutter_soloud/Sources/flutter_soloud/*'
  # flutter_soloud.mm is the SwiftPM wrapper that includes the full C++
  # implementation. CocoaPods builds the same implementation through the
  # CMake script phase below, so compiling the wrapper here defines duplicate
  # symbols when the app also force-loads libflutter_soloud_plugin.a.
  s.exclude_files = 'flutter_soloud/Sources/flutter_soloud/flutter_soloud.mm'
  s.dependency 'Flutter'
  s.platform = :ios, '13.0'

  # Check if we should disable Xiph libs support (must exist and be '1')
  disable_xiph_libs = !ENV['NO_XIPH_LIBS'].nil? && ENV['NO_XIPH_LIBS'] == '1'

  # Path to the plugin's source root from PODS_ROOT (available in app target context)
  plugin_root = '${PODS_ROOT}/../.symlinks/plugins/flutter_soloud/ios'

  preprocessor_definitions = ['$(inherited)']
  if disable_xiph_libs
    preprocessor_definitions << 'NO_XIPH_LIBS'
  end
  preprocessor_definitions << 'SIGNALSMITH_USE_PFFFT'

  # Build the plugin's native code using CMake with release optimizations.
  # No input/output files are declared on purpose: the phase runs on every
  # build and CMake's incremental tracking makes it a fast no-op when no
  # source file changed. Declaring only :output_files would let Xcode skip
  # the phase once the library exists, silently linking stale native code
  # after plugin source edits.
  build_script = <<-SCRIPT
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
  }

  # Flutter.framework does not contain a i386 slice.
  # pod_target_xcconfig: settings for the pod's own compilation target.
  # NOTE: We use SDK-conditioned OTHER_LDFLAGS here (not vendored_libraries) because
  # the Xiph libraries have separate -device and -simulator variants. vendored_libraries
  # always injects the same -l flags regardless of SDK, which causes linker failures
  # when building for simulator (it tries to link -device libs that lack simulator slices).
  pod_xcconfig = { 
    'HEADER_SEARCH_PATHS' => [
      '$(PODS_TARGET_SRCROOT)/flutter_soloud/include',
      '$(PODS_TARGET_SRCROOT)/flutter_soloud/include/opus',
      '$(PODS_TARGET_SRCROOT)/flutter_soloud/include/ogg',
      '$(PODS_TARGET_SRCROOT)/flutter_soloud/include/vorbis',
      '$(PODS_TARGET_SRCROOT)/../src',
      '$(PODS_TARGET_SRCROOT)/../src/soloud/include',
      '${PODS_ROOT}/abseil',
    ],
    'GCC_PREPROCESSOR_DEFINITIONS' => preprocessor_definitions.join(' '),
    'DEFINES_MODULE' => 'YES', 
    'VALID_ARCHS' => 'arm64 x86_64',
    'LIBRARY_SEARCH_PATHS' => [
      '$(PODS_TARGET_SRCROOT)/cmake_build/$(PLATFORM_NAME)',
      '$(PODS_TARGET_SRCROOT)/flutter_soloud/libs',
    ],
    "CLANG_CXX_LANGUAGE_STANDARD" => "c++17",
    "CLANG_CXX_LIBRARY" => "libc++"
  }

  # Add SDK-conditioned linker flags for Xiph libs to the pod's own target
  if !disable_xiph_libs
    pod_xcconfig['OTHER_LDFLAGS[sdk=iphoneos*]'] = '$(inherited) -logg_iOS-device -lopus_iOS-device -lvorbis_iOS-device -lvorbisenc_iOS-device -lvorbisfile_iOS-device -lFLAC_iOS-device'
    pod_xcconfig['OTHER_LDFLAGS[sdk=iphonesimulator*]'] = '$(inherited) -logg_iOS-simulator -lopus_iOS-simulator -lvorbis_iOS-simulator -lvorbisenc_iOS-simulator -lvorbisfile_iOS-simulator -lFLAC_iOS-simulator'
  end

  s.pod_target_xcconfig = pod_xcconfig

  # user_target_xcconfig: settings propagated to the APP target's linker.
  # -force_load must be here because it's the app binary that needs the FFI symbols.
  # We use PODS_ROOT-based paths because PODS_TARGET_SRCROOT is not available
  # in the app target's context.
  force_load_lib = "-force_load #{plugin_root}/cmake_build/$(PLATFORM_NAME)/libflutter_soloud_plugin.a"

  if disable_xiph_libs
    user_ldflags_device = force_load_lib
    user_ldflags_sim = force_load_lib
  else
    user_ldflags_device = "#{force_load_lib} -L#{plugin_root}/flutter_soloud/libs -logg_iOS-device -lopus_iOS-device -lvorbis_iOS-device -lvorbisenc_iOS-device -lvorbisfile_iOS-device -lFLAC_iOS-device"
    user_ldflags_sim = "#{force_load_lib} -L#{plugin_root}/flutter_soloud/libs -logg_iOS-simulator -lopus_iOS-simulator -lvorbis_iOS-simulator -lvorbisenc_iOS-simulator -lvorbisfile_iOS-simulator -lFLAC_iOS-simulator"
  end

  s.user_target_xcconfig = {
    'OTHER_LDFLAGS[sdk=iphoneos*]' => "$(inherited) #{user_ldflags_device} -lc++",
    'OTHER_LDFLAGS[sdk=iphonesimulator*]' => "$(inherited) #{user_ldflags_sim} -lc++",
    'LIBRARY_SEARCH_PATHS' => "$(inherited) \"#{plugin_root}/cmake_build/$(PLATFORM_NAME)\" \"#{plugin_root}/flutter_soloud/libs\"",
    # Fix for FFI symbol stripping on iOS Release builds
    'STRIP_STYLE' => 'debugging',
    'DEBUG_INFORMATION_FORMAT' => 'dwarf-with-dsym',
  }
  
  # Do NOT use vendored_libraries for Xiph libs — it generates non-SDK-conditioned
  # -l flags that always point to -device variants, breaking simulator builds.
  # Instead, preserve_paths keeps the .a files from being stripped by CocoaPods,
  # and the SDK-conditioned OTHER_LDFLAGS above handle linking.
  if !disable_xiph_libs
    s.preserve_paths = [
      'flutter_soloud/libs/libopus_iOS-device.a',
      'flutter_soloud/libs/libogg_iOS-device.a',
      'flutter_soloud/libs/libopus_iOS-simulator.a',
      'flutter_soloud/libs/libogg_iOS-simulator.a',
      'flutter_soloud/libs/libvorbis_iOS-device.a',
      'flutter_soloud/libs/libvorbis_iOS-simulator.a',
      'flutter_soloud/libs/libvorbisenc_iOS-device.a',
      'flutter_soloud/libs/libvorbisenc_iOS-simulator.a',
      'flutter_soloud/libs/libvorbisfile_iOS-device.a',
      'flutter_soloud/libs/libvorbisfile_iOS-simulator.a',
      'flutter_soloud/libs/libFLAC_iOS-device.a',
      'flutter_soloud/libs/libFLAC_iOS-simulator.a'
    ]
  end

  s.swift_version = '5.0'
  s.ios.framework  = ['AudioToolbox', 'AVFAudio']
end
