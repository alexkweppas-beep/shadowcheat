add_library(internal SHARED
    src/main.cpp
)

target_link_libraries(internal PRIVATE shadow_shared)

set_target_properties(internal PROPERTIES
    OUTPUT_NAME shadow_internal
    PREFIX ""
)
