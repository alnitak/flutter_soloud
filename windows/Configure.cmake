# copied from souloud/contrib to set these parameters for linux

include (${CMAKE_CURRENT_SOURCE_DIR}/../src/soloud/contrib/cmake/OptionDependentOnPackage.cmake)
include (${CMAKE_CURRENT_SOURCE_DIR}/../src/soloud/contrib/cmake/PrintOptionStatus.cmake)

option (SOLOUD_DYNAMIC "Set to ON to build dynamic SoLoud" ON)
print_option_status (SOLOUD_DYNAMIC "Build dynamic library")

option (SOLOUD_STATIC "Set to ON to build static SoLoud" OFF)
print_option_status (SOLOUD_STATIC "Build static library")

option (SOLOUD_C_API "Set to ON to include the C API" OFF)
print_option_status (SOLOUD_C_API "Build C API")


option (SOLOUD_BACKEND_MINIAUDIO "Set to ON to include MiniAudio" ON)
print_option_status (SOLOUD_BACKEND_MINIAUDIO "Build MiniAudio")

option (SOLOUD_BUILD_DEMOS "Set to ON for building demos" OFF)
print_option_status (SOLOUD_BUILD_DEMOS "Build demos")

option (SOLOUD_BACKEND_NULL "Set to ON for building NULL backend" ON)
print_option_status (SOLOUD_BACKEND_NULL "NULL backend")

option (SOLOUD_BACKEND_SDL2 "Set to ON for building SDL2 backend" OFF)
print_option_status (SOLOUD_BACKEND_SDL2 "SDL2 backend")

option (SOLOUD_BACKEND_ALSA "Set to ON for building ALSA backend" OFF)
print_option_status (SOLOUD_BACKEND_ALSA "ALSA backend")

option (SOLOUD_BACKEND_COREAUDIO "Set to ON for building CoreAudio backend" OFF)
print_option_status (SOLOUD_BACKEND_COREAUDIO "CoreAudio backend")

option (SOLOUD_BACKEND_OPENSLES "Set to ON for building OpenSLES backend" OFF)
print_option_status (SOLOUD_BACKEND_OPENSLES "OpenSLES backend")

option (SOLOUD_BACKEND_XAUDIO2 "Set to ON for building XAudio2 backend" OFF)
print_option_status (SOLOUD_BACKEND_XAUDIO2 "XAudio2 backend")

option (SOLOUD_BACKEND_WINMM "Set to ON for building WINMM backend" ON)
print_option_status (SOLOUD_BACKEND_WINMM "WINMM backend")

option (SOLOUD_BACKEND_WASAPI "Set to ON for building WASAPI backend" OFF)
print_option_status (SOLOUD_BACKEND_WASAPI "WASAPI backend")

option (SOLOUD_GENERATE_GLUE "Set to ON for generating the Glue APIs" OFF)
print_option_status (SOLOUD_GENERATE_GLUE "Generate Glue")
