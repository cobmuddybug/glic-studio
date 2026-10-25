# studio_core: app logic with no UI or platform dependencies (unit-tested).
add_library(studio_core STATIC
    model/animation.cpp
    model/explore.cpp
    model/history.cpp
    model/preview_scheduler.cpp
    model/params.cpp
    render/animation_export.cpp
    render/batch.cpp
    render/overlay.cpp
    render/resize.cpp
    render/worker.cpp
)
target_include_directories(studio_core PUBLIC .)
target_link_libraries(studio_core PUBLIC glic::glic glic_preset)
find_package(Threads REQUIRED)
target_link_libraries(studio_core PUBLIC Threads::Threads)
target_compile_options(studio_core PRIVATE -Wall -Wextra)

if(NOT GLIC_BUILD_APP)
    return()
endif()

find_package(SDL3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

include(FetchContent)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9b-docking
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3)
target_compile_options(imgui PRIVATE -w)

add_executable(glic-studio
    main.cpp
    app.cpp
    platform/platform.cpp
    render/texture.cpp
    ui/animate_tab.cpp
    ui/canvas.cpp
    ui/corrupt_tab.cpp
    ui/explore_ui.cpp
    ui/overlay_panel.cpp
    ui/settings_panel.cpp
    ui/keybindings.cpp
)
target_link_libraries(glic-studio PRIVATE studio_core glic_cli_spec glic_preset imgui SDL3::SDL3 OpenGL::GL)
target_compile_definitions(glic-studio PRIVATE GLIC_STUDIO_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_compile_options(glic-studio PRIVATE -Wall -Wextra)
install(TARGETS glic-studio RUNTIME DESTINATION bin)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/presets/ DESTINATION share/glic-studio/presets FILES_MATCHING PATTERN "*.json")
