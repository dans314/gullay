# Gullay: CLAP, VST3 and AU (macOS) / CLAP and VST3 (Windows), built with clap-wrapper.
#
# macOS:  sh build-all-mac.sh    (needs CMake: brew install cmake; the first build downloads the SDKs)
# Windows: built by GitHub Actions (.github/workflows/build.yml); by hand:
#          cmake -B build -A x64 -T ClangCL -DWEBVIEW2_DIR=<unpacked Microsoft.Web.WebView2 NuGet package>
# Linux:  cmake -B build-cmake && cmake --build build-cmake    (CLAP and VST3, no window)
cmake_minimum_required(VERSION 3.21)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Oldest macOS")
# Windows: build everything against the static C++ runtime (/MT), so the plugin needs no extra installs
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
project(Gullay VERSION 0.8.2 LANGUAGES C CXX)
if(APPLE)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)
# clap and clap-wrapper (MIT). clap-wrapper downloads the VST3 SDK and Apple's AudioUnitSDK itself.
set(CLAP_WRAPPER_OUTPUT_NAME clap-wrapper-target)
set(CLAP_WRAPPER_DONT_ADD_TARGETS TRUE)
set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES TRUE)
FetchContent_Declare(clap GIT_REPOSITORY https://github.com/free-audio/clap.git GIT_TAG 1.2.7 GIT_SHALLOW ON)
FetchContent_Declare(clap-wrapper GIT_REPOSITORY https://github.com/free-audio/clap-wrapper.git
                     GIT_TAG 2a4da4db3665ca18dd0830c5dec4303d3765efec)
FetchContent_MakeAvailable(clap clap-wrapper)

# the window page, embedded as a header
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_gui.py
                  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# ----- the plugin, as a static library the formats are built from
file(GLOB CLOUDSEED_DSP ${CMAKE_CURRENT_SOURCE_DIR}/libs/cloudseed/DSP/*.cpp)
add_library(gullay-impl STATIC
  src/gy_clap.cpp
  src/gy_reverb.cpp
  # Valley Plateau (GPL3): the Dattorro tank
  libs/valley/Plateau/Dattorro.cpp
  libs/valley/dsp/filters/OnePoleFilters.cpp
  libs/valley/utilities/Utilities.cpp
  # Cloud Seed (MIT)
  libs/cloudseed/Parameters.cpp
  ${CLOUDSEED_DSP}
  # 4ms Tapographic Delay (GPL3 / MIT): the tap allocator, its tables and stmlib's pieces
  libs/fourms/tapo/tap_allocator.cc
  libs/fourms/tapo/resources.cc
  libs/fourms/tapo/stmlib/utils/random.cc
  libs/fourms/tapo/stmlib/dsp/units.cc)
target_include_directories(gullay-impl PUBLIC
  src libs/sst-filters-include libs/sst-basic-blocks-include libs/simde
  libs/fourms libs/fourms/tapo libs/valley libs/cloudseed)
target_link_libraries(gullay-impl PUBLIC clap)
# TEST: the Tapographic code's portable C path (no ARM assembly); GY_SEPARATE_ENTRY: the entry is in gy_entry.cpp
target_compile_definitions(gullay-impl PUBLIC GY_SEPARATE_ENTRY=1 TEST=1)

if(APPLE)
  string(TIMESTAMP GY_BUILD_TAG "%s")
  target_sources(gullay-impl PRIVATE src/gy_gui_mac.mm)
  set_source_files_properties(src/gy_gui_mac.mm PROPERTIES COMPILE_FLAGS "-fno-objc-arc -DGY_OBJC_SUFFIX=${GY_BUILD_TAG}")
  target_link_libraries(gullay-impl PUBLIC "-framework Cocoa" "-framework WebKit")
  set(GY_FORMATS CLAP VST3 AUV2)
elseif(WIN32)
  # WebView2 SDK: the Microsoft.Web.WebView2 NuGet package, unpacked (the workflow downloads it)
  set(WEBVIEW2_DIR "" CACHE PATH "Unpacked Microsoft.Web.WebView2 NuGet package")
  if(NOT WEBVIEW2_DIR)
    message(FATAL_ERROR "Set WEBVIEW2_DIR to the unpacked Microsoft.Web.WebView2 NuGet package")
  endif()
  target_sources(gullay-impl PRIVATE src/gy_gui_win.cpp)
  target_include_directories(gullay-impl PRIVATE ${WEBVIEW2_DIR}/build/native/include)
  target_link_libraries(gullay-impl PUBLIC ${WEBVIEW2_DIR}/build/native/x64/WebView2LoaderStatic.lib version shlwapi ole32 shell32 uuid)
  # _USE_MATH_DEFINES: Microsoft's <cmath> hides M_PI without it
  target_compile_definitions(gullay-impl PUBLIC _USE_MATH_DEFINES NOMINMAX _CRT_SECURE_NO_WARNINGS)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(gullay-impl PRIVATE /clang:-ferror-limit=0)
  endif()
  set(GY_FORMATS CLAP VST3)
else()
  set(GY_FORMATS CLAP VST3)
endif()

make_clapfirst_plugins(
  TARGET_NAME gullay
  IMPL_TARGET gullay-impl
  OUTPUT_NAME "Gullay"
  ENTRY_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/gy_entry.cpp"
  BUNDLE_IDENTIFIER "com.otiskanno.gullay"
  BUNDLE_VERSION ${PROJECT_VERSION}
  WINDOWS_FOLDER_VST3 TRUE
  PLUGIN_FORMATS ${GY_FORMATS}
  COPY_AFTER_BUILD FALSE
  AUV2_MANUFACTURER_NAME "Otis Kanno"
  AUV2_MANUFACTURER_CODE "OtKa"
  AUV2_SUBTYPE_CODE "Gula"
  AUV2_INSTRUMENT_TYPE "aumf" # a music effect: it takes MIDI (key, patterns, Learn)
)

# The host test (tests/gy_host_test.cpp) as a program, for CI: cmake -DGY_HOST_TEST=ON
option(GY_HOST_TEST "Build the host test program" OFF)
if(GY_HOST_TEST)
  add_executable(gy_host_test tests/gy_host_test.cpp)
  target_include_directories(gy_host_test PRIVATE libs/clap-main/include)
  if(UNIX)
    target_link_libraries(gy_host_test PRIVATE dl)
  endif()
endif()
