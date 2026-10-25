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
    test_studio.cpp
    test_preset.cpp
    test_corrupt.cpp
)
target_link_libraries(glic_tests PRIVATE glic::glic glic_cli_spec glic_preset studio_core doctest::doctest)
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

add_test(NAME libglic COMMAND glic_tests --test-suite-exclude=fuzz)
set_tests_properties(libglic PROPERTIES FIXTURES_REQUIRED baseline)

# Design §8: the corruption fuzz, on its own so it can be run alone (also under ASan).
add_test(NAME corruption_fuzz COMMAND glic_tests --test-suite=fuzz)


# End to end through the CLI: encode a few baseline cases and compare bytes.
add_test(NAME cli_golden
    COMMAND ${PROJECT_SOURCE_DIR}/tools/check_cli.sh $<TARGET_FILE:glic-cli> ${BASELINE_OUT})
set_tests_properties(cli_golden PROPERTIES FIXTURES_REQUIRED baseline)
