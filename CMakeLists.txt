cmake_minimum_required(VERSION 3.20)
project(glyphware VERSION 0.1.0 LANGUAGES CXX)

# glyphware — neutral font/glyph engine on FreeType + HarfBuzz.
# Host-agnostic: font bytes are supplied by a host-injected FontLoader; no
# storage/rendering dependency. Consumed by 吉里吉里Z core, Elements/ThorVG,
# layerExVector (see design plan). This library itself knows nothing about krkr.

option(GLYPHWARE_BUILD_TESTS "Build glyphware smoke/unit tests" ON)
# Install rules (glyphware.lib + include/glyphware). Parent projects embedding
# glyphware via add_subdirectory can set this to OFF to keep glyphware (and its
# bundled SheenBidi) out of their install tree.
option(GLYPHWARE_INSTALL "Generate install rules" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/utf-8 /W3 /permissive-)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

find_package(Freetype REQUIRED)

# HarfBuzz: reuse a `harfbuzz` target already provided by the parent project
# (the krkrz_dev umbrella's cmake/CommonExternals.cmake FetchContents HarfBuzz
# once and shares it across plugins to keep them ABI-consistent — so it must NOT
# also come from vcpkg). Only find it via the package manager when building
# glyphware standalone, i.e. when no such target exists. This mirrors how
# layerExVector / elements guard their own thorvg/harfbuzz with if(NOT TARGET).
if(NOT TARGET harfbuzz)
  find_package(harfbuzz CONFIG REQUIRED)
endif()

# SheenBidi (Apache-2.0) — the BiDi backend. Fetched and built as a single unity
# static library (its default). Swappable: see src/Bidi_sheenbidi.cpp.
# Fork (wtnbgo/SheenBidi, custom branch = v3.0.0 + SB_INSTALL option): upstream
# has unconditional install rules that would leak into the embedding project's
# install tree; the fork guards them behind SB_INSTALL.
include(FetchContent)
set(SB_CONFIG_UNITY ON CACHE BOOL "" FORCE)
set(BUILD_GENERATOR OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
if(NOT DEFINED SB_INSTALL)
  set(SB_INSTALL ${GLYPHWARE_INSTALL})
endif()
FetchContent_Declare(sheenbidi
  GIT_REPOSITORY https://github.com/wtnbgo/SheenBidi.git
  GIT_TAG c90c0949e33c6f9f1e52c3caf79929c8b1678503 # custom (v3.0.0 + SB_INSTALL)
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
  src/ColorGlyph.cpp
  src/Rasterize.cpp
  src/Shaper.cpp
  src/Registry.cpp
  src/Bidi_sheenbidi.cpp
  src/Layout.cpp
  src/Vertical.cpp
  src/Block.cpp
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

if(GLYPHWARE_INSTALL)
  include(GNUInstallDirs)
  install(TARGETS glyphware EXPORT glyphwareTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
