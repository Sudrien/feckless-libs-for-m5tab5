# SPDX-License-Identifier: MIT
#
# The members of an archive (picolibc's libc.a, libgcc.a) that define the
# names in NAMES, copied into an archive of their own, OUT:
#
#   cmake -DNM=<nm> -DAR=<ar> -DLIBC=<archive> -DNAMES=<names.txt>
#         -DWORK=<dir> -DOUT=<lib.a> -P cmake/archive_subset.cmake
#
# Not the archive whole: given every undefined name of a boxed app, libc.a
# would also supply malloc (and its locks), strdup, abort and the rest that
# must stay the host's, and those are not position-independent, nor is half
# of libgcc. Only these members are linked into the app; the build fails here
# if one of the names is not in the archive, and boxcheck.cmake fails the ELF
# if a member needed one that is not here.

file(STRINGS "${NAMES}" _want REGEX "^[A-Za-z_][A-Za-z0-9_]*$")
execute_process(COMMAND "${NM}" -A -g --defined-only "${LIBC}"
                OUTPUT_VARIABLE _nm RESULT_VARIABLE _r ERROR_QUIET)
if(NOT _r EQUAL 0)
    message(FATAL_ERROR "feckless_box_libc: ${NM} could not read ${LIBC}")
endif()
string(REPLACE "\n" ";" _lines "${_nm}")
set(_members "")
foreach(_s IN LISTS _want)
    set(_m "")
    foreach(_l IN LISTS _lines)
        # <libc.a>:<member.o>:00000000 T <name> (binutils prints no space
        # after the member's colon)
        if(_l MATCHES ":([^:/]+):[ ]*[0-9a-fA-F]+ [TDRBVW] ${_s}$")
            set(_m "${CMAKE_MATCH_1}")
            break()
        endif()
    endforeach()
    if(NOT _m)
        message(FATAL_ERROR "feckless_box_libc: ${_s} is not defined in ${LIBC}")
    endif()
    list(APPEND _members "${_m}")
endforeach()
list(REMOVE_DUPLICATES _members)

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
execute_process(COMMAND "${AR}" x "${LIBC}" ${_members}
                WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE _r)
if(NOT _r EQUAL 0)
    message(FATAL_ERROR "feckless_box_libc: ${AR} x failed on ${LIBC}")
endif()
file(REMOVE "${OUT}")
execute_process(COMMAND "${AR}" rcs "${OUT}" ${_members}
                WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE _r)
if(NOT _r EQUAL 0)
    message(FATAL_ERROR "feckless_box_libc: ${AR} rcs failed for ${OUT}")
endif()
list(LENGTH _members _n)
get_filename_component(_from "${LIBC}" NAME)
message(STATUS "feckless_box_libc: ${_n} members of ${_from}")
