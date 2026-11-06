set(input "${source}/tags-filter-bool/input.osm.opl")
set(output "${CMAKE_CURRENT_BINARY_DIR}/invalid-output.osm.pbf")
foreach(expression IN ITEMS "" "a AND" "a OR OR b" "(a" "a)" "name='unfinished" "x/a")
    execute_process(COMMAND "${osmium}" tags-filter-bool "${input}" "${expression}" -o "${output}"
        RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT result EQUAL 2 OR NOT stderr MATCHES "Boolean expression, column [0-9]+:")
        message(FATAL_ERROR "Expected argument error for '${expression}', got ${result}: ${stderr}")
    endif()
    if(EXISTS "${output}")
        message(FATAL_ERROR "Invalid syntax created an output file")
    endif()
endforeach()

execute_process(COMMAND "${osmium}" tags-filter-bool "${input}" -e "${source}/tags-filter-bool/invalid-expressions.txt" -o "${output}"
    RESULT_VARIABLE result ERROR_VARIABLE stderr)
if(NOT result EQUAL 2 OR NOT stderr MATCHES "invalid-expressions.txt:3: Boolean expression, column 6:")
    message(FATAL_ERROR "Missing filename/line/column: ${stderr}")
endif()

execute_process(COMMAND "${osmium}" tags-filter-bool -F opl - "a" -f opl
    INPUT_FILE "${input}" RESULT_VARIABLE result ERROR_VARIABLE stderr)
if(NOT result EQUAL 2 OR NOT stderr MATCHES "STDIN")
    message(FATAL_ERROR "STDIN without -R must fail: ${stderr}")
endif()

execute_process(COMMAND "${osmium}" tags-filter-bool "${input}" -e "${source}/tags-filter-bool/missing.txt" -f opl
    RESULT_VARIABLE result ERROR_VARIABLE stderr)
if(NOT result EQUAL 2 OR NOT stderr MATCHES "Could not open file")
    message(FATAL_ERROR "Missing expression file must fail: ${stderr}")
endif()
