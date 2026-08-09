cmake_minimum_required(VERSION 3.20)
project(glyphware VERSION 0.1.0 LANGUAGES CXX)

# glyphware — neutral font/glyph engine on FreeType + HarfBuzz.
# Host-agnostic: font bytes are supplied by a host-injected FontLoader; no
# storage/rendering dependency. Consumed by 吉里吉里Z core, Elements/ThorVG,
# layerExVector (see design plan). This library itself knows nothing about krkr.

option(GLYPHWARE_BUILD_TESTS "Build glyphware smoke/unit tests" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/utf-8 /W3 /permissive-)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

find_package(Freetype REQUIRED)
find_package(harfbuzz CONFIG REQUIRED)

# SheenBidi (Apache-2.0) — the BiDi backend. Fetched and built as a single unity
# static library (its default). Swappable: see src/Bidi_sheenbidi.cpp.
include(FetchContent)
set(SB_CONFIG_UNITY ON CACHE BOOL "" FORCE)
set(BUILD_GENERATOR OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(sheenbidi
  GIT_REPOSITORY https://github.com/Tehreer/SheenBidi.git
  GIT_TAG v3.0.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(sheenbidi)

# picojson (BSD-2-Clause) — header-only JSON, for fonts.json manifest parsing.
FetchContent_Declare(picojson
  GIT_REPOSITORY https://github.com/kazuho/picojson.git
  GIT_TAG v1.3.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(picojson)

add_library(glyphware
  src/Library.cpp
  src/Face.cpp
  src/Shaper.cpp
  src/Registry.cpp
  src/Bidi_sheenbidi.cpp
  src/Layout.cpp
  src/Manifest.cpp
)
target_include_directories(glyphware PRIVATE ${picojson_SOURCE_DIR})
add_library(glyphware::glyphware ALIAS glyphware)

target_include_directories(glyphware
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(glyphware
  PUBLIC
    Freetype::Freetype
  PRIVATE
    harfbuzz::harfbuzz
    SheenBidi::SheenBidi
)

if(GLYPHWARE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

include(GNUInstallDirs)
install(TARGETS glyphware EXPORT glyphwareTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
