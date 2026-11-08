# SPDX-License-Identifier: MIT
#
# feckless_abi_box_elf(<name>): an app's ELF for a box (u-feckless-0005). In
# place of feckless_abi_elf(), after project():
#
#   project(myapp)
#   feckless_abi_box_elf(myapp)
#
# It is feckless_abi_elf() -- the same link, the same ABI check -- with the
# pure part of libc linked into the ELF (inapp.txt: this component's strto*,
# errno and printf, and the members of picolibc's libc.a and of libgcc.a that
# link into a shared object), and a second check (cmake/boxcheck.cmake). main must
# REQUIRE feckless_box_libc and call feckless_abi_require_box().
#
# project_elf() links with its own flags, not the project's, so a bare -lc
# would be the toolchain's default multilib (and newlib's): picolibc's libc.a
# is found here from the project's -march/-mabi as picolibc.specs finds it.

set(FECKLESS_BOX_LIBC_DIR "${CMAKE_CURRENT_LIST_DIR}")

macro(feckless_abi_box_elf _name)
    separate_arguments(_fb_flags UNIX_COMMAND "${CMAKE_C_FLAGS}")
    execute_process(COMMAND ${CMAKE_C_COMPILER} ${_fb_flags} -print-sysroot
                    OUTPUT_VARIABLE _fb_sysroot OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${CMAKE_C_COMPILER} ${_fb_flags} -print-multi-directory
                    OUTPUT_VARIABLE _fb_multi OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(_fb_libc "${_fb_sysroot}/../picolibc/riscv32-esp-elf/lib/${_fb_multi}/libc.a")
    if(NOT EXISTS "${_fb_libc}")
        message(FATAL_ERROR "feckless_box_libc: no picolibc libc.a at ${_fb_libc} "
                "(sysroot ${_fb_sysroot}, multilib ${_fb_multi})")
    endif()
    execute_process(COMMAND ${CMAKE_C_COMPILER} ${_fb_flags} -print-libgcc-file-name
                    OUTPUT_VARIABLE _fb_libgcc OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT EXISTS "${_fb_libgcc}")
        message(FATAL_ERROR "feckless_box_libc: no libgcc.a at ${_fb_libgcc}")
    endif()
    string(REPLACE "-gcc" "-nm" _fb_nm "${CMAKE_C_COMPILER}")
    string(REPLACE "-gcc" "-ar" _fb_ar "${CMAKE_C_COMPILER}")
    string(REPLACE "-gcc" "-readelf" _fb_readelf "${CMAKE_C_COMPILER}")
    set(_fb_subsets "")
    foreach(_fb_part libc libgcc)
        if(_fb_part STREQUAL "libc")
            set(_fb_from "${_fb_libc}")
            set(_fb_names "${FECKLESS_BOX_LIBC_DIR}/picolibc_names.txt")
        else()
            set(_fb_from "${_fb_libgcc}")
            set(_fb_names "${FECKLESS_BOX_LIBC_DIR}/libgcc_names.txt")
        endif()
        set(_fb_out "${CMAKE_BINARY_DIR}/feckless_box_${_fb_part}.a")
        add_custom_command(OUTPUT "${_fb_out}"
            COMMAND ${CMAKE_COMMAND} -DNM=${_fb_nm} -DAR=${_fb_ar} -DLIBC=${_fb_from}
                    -DNAMES=${_fb_names}
                    -DWORK=${CMAKE_BINARY_DIR}/feckless_box_${_fb_part}
                    -DOUT=${_fb_out}
                    -P ${FECKLESS_BOX_LIBC_DIR}/cmake/archive_subset.cmake
            DEPENDS "${_fb_names}" "${_fb_from}"
            COMMENT "Copying the position-independent part of ${_fb_part}"
            VERBATIM)
        list(APPEND _fb_subsets "${_fb_out}")
    endforeach()
    add_custom_target(feckless_box_subsets DEPENDS ${_fb_subsets})

    # The component for the build dependency; again, with the subset, in one
    # group, so that a name either needs from the other resolves either way.
    list(APPEND ELF_COMPONENTS feckless_box_libc)
    list(APPEND ELF_LIBS "-Wl,--start-group"
                         "esp-idf/feckless_box_libc/libfeckless_box_libc.a"
                         ${_fb_subsets}
                         "-Wl,--end-group")
    feckless_abi_elf(${_name})
    add_dependencies(elf feckless_box_subsets)
    add_custom_command(TARGET elf POST_BUILD
        COMMAND ${CMAKE_COMMAND}
                -DREADELF=${_fb_readelf}
                -DELF=${CMAKE_BINARY_DIR}/${_name}.app.elf
                -DINAPP=${FECKLESS_BOX_LIBC_DIR}/inapp.txt
                -P ${FECKLESS_BOX_LIBC_DIR}/cmake/boxcheck.cmake
        COMMENT "Checking ${_name}.app.elf for a box"
        VERBATIM)
endmacro()
