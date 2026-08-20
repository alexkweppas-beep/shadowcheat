add_executable(external
    src/main.cpp
)

target_link_libraries(external PRIVATE shadow_shared)

set_target_properties(external PROPERTIES
    OUTPUT_NAME shadow_external
)
