# Presets and PNG metadata, shared by the CLI and the GUI.
find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

add_library(glic_preset STATIC preset.cpp png_meta.cpp)
target_include_directories(glic_preset PUBLIC .)
target_link_libraries(glic_preset PUBLIC glic::glic glic_cli_spec nlohmann_json::nlohmann_json)
target_compile_options(glic_preset PRIVATE -Wall -Wextra)
