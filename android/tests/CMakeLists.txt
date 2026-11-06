cmake_minimum_required(VERSION 3.22)
project(PADCheaperAndroidReaderTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
enable_testing()
set(READER "${CMAKE_CURRENT_LIST_DIR}/../app/src/main/cpp")
add_executable(library_inventory_tests library_inventory_tests.cpp "${READER}/LibraryInventory.cpp")
target_include_directories(library_inventory_tests PRIVATE "${READER}")
target_compile_options(library_inventory_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME library_inventory_tests COMMAND library_inventory_tests)

add_executable(runtime_code_capture_tests runtime_code_capture_tests.cpp
    "${READER}/RuntimeCodeCapture.cpp" "${READER}/LibraryInventory.cpp")
target_include_directories(runtime_code_capture_tests PRIVATE "${READER}")
target_compile_options(runtime_code_capture_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME runtime_code_capture_tests COMMAND runtime_code_capture_tests)

add_executable(board_layout_tests board_layout_tests.cpp "${READER}/BoardLayout.cpp")
target_include_directories(board_layout_tests PRIVATE "${READER}")
target_compile_options(board_layout_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME board_layout_tests COMMAND board_layout_tests)

# Pure owned-data adapter cases, with the same shared solver implementation.
# Configuring/building/running this suite still requires an explicit test request.
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../core" "${CMAKE_CURRENT_BINARY_DIR}/core")
add_executable(board_preview_tests board_preview_tests.cpp "${READER}/BoardPreview.cpp")
target_include_directories(board_preview_tests PRIVATE "${READER}")
target_link_libraries(board_preview_tests PRIVATE padcheaper_core)
target_compile_options(board_preview_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME board_preview_tests COMMAND board_preview_tests)
