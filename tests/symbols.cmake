# Fails when the library exports a symbol outside the sl_ namespace, which could clash with the user's own.
# Usage: cmake -DNM=<nm> -DLIB=<library> -P symbols.cmake
execute_process(COMMAND ${NM} -g --defined-only ${LIB} OUTPUT_VARIABLE out RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${NM} failed on ${LIB}")
endif()
string(REPLACE "\n" ";" lines "${out}")
set(bad "")
foreach(line IN LISTS lines)
    # "<address> <type> <name>"; upper-case types are global (text, data, bss, read-only).
    if(line MATCHES "^[0-9a-fA-F]* [TDBRSC] _?([A-Za-z0-9_.$]+)$")
        set(name ${CMAKE_MATCH_1})   # the next MATCHES resets CMAKE_MATCH_1
        if(NOT name MATCHES "^sl_")
            list(APPEND bad ${name})
        endif()
    endif()
endforeach()
if(bad)
    list(JOIN bad " " names)
    message(FATAL_ERROR "symbols outside the sl_ prefix: ${names}")
endif()
