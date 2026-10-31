# feckless-libs-for-m5tab5: feckless_abi_storage
#
# The feckless ABI's storage section (3008). A launcher that links this
# gives its apps the storage handler, FatFs and libc's file I/O: the
# table esp_elf resolves them against, generated from feckless_abi's
# sections/storage.txt, and feckless_abi_storage_v2, which every such app
# refers to. An app names this component too, for the headers; its ELF
# links none of it.
#
# WHOLE_ARCHIVE: nothing in a launcher refers to the table but
# feckless_abi_run()'s weak reference, which pulls nothing out of an
# archive by itself.
#
# SPDX-License-Identifier: MIT

set(_table "${CMAKE_CURRENT_BINARY_DIR}/feckless_abi_storage_syms.c")

idf_component_register(
    SRCS "src/feckless_abi_storage.c" "${_table}"
    INCLUDE_DIRS "include"
    REQUIRES feckless_abi feckless_storage_handler fatfs
    PRIV_REQUIRES elf_loader
    WHOLE_ARCHIVE
)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_get_property(_abi_dir feckless_abi COMPONENT_DIR)

    # One tag, as feckless_abi checks for its own siblings.
    foreach(_dir "${CMAKE_CURRENT_LIST_DIR}" "${_abi_dir}")
        file(STRINGS "${_dir}/idf_component.yml" _v REGEX "^version:")
        string(REGEX REPLACE "^version:[ \t]*\"?([^\" \t]*)\"?.*" "\\1" _v "${_v}")
        list(APPEND _versions "${_v}")
    endforeach()
    list(GET _versions 0 _own)
    list(GET _versions 1 _abiv)
    if(NOT _own STREQUAL _abiv)
        message(FATAL_ERROR "feckless_abi_storage is ${_own}, feckless_abi is ${_abiv}: "
                "name every feckless component at the same tag")
    endif()

    # The FatFs an app's FIL, FF_DIR and FILINFO are laid out for, and
    # TCHAR is typed for: these must be the same on both sides, so both
    # are held to them. Each is a field or a type in ff.h.
    idf_component_get_property(_fatfs_dir fatfs COMPONENT_DIR)
    set(_fatfs_url "")
    if(EXISTS "${_fatfs_dir}/idf_component.yml")
        file(STRINGS "${_fatfs_dir}/idf_component.yml" _fatfs_url REGEX "^url:")
    endif()
    set(_bad "")
    if(NOT _fatfs_url MATCHES "feckless-libs-for-m5tab5")
        list(APPEND _bad "fatfs is ${_fatfs_dir}, not feckless-libs' (components/fatfs)")
    endif()
    if(NOT CONFIG_FECKLESS_FATFS_EXFAT)
        list(APPEND _bad "CONFIG_FECKLESS_FATFS_EXFAT=y (FF_FS_EXFAT, FF_LBA64)")
    endif()
    if(NOT (CONFIG_FATFS_LFN_HEAP OR CONFIG_FATFS_LFN_STACK))
        list(APPEND _bad "CONFIG_FATFS_LFN_HEAP=y or _STACK=y (FILINFO's fname)")
    endif()
    if(NOT CONFIG_FATFS_API_ENCODING_UTF_8)
        list(APPEND _bad "CONFIG_FATFS_API_ENCODING_UTF_8=y (TCHAR is char)")
    endif()
    if(NOT CONFIG_WL_SECTOR_SIZE EQUAL 4096)
        list(APPEND _bad "CONFIG_WL_SECTOR_SIZE=4096 (FF_MAX_SS, is ${CONFIG_WL_SECTOR_SIZE})")
    endif()
    if(NOT CONFIG_FATFS_PER_FILE_CACHE)
        list(APPEND _bad "CONFIG_FATFS_PER_FILE_CACHE=y (FF_FS_TINY 0: FIL's buf)")
    endif()
    if(NOT CONFIG_FATFS_USE_DYN_BUFFERS)
        list(APPEND _bad "CONFIG_FATFS_USE_DYN_BUFFERS=y (FIL's buf a pointer)")
    endif()
    # 5003: on, since storage_v2. Errant's and Spurious's readers build a
    # cluster map for their large files.
    if(NOT CONFIG_FATFS_USE_FASTSEEK)
        list(APPEND _bad "CONFIG_FATFS_USE_FASTSEEK=y (FIL's cltbl, storage_v2)")
    endif()
    if(NOT CONFIG_FATFS_FS_LOCK EQUAL 0)
        list(APPEND _bad "CONFIG_FATFS_FS_LOCK=0 (FIL's and FF_DIR's lockid, is ${CONFIG_FATFS_FS_LOCK})")
    endif()
    if(_bad)
        string(REPLACE ";" "\n  " _bad "${_bad}")
        message(FATAL_ERROR "feckless_abi_storage: the storage section's FatFs needs\n  ${_bad}")
    endif()

    include("${_abi_dir}/cmake/abitable.cmake")
    feckless_abi_write_table("${_table}" "${_abi_dir}/sections/storage.txt"
                             g_feckless_abi_storage_elfsyms
                             "\"feckless_abi_storage_v2\"" feckless_abi_storage_v2)
endif()
