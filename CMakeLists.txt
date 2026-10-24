cmake_minimum_required(VERSION 3.20)
project(glic-studio VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(GLIC_SANITIZE "Build with AddressSanitizer and UBSan" OFF)
option(GLIC_BUILD_TESTS "Build tests" ON)
option(GLIC_BUILD_APP "Build the glic-studio GUI (needs SDL3, OpenGL)" ON)

if(GLIC_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(libglic)
add_subdirectory(glic-cli)
add_subdirectory(app)

if(GLIC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
