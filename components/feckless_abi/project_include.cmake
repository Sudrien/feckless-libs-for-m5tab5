# SPDX-License-Identifier: MIT
#
# feckless_abi_elf(<name>): an app's ELF for a feckless launcher. Call it
# after project(), in place of elf_loader's project_elf():
#
#   project(myapp)
#   feckless_abi_elf(myapp)
#
# It links main (and ELF_COMPONENTS, statically) into <name>.app.elf with
# project_elf(), then checks the ELF's undefined symbols against
# exports.txt: any outside the ABI, or no reference to this version's
# feckless_abi_vN (feckless_abi_require()), fails the build.

set(FECKLESS_ABI_DIR "${CMAKE_CURRENT_LIST_DIR}")

macro(feckless_abi_elf _name)
    include(elf_loader)
    # -Bsymbolic: the ELF's references to what it defines itself bind
    # inside it. Without it a global it defines -- esp-dsp's assembly
    # entry points, default visibility -- is reached through the PLT by
    # name, and elf_loader 2.0.0 looks every named relocation up in the
    # launcher's table only: "Can't find common dsps_fft2r_fc32_arp4_".
    # project_elf() links with ${ELF_LIBS} on the command line, which is
    # the one way in.
    list(APPEND ELF_LIBS "-Wl,-Bsymbolic")
    # The app's own esp_app_desc_t, linked in: with -Bsymbolic its
    # esp_app_get_description() returns the app's version, name and build
    # time. Taken from the launcher, as it was, it described the launcher.
    # The launcher still exports it, for apps built before this.
    list(APPEND ELF_COMPONENTS esp_app_format)
    project_elf(${_name})
    string(REPLACE "-gcc" "-readelf" _fa_readelf "${CMAKE_C_COMPILER}")
    add_custom_command(TARGET elf POST_BUILD
        COMMAND ${CMAKE_COMMAND}
                -DREADELF=${_fa_readelf}
                -DELF=${CMAKE_BINARY_DIR}/${_name}.app.elf
                -DEXPORTS=${FECKLESS_ABI_DIR}/exports.txt
                -DHEADER=${FECKLESS_ABI_DIR}/include/feckless_abi.h
                -P ${FECKLESS_ABI_DIR}/cmake/abicheck.cmake
        COMMENT "Checking ${_name}.app.elf against the feckless ABI"
        VERBATIM)
endmacro()
