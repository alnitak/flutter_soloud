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
  s.platform = :ios, '11.0'

  s.compiler_flags      = [
    '-GCC_WARN_INHIBIT_ALL_WARNINGS',
    '-w',
    '-DOS_OBJECT_USE_OBJC=0', '-Wno-format',
    '-lpthread',
    '-lm'
  ]
  # Flutter.framework does not contain a i386 slice.
  s.pod_target_xcconfig = { 
    # Enable equivalent of '-Isrc/include' to make '#include <openssl/...>' work
    'HEADER_SEARCH_PATHS' => [
      '$(PODS_TARGET_SRCROOT)/../src',
      '$(PODS_TARGET_SRCROOT)/../src/soloud/include',
    ],
    'GCC_PREPROCESSOR_DEFINITIONS' => '$(inherited)',
    'DEFINES_MODULE' => 'YES', 
    'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'i386' 
  }
  s.swift_version = '5.0'
  # spec.framework      = 'SystemConfiguration'
  s.ios.framework  = ['AudioToolbox', 'AVFAudio']
  # spec.osx.framework  = 'AppKit'
end
