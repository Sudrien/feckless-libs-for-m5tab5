# feckless-libs-for-m5tab5: fatfs. IDF's fatfs has a project_include.cmake
# (fatfs_create_partition_image() and the rest); standing in for it, this
# one includes it, so a project calling those still finds them.
# SPDX-License-Identifier: MIT

if(EXISTS "${IDF_PATH}/components/fatfs/project_include.cmake")
    include("${IDF_PATH}/components/fatfs/project_include.cmake")
endif()
