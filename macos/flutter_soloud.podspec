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
  s.source_files     = 'Classes/**/*.{h,mm}'
  s.dependency 'FlutterMacOS'
  s.platform = :osx, '10.15'

  # Check if opus and ogg libraries are available
  has_opus_ogg = system("[ -f /usr/local/lib/libogg.dylib -o -f /opt/homebrew/lib/libogg.dylib ] && [ -f /usr/local/lib/libopus.dylib -o -f /opt/homebrew/lib/libopus.dylib ]")

  preprocessor_definitions = ['$(inherited)']
  if has_opus_ogg
    preprocessor_definitions << 'LIBOPUS_OGG_AVAILABLE=1'
  end

  s.pod_target_xcconfig = { 
    'HEADER_SEARCH_PATHS' => [
      '/usr/local/include', # For Intel Macs
      '/opt/homebrew/include', # For Silicon Macs
      '$(PODS_TARGET_SRCROOT)/../src',
      '$(PODS_TARGET_SRCROOT)/../src/soloud/include',
    ],
    'GCC_PREPROCESSOR_DEFINITIONS' => preprocessor_definitions.join(' '),
    'DEFINES_MODULE' => 'YES', 
    'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'i386',
    "CLANG_CXX_LANGUAGE_STANDARD" => "c++17",
    "CLANG_CXX_LIBRARY" => "libc++",
    'OTHER_LDFLAGS' => has_opus_ogg ? '-L/usr/local/lib -L/opt/homebrew/lib -logg -lopus' : ''
    'OTHER_CFLAGS' => '-msse -msse2 -msse3 -msse4.1 -O3 -ffast-math -flto',
    'OTHER_CPLUSPLUSFLAGS' => '-msse -msse2 -msse3 -msse4.1 -O3 -ffast-math -flto'
   }
   
  s.swift_version = '5.0'
  s.ios.framework  = ['AudioToolbox', 'AVFAudio']

end
