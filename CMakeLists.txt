cmake_minimum_required(VERSION 3.28)
project(tuxdmx VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TUXDMX_ENABLE_TESTS "Build tuxdmx tests" ON)
option(TUXDMX_ENABLE_PORTAUDIO "Enable real microphone input via PortAudio" ON)
option(TUXDMX_ENABLE_RTMIDI "Enable server-side MIDI input via RtMidi" ON)

find_package(SQLite3 QUIET)
set(TUXDMX_SQLITE_TARGET "")
if(SQLite3_FOUND)
  set(TUXDMX_SQLITE_TARGET SQLite::SQLite3)
else()
  # vcpkg on Windows commonly exposes sqlite as unofficial-sqlite3.
  find_package(unofficial-sqlite3 CONFIG REQUIRED)
  set(TUXDMX_SQLITE_TARGET unofficial::sqlite3::sqlite3)
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

if(TUXDMX_ENABLE_PORTAUDIO AND PkgConfig_FOUND)
  pkg_check_modules(PORTAUDIO IMPORTED_TARGET portaudio-2.0)
  if(PORTAUDIO_FOUND)
    target_link_libraries(tuxdmx PRIVATE PkgConfig::PORTAUDIO)
    target_compile_definitions(tuxdmx PRIVATE TUXDMX_WITH_PORTAUDIO=1)
  endif()
endif()

if(TUXDMX_ENABLE_RTMIDI AND PkgConfig_FOUND)
  pkg_check_modules(RTMIDI IMPORTED_TARGET rtmidi)
  if(RTMIDI_FOUND)
    target_link_libraries(tuxdmx PRIVATE PkgConfig::RTMIDI)
    target_compile_definitions(tuxdmx PRIVATE TUXDMX_WITH_RTMIDI=1)
  endif()
endif()

if(APPLE AND NOT RTMIDI_FOUND)
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
