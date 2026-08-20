add_library(shadow_shared INTERFACE)

target_include_directories(shadow_shared INTERFACE include)

target_compile_definitions(shadow_shared
    INTERFACE $<$<BOOL:${SHADOW_DEV_BUILD}>:SHADOW_DEV>
)
