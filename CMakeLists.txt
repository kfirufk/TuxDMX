cmake_minimum_required(VERSION 3.28)
project(tuxdmx VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TUXDMX_ENABLE_TESTS "Build tuxdmx tests" ON)
option(TUXDMX_ENABLE_PORTAUDIO "Enable real microphone input via PortAudio" ON)

find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig QUIET)

set(TUXDMX_SOURCES
    src/main.cpp
    src/app/app_controller.cpp
    src/audio/audio_engine.cpp
    src/db/database.cpp
    src/dmx/dmx_engine.cpp
    src/dmx/enttec_dmx_pro.cpp
    src/http/http_server.cpp
    src/logger/logger.cpp
)

add_executable(tuxdmx ${TUXDMX_SOURCES})

target_include_directories(tuxdmx PRIVATE src/include)

target_link_libraries(tuxdmx PRIVATE SQLite::SQLite3 Threads::Threads)

target_compile_definitions(tuxdmx PRIVATE TUXDMX_DEFAULT_WEB_ROOT="${CMAKE_SOURCE_DIR}/web")

if(TUXDMX_ENABLE_PORTAUDIO AND PkgConfig_FOUND)
  pkg_check_modules(PORTAUDIO IMPORTED_TARGET portaudio-2.0)
  if(PORTAUDIO_FOUND)
    target_link_libraries(tuxdmx PRIVATE PkgConfig::PORTAUDIO)
    target_compile_definitions(tuxdmx PRIVATE TUXDMX_WITH_PORTAUDIO=1)
  endif()
endif()

if(WIN32)
  target_link_libraries(tuxdmx PRIVATE ws2_32)
endif()

if(TUXDMX_ENABLE_TESTS)
  enable_testing()
  add_executable(tuxdmx_db_tests tests/test_database.cpp src/db/database.cpp)
  target_include_directories(tuxdmx_db_tests PRIVATE src/include)
  target_link_libraries(tuxdmx_db_tests PRIVATE SQLite::SQLite3)
  add_test(NAME tuxdmx_db_tests COMMAND tuxdmx_db_tests)
endif()
