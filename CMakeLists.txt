cmake_minimum_required(VERSION 3.28)
project(tuxdmx VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TUXDMX_ENABLE_TESTS "Build tuxdmx tests" ON)
option(TUXDMX_ENABLE_PORTAUDIO "Enable real microphone input via PortAudio" ON)
option(TUXDMX_ENABLE_RTMIDI "Enable server-side MIDI input via RtMidi" ON)

# Resolve vcpkg root/triplet early so optional backends can use the same fallback path logic.
set(TUXDMX_VCPKG_ROOT "")
if(DEFINED ENV{VCPKG_ROOT} AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
  file(TO_CMAKE_PATH "$ENV{VCPKG_ROOT}" TUXDMX_VCPKG_ROOT)
endif()
if(NOT TUXDMX_VCPKG_ROOT AND DEFINED CMAKE_TOOLCHAIN_FILE)
  # Typical toolchain path: <vcpkg>/scripts/buildsystems/vcpkg.cmake
  file(TO_CMAKE_PATH "${CMAKE_TOOLCHAIN_FILE}" _toolchain_file_norm)
  get_filename_component(_tc_dir "${_toolchain_file_norm}" DIRECTORY)
  get_filename_component(_scripts_dir "${_tc_dir}" DIRECTORY)
  get_filename_component(TUXDMX_VCPKG_ROOT "${_scripts_dir}" DIRECTORY)
endif()

set(TUXDMX_VCPKG_TRIPLET "")
if(DEFINED VCPKG_TARGET_TRIPLET AND VCPKG_TARGET_TRIPLET)
  set(TUXDMX_VCPKG_TRIPLET "${VCPKG_TARGET_TRIPLET}")
elseif(DEFINED ENV{VCPKG_DEFAULT_TRIPLET} AND NOT "$ENV{VCPKG_DEFAULT_TRIPLET}" STREQUAL "")
  set(TUXDMX_VCPKG_TRIPLET "$ENV{VCPKG_DEFAULT_TRIPLET}")
elseif(WIN32)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(TUXDMX_VCPKG_TRIPLET "x64-windows")
  else()
    set(TUXDMX_VCPKG_TRIPLET "x86-windows")
  endif()
endif()

set(TUXDMX_SQLITE_TARGET "")

# 1) Try standard CMake module first.
find_package(SQLite3 QUIET)
if(SQLite3_FOUND AND TARGET SQLite::SQLite3)
  set(TUXDMX_SQLITE_TARGET SQLite::SQLite3)
endif()

# 2) Fallback: resolve sqlite3 manually from vcpkg install tree.
if(NOT TUXDMX_SQLITE_TARGET)
  if(TUXDMX_VCPKG_ROOT AND TUXDMX_VCPKG_TRIPLET)
    find_path(SQLITE3_INCLUDE_DIR_FALLBACK
      NAMES sqlite3.h
      PATHS "${TUXDMX_VCPKG_ROOT}/installed/${TUXDMX_VCPKG_TRIPLET}/include"
      NO_DEFAULT_PATH)
    find_library(SQLITE3_LIBRARY_FALLBACK
      NAMES sqlite3 sqlite3.lib
      PATHS "${TUXDMX_VCPKG_ROOT}/installed/${TUXDMX_VCPKG_TRIPLET}/lib"
      NO_DEFAULT_PATH)
    if(SQLITE3_INCLUDE_DIR_FALLBACK AND SQLITE3_LIBRARY_FALLBACK)
      add_library(SQLite3Fallback UNKNOWN IMPORTED)
      set_target_properties(SQLite3Fallback PROPERTIES
        IMPORTED_LOCATION "${SQLITE3_LIBRARY_FALLBACK}"
        INTERFACE_INCLUDE_DIRECTORIES "${SQLITE3_INCLUDE_DIR_FALLBACK}")
      set(TUXDMX_SQLITE_TARGET SQLite3Fallback)
    endif()
  endif()
endif()

if(NOT TUXDMX_SQLITE_TARGET)
  message(FATAL_ERROR "SQLite3 not found. Install sqlite3 via vcpkg and set CMAKE_TOOLCHAIN_FILE to vcpkg.cmake.")
endif()
find_package(Threads REQUIRED)
find_package(PkgConfig QUIET)

set(TUXDMX_SOURCES
    src/main.cpp
    src/app/app_controller.cpp
    src/audio/audio_engine.cpp
    src/db/database.cpp
    src/dmx/dmx_backend_factory.cpp
    src/dmx/dmx_engine.cpp
    src/dmx/enttec_dmx_pro.cpp
    src/http/http_server.cpp
    src/logger/logger.cpp
    src/midi/midi_engine.cpp
)

add_executable(tuxdmx ${TUXDMX_SOURCES})

target_include_directories(tuxdmx PRIVATE src/include)

target_link_libraries(tuxdmx PRIVATE ${TUXDMX_SQLITE_TARGET} Threads::Threads)

target_compile_definitions(tuxdmx PRIVATE TUXDMX_DEFAULT_WEB_ROOT="${CMAKE_SOURCE_DIR}/web")

set(TUXDMX_PORTAUDIO_ENABLED OFF)
if(TUXDMX_ENABLE_PORTAUDIO)
  if(PkgConfig_FOUND)
    pkg_check_modules(PORTAUDIO IMPORTED_TARGET QUIET portaudio-2.0)
    if(PORTAUDIO_FOUND)
      target_link_libraries(tuxdmx PRIVATE PkgConfig::PORTAUDIO)
      set(TUXDMX_PORTAUDIO_ENABLED ON)
    endif()
  endif()

  if(NOT TUXDMX_PORTAUDIO_ENABLED)
    find_package(portaudio CONFIG QUIET)
    if(TARGET portaudio)
      target_link_libraries(tuxdmx PRIVATE portaudio)
      set(TUXDMX_PORTAUDIO_ENABLED ON)
    elseif(TARGET portaudio_static)
      target_link_libraries(tuxdmx PRIVATE portaudio_static)
      set(TUXDMX_PORTAUDIO_ENABLED ON)
    elseif(TARGET PortAudio::PortAudio)
      target_link_libraries(tuxdmx PRIVATE PortAudio::PortAudio)
      set(TUXDMX_PORTAUDIO_ENABLED ON)
    endif()
  endif()

  if(NOT TUXDMX_PORTAUDIO_ENABLED AND WIN32 AND TUXDMX_VCPKG_ROOT AND TUXDMX_VCPKG_TRIPLET)
    find_path(PORTAUDIO_INCLUDE_DIR_FALLBACK
      NAMES portaudio.h
      PATHS "${TUXDMX_VCPKG_ROOT}/installed/${TUXDMX_VCPKG_TRIPLET}/include"
      NO_DEFAULT_PATH)
    find_library(PORTAUDIO_LIBRARY_FALLBACK
      NAMES portaudio portaudio_x64 portaudio.lib
      PATHS "${TUXDMX_VCPKG_ROOT}/installed/${TUXDMX_VCPKG_TRIPLET}/lib"
      NO_DEFAULT_PATH)
    if(PORTAUDIO_INCLUDE_DIR_FALLBACK AND PORTAUDIO_LIBRARY_FALLBACK)
      add_library(PortAudioFallback UNKNOWN IMPORTED)
      set_target_properties(PortAudioFallback PROPERTIES
        IMPORTED_LOCATION "${PORTAUDIO_LIBRARY_FALLBACK}"
        INTERFACE_INCLUDE_DIRECTORIES "${PORTAUDIO_INCLUDE_DIR_FALLBACK}")
      target_link_libraries(tuxdmx PRIVATE PortAudioFallback)
      set(TUXDMX_PORTAUDIO_ENABLED ON)
    endif()
  endif()

  if(TUXDMX_PORTAUDIO_ENABLED)
    target_compile_definitions(tuxdmx PRIVATE TUXDMX_WITH_PORTAUDIO=1)
    message(STATUS "Audio backend enabled: PortAudio")
  else()
    message(WARNING "PortAudio not found. Audio input devices will be unavailable and simulated fallback will be used.")
  endif()
endif()

set(TUXDMX_RTMIDI_ENABLED OFF)
if(TUXDMX_ENABLE_RTMIDI)
  if(PkgConfig_FOUND)
    pkg_check_modules(RTMIDI IMPORTED_TARGET QUIET rtmidi)
    if(RTMIDI_FOUND)
      target_link_libraries(tuxdmx PRIVATE PkgConfig::RTMIDI)
      set(TUXDMX_RTMIDI_ENABLED ON)
    endif()
  endif()

  if(NOT TUXDMX_RTMIDI_ENABLED)
    find_package(RtMidi CONFIG QUIET)
    if(TARGET RtMidi::rtmidi)
      target_link_libraries(tuxdmx PRIVATE RtMidi::rtmidi)
      set(TUXDMX_RTMIDI_ENABLED ON)
    elseif(TARGET rtmidi)
      target_link_libraries(tuxdmx PRIVATE rtmidi)
      set(TUXDMX_RTMIDI_ENABLED ON)
    elseif(TARGET rtmidi::rtmidi)
      target_link_libraries(tuxdmx PRIVATE rtmidi::rtmidi)
      set(TUXDMX_RTMIDI_ENABLED ON)
    endif()
  endif()

  if(NOT TUXDMX_RTMIDI_ENABLED AND WIN32 AND TUXDMX_VCPKG_ROOT AND TUXDMX_VCPKG_TRIPLET)
    find_path(RTMIDI_INCLUDE_DIR_FALLBACK
      NAMES RtMidi.h
      PATHS "${TUXDMX_VCPKG_ROOT}/installed/${TUXDMX_VCPKG_TRIPLET}/include"
      NO_DEFAULT_PATH)
    find_library(RTMIDI_LIBRARY_FALLBACK
      NAMES rtmidi rtmidi.lib
      PATHS "${TUXDMX_VCPKG_ROOT}/installed/${TUXDMX_VCPKG_TRIPLET}/lib"
      NO_DEFAULT_PATH)
    if(RTMIDI_INCLUDE_DIR_FALLBACK AND RTMIDI_LIBRARY_FALLBACK)
      add_library(RtMidiFallback UNKNOWN IMPORTED)
      set_target_properties(RtMidiFallback PROPERTIES
        IMPORTED_LOCATION "${RTMIDI_LIBRARY_FALLBACK}"
        INTERFACE_INCLUDE_DIRECTORIES "${RTMIDI_INCLUDE_DIR_FALLBACK}"
        INTERFACE_LINK_LIBRARIES "winmm")
      target_link_libraries(tuxdmx PRIVATE RtMidiFallback)
      set(TUXDMX_RTMIDI_ENABLED ON)
    endif()
  endif()

  if(TUXDMX_RTMIDI_ENABLED)
    target_compile_definitions(tuxdmx PRIVATE TUXDMX_WITH_RTMIDI=1)
    message(STATUS "MIDI backend enabled: RtMidi")
  else()
    message(WARNING "RtMidi not found. Server-side MIDI will be unavailable in this build.")
  endif()
endif()

if(APPLE AND NOT TUXDMX_RTMIDI_ENABLED)
  target_link_libraries(tuxdmx PRIVATE "-framework CoreMIDI" "-framework CoreFoundation")
  target_compile_definitions(tuxdmx PRIVATE TUXDMX_WITH_COREMIDI=1)
endif()

if(WIN32)
  target_link_libraries(tuxdmx PRIVATE ws2_32 setupapi)
endif()

if(TUXDMX_ENABLE_TESTS)
  enable_testing()
  add_executable(tuxdmx_db_tests tests/test_database.cpp src/db/database.cpp)
  target_include_directories(tuxdmx_db_tests PRIVATE src/include)
  target_link_libraries(tuxdmx_db_tests PRIVATE ${TUXDMX_SQLITE_TARGET})
  add_test(NAME tuxdmx_db_tests COMMAND tuxdmx_db_tests)

  add_executable(tuxdmx_dmx_engine_tests
      tests/test_dmx_engine.cpp
      src/dmx/dmx_engine.cpp
      src/dmx/dmx_backend_factory.cpp
      src/dmx/enttec_dmx_pro.cpp
      src/logger/logger.cpp)
  target_include_directories(tuxdmx_dmx_engine_tests PRIVATE src/include)
  target_link_libraries(tuxdmx_dmx_engine_tests PRIVATE Threads::Threads)
  if(WIN32)
    target_link_libraries(tuxdmx_dmx_engine_tests PRIVATE ws2_32 setupapi)
  endif()
  add_test(NAME tuxdmx_dmx_engine_tests COMMAND tuxdmx_dmx_engine_tests)
endif()
