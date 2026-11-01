# SPDX-License-Identifier: MIT
#
# 8012: libversions.h, from the records the build itself uses, so the
# screen cannot drift from what was compiled. Avoidant's 0028 (itself
# Lothesome's, Mahjong's and Defeatist's), made one copy:
#
#   the app                    ROOT's git commit, LV_NAME, LV_LICENSE
#   dependencies.lock          every component the component manager
#                              resolved, at its version (git ones by
#                              commit); its licence from the licence file
#                              in the copy compiled
#   <ROOT>/components/*/       vendored ones: version from their
#                              idf_component.yml, licence from their file
#   feckless_graphics_handler  the fonts compiled in with it: Ark Pixel
#                              (its commit, from gen_ark12.py) and
#                              Galmuri, OFL-1.1; Arabixel Basic, CC BY 4.0
#   feckless_image_handler     the decoders it fetches (8013): pngle,
#                              stb_image, J40, at its cmake/vendored.cmake
#                              pins -- Defeatist's 8069 rows
#   LV_EXTRA                   the app's own rows, last
#
# A component's directory is the build's own when it is one of the
# build's components (so a build with the component manager off, or an
# ELF build of fewer components, still finds it), else managed_components/
# beside the lock.
#
# In: ROOT (default: this file's project, two up), LV_NAME, LV_LICENSE,
# LV_NO_IDF, LV_EXTRA ("module|checkout|licence" each).
# Out: LIBVERSIONS_DIR, holding libversions.h and libversions.tsv, each
# rewritten only when its text changes; LIBVERSIONS_TSV, the latter.

if(NOT ROOT)
    set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
endif()
get_filename_component(ROOT "${ROOT}" ABSOLUTE)
if(NOT LV_NAME)
    get_filename_component(LV_NAME "${ROOT}" NAME)
endif()
if(NOT LV_LICENSE)
    set(LV_LICENSE "MIT")
endif()

set(_lock "")
set(_built "")
if(COMMAND idf_build_get_property)
    idf_build_get_property(_lock DEPENDENCIES_LOCK)
    idf_build_get_property(_built BUILD_COMPONENTS)
endif()
if(NOT _lock)
    set(_lock "${ROOT}/dependencies.lock")
endif()
get_filename_component(_lockdir "${_lock}" DIRECTORY)
set(_libs "")   # "module|checkout|licence", one each

macro(_lv_depends f)
    if(NOT CMAKE_SCRIPT_MODE_FILE)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${f}")
    endif()
endmacro()

# A 40-hex git commit as its first seven; anything else as written.
function(_lv_short out ver)
    string(LENGTH "${ver}" _len)
    if(_len EQUAL 40 AND ver MATCHES "^[0-9a-f]+$")
        string(SUBSTRING "${ver}" 0 7 ver)
    endif()
    set(${out} "${ver}" PARENT_SCOPE)
endfunction()

# Where a component's files are: the build's copy, else managed_components/.
function(_lv_dir out name)
    set(_d "")
    string(REGEX REPLACE "^.*/" "" _bare "${name}")
    # list(FIND), not IN_LIST: script mode has no policy CMP0057.
    list(FIND _built "${name}" _i)
    list(FIND _built "${_bare}" _j)
    if(NOT _i EQUAL -1)
        idf_component_get_property(_d "${name}" COMPONENT_DIR)
    elseif(NOT _j EQUAL -1)
        idf_component_get_property(_d "${_bare}" COMPONENT_DIR)
    endif()
    if(NOT _d)
        string(REPLACE "/" "__" _m "${name}")
        set(_d "${_lockdir}/managed_components/${_m}")
    endif()
    set(${out} "${_d}" PARENT_SCOPE)
endfunction()

# A component's licence, from the licence file in its directory, else
# its idf_component.yml's license field.
function(_lv_license out dir)
    set(_lic "")
    file(GLOB _files LIST_DIRECTORIES false
         "${dir}/license.txt" "${dir}/LICENSE" "${dir}/LICENSE.*"
         "${dir}/LICENCE" "${dir}/LICENSE-*")
    foreach(_f IN LISTS _files)
        file(STRINGS "${_f}" _head LIMIT_COUNT 15)
        string(JOIN "\n" _head ${_head})
        if(_head MATCHES "(Valid|SPDX)-License-Identifier: *([A-Za-z0-9.+-]+)")
            set(_lic "${CMAKE_MATCH_2}")
        elseif(_head MATCHES "Espressif Modified MIT")
            set(_lic "Espressif MIT")
        elseif(_head MATCHES "Apache License" AND _head MATCHES "Version 2\\.0")
            set(_lic "Apache-2.0")
        elseif(_head MATCHES "MIT License")
            set(_lic "MIT")
        elseif(_head MATCHES "Open Font License")
            set(_lic "OFL-1.1")
        elseif(_head MATCHES "CC0 1\\.0")
            set(_lic "CC0-1.0")
        endif()
        if(_lic)
            break()
        endif()
    endforeach()
    if(NOT _lic AND EXISTS "${dir}/idf_component.yml")
        file(STRINGS "${dir}/idf_component.yml" _f REGEX "^license:")
        if(_f MATCHES "^license: *[\"']?([^\"']+)")
            set(_lic "${CMAKE_MATCH_1}")
        endif()
    endif()
    if(NOT _lic)
        set(_lic "licence not found")
    endif()
    set(${out} "${_lic}" PARENT_SCOPE)
endfunction()

# The app, at the commit it was built from.
set(_self "unknown")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD WORKING_DIRECTORY "${ROOT}"
                    OUTPUT_VARIABLE _self OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0 OR NOT _self)
        set(_self "unknown")
    endif()
endif()
_lv_short(_self "${_self}")
list(APPEND _libs "${LV_NAME}|${_self}|${LV_LICENSE}")

# The lock's components. Four spaces in: the component's own version,
# not a requirement.
set(_gfx "")
set(_img "")
if(EXISTS "${_lock}")
    _lv_depends("${_lock}")
    file(STRINGS "${_lock}" _lines)
    set(_name "")
    set(_in_deps FALSE)
    foreach(_l IN LISTS _lines)
        if(_l STREQUAL "dependencies:")
            set(_in_deps TRUE)
        elseif(_l MATCHES "^[a-z_]+:")
            set(_in_deps FALSE)
        elseif(_in_deps AND _l MATCHES "^  ([^ ][^:]*):$")
            set(_name "${CMAKE_MATCH_1}")
        elseif(_in_deps AND _name AND _l MATCHES "^    version: '?([^']*)'?$")
            set(_ver "${CMAKE_MATCH_1}")
            string(REGEX REPLACE "^espressif/" "" _short "${_name}")
            if(_short STREQUAL "idf" AND LV_NO_IDF)
                # left out
            elseif(_short STREQUAL "idf")
                set(_lic "Apache-2.0")
                if(DEFINED ENV{IDF_PATH})
                    _lv_license(_lic "$ENV{IDF_PATH}")
                endif()
                list(APPEND _libs "ESP-IDF|${_ver}|${_lic}")
            elseif(NOT _short STREQUAL "cmake_utilities")
                _lv_short(_ver "${_ver}")
                _lv_dir(_d "${_name}")
                _lv_license(_lic "${_d}")
                # esp_jpeg's own code is Apache-2.0; the TJpgDec it wraps
                # is ChaN's, whose notice travels with the binary
                # (Defeatist's).
                if(_short STREQUAL "esp_jpeg")
                    string(APPEND _lic " + TJpgDec (ChaN)")
                endif()
                list(APPEND _libs "${_short}|${_ver}|${_lic}")
                if(_short STREQUAL "feckless_graphics_handler")
                    set(_gfx "${_d}")
                elseif(_short STREQUAL "feckless_image_handler")
                    set(_img "${_d}")
                endif()
            endif()
            set(_name "")
        endif()
    endforeach()
else()
    list(APPEND _libs "dependencies.lock|missing|")
endif()

# Vendored components: <ROOT>/components/<name>/ with an idf_component.yml.
file(GLOB _vendored LIST_DIRECTORIES true "${ROOT}/components/*")
foreach(_d IN LISTS _vendored)
    if(IS_DIRECTORY "${_d}" AND EXISTS "${_d}/idf_component.yml")
        _lv_depends("${_d}/idf_component.yml")
        get_filename_component(_n "${_d}" NAME)
        file(STRINGS "${_d}/idf_component.yml" _v REGEX "^version:")
        string(REGEX REPLACE "^version: *[\"']?([^\"']*)[\"']?.*$" "\\1" _v "${_v}")
        if(NOT _v)
            set(_v "vendored")
        else()
            set(_v "${_v} (vendored)")
        endif()
        _lv_license(_lic "${_d}")
        list(APPEND _libs "${_n}|${_v}|${_lic}")
    endif()
endforeach()

# The fonts feckless_graphics_handler compiles in: their licences travel
# with the firmware. Not listed when the build has no graphics handler.
list(FIND _built "feckless_graphics_handler" _i)
if(NOT _gfx AND NOT _i EQUAL -1)
    idf_component_get_property(_gfx feckless_graphics_handler COMPONENT_DIR)
endif()
if(_gfx AND EXISTS "${_gfx}/fonts")
    set(_v "-")
    if(EXISTS "${_gfx}/tools/gen_ark12.py")
        _lv_depends("${_gfx}/tools/gen_ark12.py")
        file(STRINGS "${_gfx}/tools/gen_ark12.py" _v REGEX "^ARK_COMMIT *=")
        string(REGEX REPLACE "^ARK_COMMIT *= *\"([0-9a-f]+)\".*$" "\\1" _v "${_v}")
        _lv_short(_v "${_v}")
    endif()
    list(APPEND _libs "Ark Pixel font (TakWolf)|${_v}|OFL-1.1")
    if(EXISTS "${_gfx}/fonts/ark12/LICENSE-OFL-galmuri")
        list(APPEND _libs "Galmuri font (Minseo Lee)|-|OFL-1.1")
    endif()
    if(EXISTS "${_gfx}/fonts/arabixel")
        list(APPEND _libs "Arabixel Basic font|-|CC-BY-4.0")
    endif()
endif()

# The decoders feckless_image_handler fetches at configure time, at its
# pins, when the build has it.
list(FIND _built "feckless_image_handler" _i)
if(NOT _img AND NOT _i EQUAL -1)
    idf_component_get_property(_img feckless_image_handler COMPONENT_DIR)
endif()
if(_img AND EXISTS "${_img}/cmake/vendored.cmake")
    _lv_depends("${_img}/cmake/vendored.cmake")
    file(READ "${_img}/cmake/vendored.cmake" _t)
    foreach(_pair "pngle|IMG_PNGLE_COMMIT|MIT"
                  "stb_image|IMG_STB_COMMIT|MIT or Unlicense"
                  "J40|IMG_J40_COMMIT|public domain or MIT-0")
        string(REPLACE "|" ";" _pl "${_pair}")
        list(GET _pl 0 _pn)
        list(GET _pl 1 _var)
        list(GET _pl 2 _lic)
        if(_t MATCHES "set\\(${_var} +([0-9a-f]+)\\)")
            _lv_short(_v "${CMAKE_MATCH_1}")
            list(APPEND _libs "${_pn}|${_v}|${_lic}")
        endif()
    endforeach()
endif()

# The app's own, as given. A row with no checkout or licence keeps its
# columns.
foreach(_x IN LISTS LV_EXTRA)
    string(REGEX MATCHALL "\\|" _bars "${_x}")
    list(LENGTH _bars _nb)
    if(_nb EQUAL 1)
        string(APPEND _x "|")
    elseif(_nb EQUAL 0)
        string(APPEND _x "||")
    endif()
    list(APPEND _libs "${_x}")
endforeach()

list(LENGTH _libs _n)
set(_h "/* Generated by feckless_libversions' cmake/libversions.cmake. Do not edit. */\n")
string(APPEND _h "#pragma once\n")
string(APPEND _h "#ifndef FECKLESS_LIBV_T\n#define FECKLESS_LIBV_T\n")
string(APPEND _h "typedef struct { const char *module, *checkout, *licence; } libv_t;\n")
string(APPEND _h "#endif\n")
string(APPEND _h "#define LIBVERSIONS_COUNT (${_n})\n")
string(APPEND _h "static const libv_t k_libversions[LIBVERSIONS_COUNT] = {\n")
set(_tsv "module\tcheckout\tlicence\n")
foreach(_l IN LISTS _libs)
    string(REPLACE "|" "\t" _t "${_l}")
    string(APPEND _tsv "${_t}\n")
    string(REPLACE "\\" "\\\\" _l "${_l}")
    string(REPLACE "\"" "\\\"" _l "${_l}")
    string(REPLACE "|" "\", \"" _l "${_l}")
    string(APPEND _h "    { \"${_l}\" },\n")
endforeach()
string(APPEND _h "};\n")

if(CMAKE_BINARY_DIR)
    set(LIBVERSIONS_DIR "${CMAKE_BINARY_DIR}/libversions")
else()
    set(LIBVERSIONS_DIR "${CMAKE_CURRENT_BINARY_DIR}/libversions")
endif()
file(WRITE "${LIBVERSIONS_DIR}/libversions.h.tmp" "${_h}")
configure_file("${LIBVERSIONS_DIR}/libversions.h.tmp"
               "${LIBVERSIONS_DIR}/libversions.h" COPYONLY)
file(WRITE "${LIBVERSIONS_DIR}/libversions.tsv.tmp" "${_tsv}")
configure_file("${LIBVERSIONS_DIR}/libversions.tsv.tmp"
               "${LIBVERSIONS_DIR}/libversions.tsv" COPYONLY)
set(LIBVERSIONS_TSV "${LIBVERSIONS_DIR}/libversions.tsv")
message(STATUS "feckless_libversions: ${_n} entries")
