add_library(glic_cli_spec STATIC spec.cpp)
target_include_directories(glic_cli_spec PUBLIC .)
target_link_libraries(glic_cli_spec PUBLIC glic::glic)

add_executable(glic-cli main.cpp)
target_link_libraries(glic-cli PRIVATE glic_cli_spec glic::glic)
set_target_properties(glic-cli PROPERTIES OUTPUT_NAME glic)
install(TARGETS glic-cli RUNTIME DESTINATION bin)
