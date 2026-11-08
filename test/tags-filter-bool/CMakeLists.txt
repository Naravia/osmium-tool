#-----------------------------------------------------------------------------
#
#  Osmium Tool Tests - tags-filter-bool
#
#-----------------------------------------------------------------------------

set(COMPOUND "key1=value1 AND key2=value2 AND (key3!= OR key4!=)")

function(check_tags_filter_bool _name _flags _expression _expected)
    add_test(NAME tags-filter-bool-${_name}
        COMMAND ${CMAKE_COMMAND}
        "-Dosmium=$<TARGET_FILE:osmium>"
        "-Dsource=${PROJECT_SOURCE_DIR}/test/tags-filter-bool"
        "-Dcase=${_name}"
        "-Dflags=${_flags}"
        "-Dexpression=${_expression}"
        "-Dexpected=${_expected}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_test.cmake)
endfunction()

check_tags_filter_bool(compound-R "-R" "${COMPOUND}" "n1;n2;w10;w12;r20;r22")
check_tags_filter_bool(compound "" "${COMPOUND}" "n1;n2;n3;n4;n5;n6;w10;w11;w12;r20;r22")
check_tags_filter_bool(compound-t "-t" "${COMPOUND}" "n1;n2;n3;n4;n5;n6;w10;w11;w12;r20;r22")
check_tags_filter_bool(nodes "-R" "n/(${COMPOUND})" "n1;n2")
check_tags_filter_bool(ways "" "w/(${COMPOUND})" "n1;n2;n3;n4;n5;n6;w10;w12")
check_tags_filter_bool(missing "-R" "n/NOT key1" "n6;n8;n9")
check_tags_filter_bool(area-R "-R" "a/(building AND NOT building=construction)" "w10;w14;r20;r21")
check_tags_filter_bool(area-and-way-scope "-R" "wa/building" "w10;w11;w13;w14;r20;r21")
check_tags_filter_bool(area "" "a/(building AND NOT building=construction)" "n1;n2;n3;n4;n5;n6;w10;w11;w14;r20;r21")
check_tags_filter_bool(nested "" "r/(group AND cycle=yes)" "n3;n4;n6;w11;r20;r23;r24;r25")
check_tags_filter_bool(nested-t "-t" "r/(group AND cycle=yes)" "n3;n4;n6;w11;r20;r23;r24;r25")
check_tags_filter_bool(nested-R "-R" "r/(group AND cycle=yes)" "r24")
check_tags_filter_bool(invert-R "-i;-R" "${COMPOUND}" "n3;n4;n5;n6;n7;n8;n9;w11;w13;w14;r21;r23;r24;r25")
check_tags_filter_bool(invert "-i" "${COMPOUND}" "n1;n3;n4;n5;n6;n7;n8;n9;w11;w13;w14;r20;r21;r23;r24;r25")
check_tags_filter_bool(invert-t "-i;-t" "${COMPOUND}" "n1;n3;n4;n5;n6;n7;n8;n9;w11;w13;w14;r20;r21;r23;r24;r25")
check_tags_filter_bool(file "-R;-e;${CMAKE_CURRENT_SOURCE_DIR}/expressions.txt" "" "n1;n2;n8;n9")
check_tags_filter_bool(file-and-cli "-R;-e;${CMAKE_CURRENT_SOURCE_DIR}/expressions.txt" "w/key4!=" "n1;n2;n8;n9;w12")
check_tags_filter_bool(crlf "-R" "" "n1;n2;n8;n9")
check_tags_filter_bool(empty-file "-R" "" "")
check_tags_filter_bool(stdin "-R" "${COMPOUND}" "n1;n2;w10;w12;r20;r22")
check_tags_filter_bool(alternatives "-R" "n/key3!=;w/key4!=" "n1;n4;w12")
check_tags_filter_bool(invert-node-scope "-i" "n/key1" "n1;n2;n3;n4;n5;n6;n8;n9;w10;w11;w12;w13;w14;r20;r21;r22;r23;r24;r25")
check_tags_filter_bool(invert-unscoped-ways "-i" "n/note" "n1;n2;w1")
check_tags_filter_bool(id-chunks "" "w/select=yes" "n8191;n8192;n4294967297;w8191")
check_tags_filter_bool(id-chunks-strip "-t" "w/select=yes" "n8191;n8192;n4294967297;w8191")

foreach(_script errors differential downward)
    add_test(NAME tags-filter-bool-${_script}
        COMMAND ${CMAKE_COMMAND}
        "-Dosmium=$<TARGET_FILE:osmium>"
        "-Dsource=${PROJECT_SOURCE_DIR}/test"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_${_script}.cmake)
endforeach()
