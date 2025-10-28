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

  # This will ensure the source files in Classes/ are included in the native
  # builds of apps using this FFI plugin. Podspec does not support relative
  # paths, so Classes contains a forwarder C file that relatively imports
  # `../src/*` so that the C sources can be shared among all target platforms.
  s.source           = { :path => '.' }
  s.source_files = 'Classes/**/*'
  s.dependency 'Flutter'
  s.platform = :ios, '13.0'

  # Check if we should disable opus/ogg support (must exist and be '1')
  disable_opus_ogg = !ENV['NO_OPUS_OGG_LIBS'].nil? && ENV['NO_OPUS_OGG_LIBS'] == '1'

  preprocessor_definitions = ['$(inherited)']
  if disable_opus_ogg
    preprocessor_definitions << 'NO_OPUS_OGG_LIBS'
  end

  s.compiler_flags = [
    '-w',
    '-DOS_OBJECT_USE_OBJC=0',
    '-Wno-format',
    '-lpthread',
    '-lm'
  ]

  # Flutter.framework does not contain a i386 slice.
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
      '$(PODS_TARGET_SRCROOT)/libs',
      '$(SRCROOT)/libs'
    ],
    'OTHER_LDFLAGS[sdk=iphonesimulator*]' => disable_opus_ogg ? '' : '-logg_iOS-simulator -lopus_iOS-simulator -lvorbis_iOS-simulator -lvorbisfile_iOS-simulator -lflac_iOS-simulator',
    'OTHER_LDFLAGS[sdk=iphoneos*]' => disable_opus_ogg ? '' : '-logg_iOS-device -lopus_iOS-device -lvorbis_iOS-device -lvorbisfile_iOS-device -lflac_iOS-device',
    "CLANG_CXX_LANGUAGE_STANDARD" => "c++17",
    "CLANG_CXX_LIBRARY" => "libc++"
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
