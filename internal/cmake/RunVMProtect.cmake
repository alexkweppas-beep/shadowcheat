if(NOT CONFIG STREQUAL "Release" AND NOT CONFIG STREQUAL "RelWithDebInfo")
    return()
endif()

foreach(_required INPUT OUTPUT VMP_CONSOLE VMP_PROJECT)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "RunVMProtect: ${_required} is not set")
    endif()
endforeach()

if(NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "RunVMProtect: input does not exist: ${INPUT}")
endif()
if(NOT EXISTS "${VMP_CONSOLE}")
    message(FATAL_ERROR "RunVMProtect: console does not exist: ${VMP_CONSOLE}")
endif()
if(NOT EXISTS "${VMP_PROJECT}")
    message(FATAL_ERROR "RunVMProtect: project does not exist: ${VMP_PROJECT}")
endif()

get_filename_component(_directory "${INPUT}" DIRECTORY)
get_filename_component(_raw_name "${INPUT}" NAME)
get_filename_component(_output_stem "${OUTPUT}" NAME_WE)
get_filename_component(_output_extension "${OUTPUT}" EXT)
set(_protected
    "${_directory}/${_output_stem}.vmp.tmp${_output_extension}")
get_filename_component(_protected_name "${_protected}" NAME)
set(_project_local "${_directory}/shadow_internal.build.vmp")

if(INPUT STREQUAL OUTPUT)
    message(FATAL_ERROR
        "RunVMProtect: raw input and shipping output must be different")
endif()

# Remove a stale protected artifact before starting. If VMProtect fails, only
# the explicitly named *.raw.dll remains and the build itself fails.
file(REMOVE "${OUTPUT}" "${_protected}" "${_project_local}")
file(COPY_FILE "${VMP_PROJECT}" "${_project_local}")

set(_command
    "${VMP_CONSOLE}"
    "${_raw_name}"
    "${_protected_name}"
    "-pf"
    "shadow_internal.build.vmp")
if(DEFINED VMP_WATERMARK AND NOT VMP_WATERMARK STREQUAL "")
    list(APPEND _command "-wm" "${VMP_WATERMARK}")
endif()
list(APPEND _command "-we")

execute_process(
    COMMAND ${_command}
    WORKING_DIRECTORY "${_directory}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr)

if(NOT _result EQUAL 0 OR NOT EXISTS "${_protected}")
    file(REMOVE "${_protected}" "${_project_local}" "${OUTPUT}")
    message(FATAL_ERROR
        "VMProtect failed (${_result}).\n${_stdout}\n${_stderr}")
endif()

file(STRINGS "${_protected}" _plaintext_leaks
    REGEX
        "QUANT TOOLS v1\\.0|global\\.__uaLoaded|const __ffBuckets|__bfMvFn=function|__bfCwFn=function|__bfPlFn=function|__bfAcStamp=function|__bfDmg1|__bfAutoLoot|const rows=\\[\\],seen=\\{\\};const adminKey|VMProtectSDK64\\.dll"
    LIMIT_COUNT 1)
if(_plaintext_leaks)
    file(REMOVE "${_protected}" "${_project_local}" "${OUTPUT}")
    message(FATAL_ERROR
        "VMProtect output still exposes a protected plaintext marker: "
        "${_plaintext_leaks}")
endif()

file(RENAME "${_protected}" "${OUTPUT}")
file(REMOVE "${_project_local}")

if(NOT KEEP_RAW)
    file(REMOVE "${INPUT}")
endif()

message(STATUS "VMProtect produced ${OUTPUT}")
