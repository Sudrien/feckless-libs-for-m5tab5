# SPDX-License-Identifier: MIT
# The fixture's libversions.h, row by row.
file(READ "${HEADER}" h)
foreach(row
    "{ \"Fixture\", \""
    "{ \"esp_lcd_touch\", \"1.1.2\", \"Apache-2.0\" }"
    "{ \"feckless_keyboard\", \"cd4b91b\", \"licence not found\" }"
    "{ \"ESP-IDF\", \"6.1.0\", \"Apache-2.0\" }"
    "{ \"tinyz\", \"3.0.2 (vendored)\", \"Unlicense\" }"
    "#define LIBVERSIONS_COUNT (5)")
    string(FIND "${h}" "${row}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "libversions.h lacks: ${row}\n${h}")
    endif()
endforeach()
if(h MATCHES "cmake_utilities")
    message(FATAL_ERROR "cmake_utilities listed\n${h}")
endif()
message(STATUS "gencheck: ok")
