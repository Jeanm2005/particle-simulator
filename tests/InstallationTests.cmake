set(prefix "${BUILD_DIR}/installation-test/prefix")
set(relocated "${BUILD_DIR}/installation-test/relocated")
set(scratch "${BUILD_DIR}/installation-test/work")
file(REMOVE_RECURSE "${BUILD_DIR}/installation-test")
file(MAKE_DIRECTORY "${scratch}")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}"
    --prefix "${prefix}" --config "${CONFIG}" RESULT_VARIABLE result
    OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Install failed: ${output}${error}")
endif()
file(RENAME "${prefix}" "${relocated}")
set(binary "${relocated}/${BIN_DIR}/${BINARY_NAME}")
set(data "${relocated}/${DATA_DIR}/atom_sim/data/elements.json")
file(WRITE "${scratch}/input.txt" "H\n1s\np\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=QM_DATA_DIR "${binary}" --console
    WORKING_DIRECTORY "${scratch}" INPUT_FILE "${scratch}/input.txt"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 10)
string(FIND "${error}" "${data}" found)
if(NOT result EQUAL 0 OR found EQUAL -1 OR NOT error MATCHES "Loaded 119 elements")
    message(FATAL_ERROR "Relocated binary did not load installed JSON: ${output}${error}")
endif()
if(NOT EXISTS "${scratch}/H_1s_cloud.xyz")
    message(FATAL_ERROR "Installed console run did not export a cloud")
endif()


# Check lookup when argv[0] is a bare command name found through PATH.
if(NOT CMAKE_HOST_WIN32)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=QM_DATA_DIR
        "PATH=${relocated}/${BIN_DIR}:$ENV{PATH}" "${BINARY_NAME}" --console
        WORKING_DIRECTORY "${scratch}" INPUT_FILE "${scratch}/input.txt"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 10)
    string(FIND "${error}" "${data}" found)
    if(NOT result EQUAL 0 OR found EQUAL -1)
        message(FATAL_ERROR "PATH launch did not locate installed data: ${output}${error}")
    endif()
endif()

# An explicit override takes priority over all default locations.
file(MAKE_DIRECTORY "${scratch}/override")
file(COPY "${data}" DESTINATION "${scratch}/override")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
    "QM_DATA_DIR=${scratch}/override" "${binary}" --console
    WORKING_DIRECTORY "${scratch}" INPUT_FILE "${scratch}/input.txt"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 10)
string(FIND "${error}" "${scratch}/override/elements.json" found)
if(NOT result EQUAL 0 OR found EQUAL -1)
    message(FATAL_ERROR "Data directory override was ignored: ${output}${error}")
endif()

# A directory at the output path deterministically causes open failure, even as root.
file(REMOVE "${scratch}/H_1s_cloud.xyz")
file(MAKE_DIRECTORY "${scratch}/H_1s_cloud.xyz")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=QM_DATA_DIR "${binary}" --console
    WORKING_DIRECTORY "${scratch}" INPUT_FILE "${scratch}/input.txt"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 10)
if(NOT result EQUAL 1 OR NOT error MATCHES "Failed to open" OR output MATCHES "Wrote probability cloud")
    message(FATAL_ERROR "Export failure was not reported correctly: ${output}${error}")
endif()

# Linux /dev/full opens successfully but fails on write/close.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND EXISTS "/dev/full")
    file(REMOVE_RECURSE "${scratch}/H_1s_cloud.xyz")
    file(CREATE_LINK "/dev/full" "${scratch}/H_1s_cloud.xyz" SYMBOLIC RESULT link_result)
    if(NOT link_result STREQUAL "0")
        message(FATAL_ERROR "Unable to prepare write failure regression: ${link_result}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=QM_DATA_DIR "${binary}" --console
        WORKING_DIRECTORY "${scratch}" INPUT_FILE "${scratch}/input.txt"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 10)
    if(NOT result EQUAL 1 OR NOT error MATCHES "Failed to finish writing" OR output MATCHES "Wrote probability cloud")
        message(FATAL_ERROR "Write failure was not reported correctly: ${output}${error}")
    endif()
endif()
