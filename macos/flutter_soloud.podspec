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
  s.source_files     = 'Classes/**/*.{h,mm}'
  s.dependency 'FlutterMacOS'
  s.platform = :osx, '10.15'

  # Check if we should disable opus/ogg support (must exist and be '1')
  disable_opus_ogg = !ENV['NO_OPUS_OGG_LIBS'].nil? && ENV['NO_OPUS_OGG_LIBS'] == '1'
  
  local_lib_path = '$(PODS_TARGET_SRCROOT)/libs'
  local_include_path = '$(PODS_TARGET_SRCROOT)/include'

  preprocessor_definitions = ['$(inherited)']
  if disable_opus_ogg
    preprocessor_definitions << 'NO_OPUS_OGG_LIBS'
  end

  s.pod_target_xcconfig = { 
    'HEADER_SEARCH_PATHS' => [
      local_include_path,
      '$(PODS_TARGET_SRCROOT)/../src',
      '$(PODS_TARGET_SRCROOT)/../src/soloud/include',
    ],
    'GCC_PREPROCESSOR_DEFINITIONS' => preprocessor_definitions.join(' '),
    'DEFINES_MODULE' => 'YES', 
    'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'i386',
    "CLANG_CXX_LANGUAGE_STANDARD" => "c++17",
    "CLANG_CXX_LIBRARY" => "libc++",
    'OTHER_LDFLAGS' => disable_opus_ogg ? '' : "-L#{local_lib_path} -logg -lopus",
    'OTHER_CFLAGS' => "-O3 -ffast-math -flto",
    'OTHER_CPLUSPLUSFLAGS' => "-O3 -ffast-math -flto",
    'VALID_ARCHS' => 'x86_64 arm64',
   }
   
  s.swift_version = '5.0'
  s.ios.framework  = ['AudioToolbox', 'AVFAudio']

end
