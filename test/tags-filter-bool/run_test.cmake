# Run without a shell so the Boolean expression stays a single argument.
set(input "${source}/input.osm.opl")
if(case STREQUAL "invert-unscoped-ways")
    set(input "${source}/input-no-relations.osm.opl")
elseif(case STREQUAL "crlf")
    file(READ "${source}/expressions.txt" expressions)
    string(REPLACE "\n" "\r\n" expressions "${expressions}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/expressions-crlf.txt" "${expressions}")
    list(APPEND flags -e "${CMAKE_CURRENT_BINARY_DIR}/expressions-crlf.txt")
elseif(case STREQUAL "empty-file")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/expressions-empty.txt" "# No rules\n \t\r\n")
    list(APPEND flags -e "${CMAKE_CURRENT_BINARY_DIR}/expressions-empty.txt")
endif()

if(case STREQUAL "stdin")
    execute_process(COMMAND "${osmium}" tags-filter-bool ${flags} -F opl - "${expression}" -f opl
        INPUT_FILE "${input}" OUTPUT_VARIABLE output ERROR_VARIABLE error RESULT_VARIABLE result)
else()
    execute_process(COMMAND "${osmium}" tags-filter-bool ${flags} "${input}" ${expression} -f opl
        OUTPUT_VARIABLE output ERROR_VARIABLE error RESULT_VARIABLE result)
endif()
if(NOT result EQUAL 0 OR NOT error STREQUAL "")
    message(FATAL_ERROR "Command failed (${result}): ${error}")
endif()

string(REPLACE "\n" ";" lines "${output}")
set(actual)
foreach(line IN LISTS lines)
    if(line MATCHES "^([nwr]-?[0-9]+) ")
        list(APPEND actual "${CMAKE_MATCH_1}")
    endif()
endforeach()
if(NOT "${actual}" STREQUAL "${expected}")
    message(FATAL_ERROR "Expected ${expected}, got ${actual}:\n${output}")
endif()

if(case STREQUAL "compound-t")
    if(NOT output MATCHES "n1 [^\n]* Tkey1=value1,key2=value2,key3=yes x" OR
       NOT output MATCHES "n4 [^\n]* T x" OR
       NOT output MATCHES "w11 [^\n]* T N")
        message(FATAL_ERROR "Matching objects must keep tags; referenced-only objects must lose them:\n${output}")
    endif()
elseif(case STREQUAL "nested-t")
    if(NOT output MATCHES "r24 [^\n]* Ttype=site,group=yes,cycle=yes M" OR
       NOT output MATCHES "r20 [^\n]* T M" OR
       NOT output MATCHES "r25 [^\n]* T M")
        message(FATAL_ERROR "Reference cycles must not strip matching tags or retain referenced-only tags:\n${output}")
    endif()
elseif(case STREQUAL "invert-t")
    if(NOT output MATCHES "n1 [^\n]* T x" OR
       NOT output MATCHES "n3 [^\n]* Tkey1=value1,key2=value2,key3= x" OR
       NOT output MATCHES "r20 [^\n]* T M")
        message(FATAL_ERROR "Inverted matching and referenced-only tag removal disagree:\n${output}")
    endif()
endif()

# Check all references for complete outputs, not just the selected object IDs.
list(FIND flags "-R" omit_referenced)
if(omit_referenced EQUAL -1)
    set(output_file "${CMAKE_CURRENT_BINARY_DIR}/${case}-refs.osm.opl")
    file(WRITE "${output_file}" "${output}")
    execute_process(COMMAND "${osmium}" check-refs -r "${output_file}"
        OUTPUT_VARIABLE check_output ERROR_VARIABLE check_error RESULT_VARIABLE check_result)
    if(NOT check_result EQUAL 0)
        message(FATAL_ERROR "Incomplete references: ${check_output}${check_error}")
    endif()
endif()
