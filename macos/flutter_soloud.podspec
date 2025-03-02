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

  # Check if all opus and ogg libraries are available (both x86_64 and arm64)
  # To get these libraries, you'll need to run `brew` both in arm64 mode
  # and in x86_64 mode.
  # See https://github.com/alnitak/flutter_soloud/issues/191#issuecomment-2692671697
  #
  # This is a temporary fix just to get things building.
  #
  # IMPORTANT: Apps won't work correctly on user's machines unless they happen
  # to have libopus and libogg installed.
  has_opus_ogg = system("[ -f /usr/local/lib/libogg.dylib] && [ -f /opt/homebrew/lib/libogg.dylib ] && [ -f /usr/local/lib/libopus.dylib] && [ -f /opt/homebrew/lib/libopus.dylib ]")

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
    'OTHER_LDFLAGS[arch=arm64]' => has_opus_ogg ? '-L/opt/homebrew/lib -logg -lopus' : '',
    'OTHER_LDFLAGS[arch=x86_64]' => has_opus_ogg ? '-L/usr/local/lib -logg -lopus' : '',
    'OTHER_CFLAGS' => '-msse -msse2 -msse3 -msse4.1 -O3 -ffast-math -flto',
    'OTHER_CPLUSPLUSFLAGS' => '-msse -msse2 -msse3 -msse4.1 -O3 -ffast-math -flto',
    'VALID_ARCHS' => 'x86_64 arm64',
   }
   
  s.swift_version = '5.0'
  s.ios.framework  = ['AudioToolbox', 'AVFAudio']

end
