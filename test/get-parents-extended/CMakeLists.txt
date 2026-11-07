function(check_parent_lookup _name _mode _flags _selection _expected)
    add_test(NAME get-parents-extended-${_name}
        COMMAND ${CMAKE_COMMAND}
        "-Dosmium=$<TARGET_FILE:osmium>"
        "-Dsource=${PROJECT_SOURCE_DIR}/test/tags-filter-bool"
        "-Dcase=${_name}" "-Dmode=${_mode}" "-Dflags=${_flags}"
        "-Dselection=${_selection}" "-Dexpected=${_expected}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_test.cmake)
endfunction()

check_parent_lookup(typed standalone "" "n1;w11;r24" "w10;w13;r20;r25")
check_parent_lookup(self standalone "-s" "n1;w11;r24" "n1;w10;w11;w13;r20;r24;r25")
check_parent_lookup(forward standalone "" "r25" "r24")
check_parent_lookup(one-hop standalone "" "r20" "r23")
check_parent_lookup(filtered standalone "--parents-expression=r/type=multipolygon" "n6" "r20")
check_parent_lookup(filtered-self standalone "-s;--parents-expression=r/type=multipolygon" "n6" "n6;r20")
check_parent_lookup(parent-area standalone "--parents-expression=a/building" "n1" "w10")
check_parent_lookup(parent-or standalone "--parents-expression=r/type=multipolygon;--parents-expression=w/key1" "n6" "w12;r20")
check_parent_lookup(seed-osm standalone "" "w11" "r20")
check_parent_lookup(seed-empty standalone "" "" "")
check_parent_lookup(edges standalone "" "n1;w11;r24" "w10;w13;r20;r25")

check_parent_lookup(integrated-R integrated "-R" "w/key1=value1 AND key3!=" "w10")
check_parent_lookup(integrated-way integrated "" "w/building" "n1;n2;n3;n4;n5;n6;w10;w11;w13;w14;r20;r21")
check_parent_lookup(integrated-node integrated "" "n/key3!=" "n1;n2;n3;n4;n5;n6;w10;w11;w12;w13;w14")
check_parent_lookup(integrated-node-strip integrated "-t" "n/key3!=" "n1;n2;n3;n4;n5;n6;w10;w11;w12;w13;w14")
check_parent_lookup(integrated-relation integrated "" "r/type=multipolygon" "n3;n4;n6;w11;r20;r23")
check_parent_lookup(integrated-relation-strip integrated "-t" "r/type=multipolygon" "n3;n4;n6;w11;r20;r23")
check_parent_lookup(integrated-forward-R integrated "-R" "r/type=site AND NOT group" "r24;r25")
check_parent_lookup(integrated-cycle integrated "" "r/type=site AND NOT group" "n3;n4;n6;w11;r20;r23;r24;r25")
check_parent_lookup(integrated-cycle-strip integrated "-t" "r/type=site AND NOT group" "n3;n4;n6;w11;r20;r23;r24;r25")
check_parent_lookup(integrated-parent-filter integrated "--parents-expression=r/type=multipolygon" "n/NOT key1" "n3;n4;n6;n8;n9;w11;r20")
check_parent_lookup(integrated-none integrated "" "none" "")
check_parent_lookup(integrated-invert integrated "-i;-R" "n/key1" "n6;n8;n9;w10;w11;w12;w13;w14;r20;r21;r22;r23;r24;r25")

add_test(NAME get-parents-extended-errors
    COMMAND ${CMAKE_COMMAND} "-Dosmium=$<TARGET_FILE:osmium>"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_errors.cmake)
