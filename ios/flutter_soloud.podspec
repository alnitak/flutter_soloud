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
  s.source_files = 'flutter_soloud/Sources/flutter_soloud/**/*'
  s.dependency 'Flutter'
  s.platform = :ios, '13.0'

  # Check if we should disable opus/ogg support (must exist and be '1')
  disable_opus_ogg = !ENV['NO_OPUS_OGG_LIBS'].nil? && ENV['NO_OPUS_OGG_LIBS'] == '1'

  # Path to the plugin's source root from PODS_ROOT (available in app target context)
  plugin_root = '${PODS_ROOT}/../.symlinks/plugins/flutter_soloud/ios'

  preprocessor_definitions = ['$(inherited)']
  if disable_opus_ogg
    preprocessor_definitions << 'NO_OPUS_OGG_LIBS'
  end
  preprocessor_definitions << 'SIGNALSMITH_USE_PFFFT'

  # Build the plugin's native code using CMake with release optimizations.
  # CMake handles incremental builds internally — if no source files changed,
  # this is a fast no-op.
  s.script_phase = {
    :name => 'Build flutter_soloud with CMake',
    :script => 'bash "${PODS_TARGET_SRCROOT}/build_cmake.sh"',
    :execution_position => :before_compile,
  }

  # Flutter.framework does not contain a i386 slice.
  # pod_target_xcconfig: settings for the pod's own compilation target.
  s.pod_target_xcconfig = { 
    'HEADER_SEARCH_PATHS' => [
      '$(PODS_TARGET_SRCROOT)/include',
      '$(PODS_TARGET_SRCROOT)/include/opus',
      '$(PODS_TARGET_SRCROOT)/include/ogg',
      '$(PODS_TARGET_SRCROOT)/include/vorbis',
      '$(PODS_TARGET_SRCROOT)/../src',
      '$(PODS_TARGET_SRCROOT)/../src/soloud/include',
      '${PODS_ROOT}/abseil',
    ],
    'GCC_PREPROCESSOR_DEFINITIONS' => preprocessor_definitions.join(' '),
    'DEFINES_MODULE' => 'YES', 
    'VALID_ARCHS' => 'arm64 x86_64',
    'LIBRARY_SEARCH_PATHS' => [
      '$(PODS_TARGET_SRCROOT)/cmake_build/$(PLATFORM_NAME)',
      '$(PODS_TARGET_SRCROOT)/libs',
    ],
    "CLANG_CXX_LANGUAGE_STANDARD" => "c++17",
    "CLANG_CXX_LIBRARY" => "libc++"
  }

  # user_target_xcconfig: settings propagated to the APP target's linker.
  # -force_load must be here because it's the app binary that needs the FFI symbols.
  # We use PODS_ROOT-based paths because PODS_TARGET_SRCROOT is not available
  # in the app target's context.
  force_load_lib = "-force_load #{plugin_root}/cmake_build/$(PLATFORM_NAME)/libflutter_soloud_plugin.a"

  if disable_opus_ogg
    user_ldflags_device = force_load_lib
    user_ldflags_sim = force_load_lib
  else
    user_ldflags_device = "#{force_load_lib} -L#{plugin_root}/libs -logg_iOS-device -lopus_iOS-device -lvorbis_iOS-device -lvorbisfile_iOS-device -lflac_iOS-device"
    user_ldflags_sim = "#{force_load_lib} -L#{plugin_root}/libs -logg_iOS-simulator -lopus_iOS-simulator -lvorbis_iOS-simulator -lvorbisfile_iOS-simulator -lflac_iOS-simulator"
  end

  s.user_target_xcconfig = {
    'OTHER_LDFLAGS[sdk=iphoneos*]' => "$(inherited) #{user_ldflags_device}",
    'OTHER_LDFLAGS[sdk=iphonesimulator*]' => "$(inherited) #{user_ldflags_sim}",
    'LIBRARY_SEARCH_PATHS' => "$(inherited) \"#{plugin_root}/cmake_build/$(PLATFORM_NAME)\" \"#{plugin_root}/libs\"",
  }
  
  # Only include libraries if opus/ogg is enabled
  if !disable_opus_ogg
    s.ios.vendored_libraries = [
      'libs/libopus_iOS-device.a',
      'libs/libogg_iOS-device.a',
      'libs/libvorbis_iOS-device.a',
      'libs/libvorbisfile_iOS-device.a',
      'libs/libflac_iOS-device.a'
    ]
    s.preserve_paths = [
      'libs/libopus_iOS-device.a',
      'libs/libogg_iOS-device.a',
      'libs/libopus_iOS-simulator.a',
      'libs/libogg_iOS-simulator.a',
      'libs/libvorbis_iOS-device.a',
      'libs/libvorbis_iOS-simulator.a',
      'libs/libvorbisfile_iOS-device.a',
      'libs/libvorbisfile_iOS-simulator.a',
      'libs/libflac_iOS-device.a',
      'libs/libflac_iOS-simulator.a'
    ]
  end

  s.swift_version = '5.0'
  s.ios.framework  = ['AudioToolbox', 'AVFAudio']
end
