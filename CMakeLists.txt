cmake_minimum_required(VERSION 3.25)
project(visio_ai LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

find_package(PkgConfig REQUIRED)
pkg_check_modules(X11     REQUIRED IMPORTED_TARGET x11)
pkg_check_modules(XEXT    REQUIRED IMPORTED_TARGET xext)
pkg_check_modules(XDAMAGE REQUIRED IMPORTED_TARGET xdamage)
pkg_check_modules(XTST    REQUIRED IMPORTED_TARGET xtst)
pkg_check_modules(XI      REQUIRED IMPORTED_TARGET xi)
pkg_check_modules(XFIXES  REQUIRED IMPORTED_TARGET xfixes)

# --- noyau : aucune socket, aucun protocole, seulement la machine -------
add_library(visio_core STATIC
  src/capture/screen_info.cpp
  src/capture/damage_watch.cpp
  src/capture/shm_capture.cpp
  src/core/tile_diff.cpp
  src/perceive/windows.cpp
  src/perceive/regions.cpp
)
target_include_directories(visio_core PUBLIC src)
target_link_libraries(visio_core PUBLIC
  PkgConfig::X11 PkgConfig::XEXT PkgConfig::XDAMAGE PkgConfig::XFIXES)

# --- binaire principal --------------------------------------------------
add_executable(visio src/main.cpp)
target_link_libraries(visio PRIVATE visio_core)

# --- bancs --------------------------------------------------------------
add_executable(bench_capture bench/bench_capture.cpp)
target_link_libraries(bench_capture PRIVATE visio_core)

# tesseract : la bibliothèque est là (libtesseract.so.5), les en-têtes non.
# On ne tire pas les 44 paquets de libtesseract-dev : le banc déclare l'API C qu'il utilise.
find_library(TESSERACT_LIB NAMES tesseract.so.5 libtesseract.so.5
             PATHS /usr/lib/x86_64-linux-gnu NO_DEFAULT_PATH)
add_executable(bench_ocr bench/bench_ocr.cpp)
if(TESSERACT_LIB)
  target_link_libraries(bench_ocr PRIVATE ${TESSERACT_LIB})
  message(STATUS "tesseract : ${TESSERACT_LIB}")
else()
  message(WARNING "tesseract absent : bench_ocr non lie")
endif()

add_executable(bench_damage bench/bench_damage.cpp)
target_link_libraries(bench_damage PRIVATE visio_core PkgConfig::XDAMAGE PkgConfig::XFIXES)

# --- tests --------------------------------------------------------------
enable_testing()
add_executable(unit_rect tests/unit_rect.cpp)
target_link_libraries(unit_rect PRIVATE visio_core)
add_test(NAME unit_rect COMMAND unit_rect)

add_executable(unit_merge tests/unit_merge.cpp)
target_link_libraries(unit_merge PRIVATE visio_core)
add_test(NAME unit_merge COMMAND unit_merge)

add_executable(unit_tile_diff tests/unit_tile_diff.cpp)
target_link_libraries(unit_tile_diff PRIVATE visio_core)
add_test(NAME unit_tile_diff COMMAND unit_tile_diff)

add_executable(unit_regions tests/unit_regions.cpp)
target_link_libraries(unit_regions PRIVATE visio_core)
add_test(NAME unit_regions COMMAND unit_regions)

add_executable(integration_windows tests/integration_windows.cpp)
target_link_libraries(integration_windows PRIVATE visio_core)
add_test(NAME integration_windows COMMAND integration_windows)
set_tests_properties(integration_windows PROPERTIES SKIP_RETURN_CODE 77)

add_executable(integration_damage tests/integration_damage.cpp)
target_link_libraries(integration_damage PRIVATE visio_core)
add_test(NAME integration_damage COMMAND integration_damage)
set_tests_properties(integration_damage PROPERTIES SKIP_RETURN_CODE 77)
