cmake_minimum_required(VERSION 3.22)
project(PADCheaperAndroid LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(padcheaper_runtime SHARED NativeBridge.cpp LibraryInventory.cpp)
target_compile_options(padcheaper_runtime PRIVATE -Wall -Wextra -Werror -fvisibility=hidden)
target_link_options(padcheaper_runtime PRIVATE
    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map" "-Wl,-z,max-page-size=16384")
set_property(TARGET padcheaper_runtime APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/exports.map")

# Build the existing solver from the same sources. No second Android fork.
# This archive has no game bridge and is not yet exposed to the live probe.
get_filename_component(PADCHEAPER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
add_subdirectory("${PADCHEAPER_ROOT}/core" "${CMAKE_CURRENT_BINARY_DIR}/core")
