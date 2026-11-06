cmake_minimum_required(VERSION 3.22)
project(PADCheaperAndroid LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(padcheaper_runtime SHARED NativeBridge.cpp LibraryInventory.cpp RuntimeCodeCapture.cpp
    BoardLayout.cpp BoardPreview.cpp)
target_compile_options(padcheaper_runtime PRIVATE -Wall -Wextra -Werror -fvisibility=hidden)
target_link_options(padcheaper_runtime PRIVATE
    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map" "-Wl,-z,max-page-size=16384")
set_property(TARGET padcheaper_runtime APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/exports.map")

# Build the existing solver from the same sources. No second Android fork.
# The pure-data preview adapter links this archive. No JNI/live-board entry
# point calls it. The offline raw decoder has no bridge into normalized preview
# input; live memory collection and board semantics remain unverified.
get_filename_component(PADCHEAPER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
add_subdirectory("${PADCHEAPER_ROOT}/core" "${CMAKE_CURRENT_BINARY_DIR}/core")
target_link_libraries(padcheaper_runtime PRIVATE padcheaper_core)
