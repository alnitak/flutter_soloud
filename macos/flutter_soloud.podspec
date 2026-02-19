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
  # Keep source_files so CocoaPods creates a valid pod target.
  # The .mm file is minimal — the real code is built by CMake via script_phase.
  s.source_files = 'Classes/**/*'
  s.dependency 'FlutterMacOS'
  s.platform = :osx, '10.15'

  # Check if we should disable opus/ogg support (must exist and be '1')
  disable_opus_ogg = !ENV['NO_OPUS_OGG_LIBS'].nil? && ENV['NO_OPUS_OGG_LIBS'] == '1'
  
  local_lib_path = '$(PODS_TARGET_SRCROOT)/libs'
  local_include_path = '$(PODS_TARGET_SRCROOT)/include'

  # Path to the plugin's source root from PODS_ROOT (available in app target context)
  plugin_root = '${PODS_ROOT}/../Flutter/ephemeral/.symlinks/plugins/flutter_soloud/macos'

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
   }

  # user_target_xcconfig: settings propagated to the APP target's linker.
  # -force_load must be here because it's the app binary that needs the FFI symbols,
  # not the pod's static library (which ignores linker flags).
  # We use PODS_ROOT-based paths because PODS_TARGET_SRCROOT is not available
  # in the app target's context.
  force_load_lib = "-force_load #{plugin_root}/cmake_build/macosx/libflutter_soloud_plugin.a"
  xiph_flags = disable_opus_ogg ? '' : " -L#{plugin_root}/libs -logg -lopus -lvorbis -lvorbisfile -lFLAC"

  s.user_target_xcconfig = {
    'OTHER_LDFLAGS' => "$(inherited) #{force_load_lib}#{xiph_flags}",
    'LIBRARY_SEARCH_PATHS' => "$(inherited) \"#{plugin_root}/cmake_build/macosx\" \"#{plugin_root}/libs\"",
  }

  s.swift_version = '5.0'
  s.osx.framework  = ['AudioToolbox', 'AVFAudio']

end
