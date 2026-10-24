include(FetchContent)
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.12
    GIT_SHALLOW TRUE
)
set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(doctest)

set(BASELINE_SRC ${PROJECT_SOURCE_DIR}/tests/baseline)
set(BASELINE_OUT ${CMAKE_CURRENT_BINARY_DIR}/baseline)

add_executable(glic_tests
    main.cpp
    test_golden.cpp
    test_libglic.cpp
)
target_link_libraries(glic_tests PRIVATE glic::glic glic_cli_args doctest::doctest)
target_compile_definitions(glic_tests PRIVATE
    GLIC_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    GLIC_BASELINE_DIR="${BASELINE_OUT}"
)

# The upstream .glic files ship as one tarball; unpack next to the tests.
file(MAKE_DIRECTORY ${BASELINE_OUT})
add_test(NAME baseline_unpack
    COMMAND ${CMAKE_COMMAND} -E tar xJf ${BASELINE_SRC}/glic.tar.xz
    WORKING_DIRECTORY ${BASELINE_OUT}
)
set_tests_properties(baseline_unpack PROPERTIES FIXTURES_SETUP baseline)

add_test(NAME libglic COMMAND glic_tests)
set_tests_properties(libglic PROPERTIES FIXTURES_REQUIRED baseline)

# End to end: the CLI binary regenerates the upstream baseline byte-for-byte.
add_test(NAME cli_baseline
    COMMAND ${PROJECT_SOURCE_DIR}/tools/check_baseline.sh $<TARGET_FILE:glic-cli> tests/baseline)
add_test(NAME cli_provisional
    COMMAND ${PROJECT_SOURCE_DIR}/tools/check_baseline.sh $<TARGET_FILE:glic-cli> tests/golden-provisional)
