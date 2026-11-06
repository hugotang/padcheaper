cmake_minimum_required(VERSION 3.22)
project(PADCheaperCore LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Incremental extraction: files stay at their verified iOS paths for now.
set(PH_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../ios/src")
add_library(padcheaper_core STATIC
    "${PH_SOURCE}/PHInputModel.c" "${PH_SOURCE}/PHBattleModel.c"
    "${PH_SOURCE}/PHOrbModel.c" "${PH_SOURCE}/PHOrbSolver.cpp")
target_include_directories(padcheaper_core PUBLIC "${PH_SOURCE}")
target_compile_options(padcheaper_core PRIVATE -Wall -Wextra -Werror)
# Match the established solver verification optimization. GCC 15's -O3 reports
# an array-bound warning in EvaluateStatic despite public goal-count validation;
# keep that configuration unverified rather than disabling its warning.
target_compile_options(padcheaper_core PRIVATE "$<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU>>:-O2>")

option(PH_BUILD_CORE_TESTS "Build existing solver regression executable (explicit test request only)" OFF)
if(PH_BUILD_CORE_TESTS)
    enable_testing()
    add_executable(padcheaper_core_tests "${CMAKE_CURRENT_LIST_DIR}/../ios/tests/orb_solver_tests.cpp")
    target_link_libraries(padcheaper_core_tests PRIVATE padcheaper_core)
    target_compile_options(padcheaper_core_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME padcheaper_core_tests COMMAND padcheaper_core_tests)
    # The private-score test includes PHOrbSolver.cpp itself; link only its
    # model dependencies so no duplicate solver implementation is introduced.
    add_executable(padcheaper_shape_guidance_tests
        "${CMAKE_CURRENT_LIST_DIR}/../ios/tests/orb_shape_guidance_tests.cpp"
        "${PH_SOURCE}/PHInputModel.c" "${PH_SOURCE}/PHBattleModel.c" "${PH_SOURCE}/PHOrbModel.c")
    target_include_directories(padcheaper_shape_guidance_tests PRIVATE "${PH_SOURCE}")
    target_compile_options(padcheaper_shape_guidance_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG
        "$<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU>>:-O2>")
    add_test(NAME padcheaper_shape_guidance_tests COMMAND padcheaper_shape_guidance_tests)
endif()
