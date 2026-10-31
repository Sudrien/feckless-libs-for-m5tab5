# Fetch the single-header decoders feckless_image_handler vendors, at
# configure time (7005; moved from Defeatist's cmake/vendored.cmake).
#
# Pinned to a commit, not a branch, and every file checked against a
# SHA256. Nothing here is committed to the repository, so the pin is the
# only thing that decides what a fresh clone gets. The files land in
# ../vendor/, which is gitignored; they are not required to be fetched
# if they are already there and match.
#
# SPDX-License-Identifier: MIT

# stb_image v2.30: the last JPEG decoder, and GIF.
set(IMG_STB_COMMIT 013ac3beddff3dbffafd5177e7972067cd2b5083)
# J40 version 2270: the JPEG XL decoder.
set(IMG_J40_COMMIT 252e7987d36d50f617f29e94eeeb741198adc617)

# vendored_fetch(<url> <destination> <sha256>)
function(vendored_fetch url dest sha)
    get_filename_component(name "${dest}" NAME)

    if(EXISTS "${dest}")
        file(SHA256 "${dest}" have)
        if(have STREQUAL "${sha}")
            return()
        endif()
        # A stale file from an older pin, or a half-written download.
        # Replacing it is safe: it is gitignored, so nothing local is
        # lost, and leaving it would fail the build with a confusing
        # mismatch on every subsequent configure.
        message(STATUS "vendored: ${name} does not match the pin, refetching")
        file(REMOVE "${dest}")
    endif()

    message(STATUS "vendored: fetching ${name}")

    # Downloaded to a temporary name and checked by hand rather than with
    # EXPECTED_HASH. EXPECTED_HASH raises its own FATAL_ERROR the instant
    # the hash differs, which skips everything below -- so the message
    # never appears and, worse, the rejected file is left in place for
    # the next configure to trip over. Verifying afterwards keeps both
    # the cleanup and the explanation.
    file(DOWNLOAD "${url}" "${dest}.tmp" TLS_VERIFY ON STATUS status)

    list(GET status 0 code)
    if(code EQUAL 0)
        file(SHA256 "${dest}.tmp" got)
        if(NOT got STREQUAL "${sha}")
            set(code 1)
            set(reason "SHA256 mismatch: expected ${sha}, got ${got}")
        endif()
    else()
        list(GET status 1 reason)
    endif()

    if(NOT code EQUAL 0)
        file(REMOVE "${dest}.tmp")
        message(FATAL_ERROR
            "Could not fetch ${name}: ${reason}\n"
            "\n"
            "This file is not committed to the repository. If this "
            "machine has no network access, fetch it elsewhere and put "
            "it in ${CMAKE_CURRENT_LIST_DIR}/../vendor/ ; the build only "
            "cares that the file exists and matches the pin.\n"
            "\n"
            "A mismatch rather than a network failure means upstream "
            "moved or the pin is wrong. Do not just update the hash.")
    endif()

    file(RENAME "${dest}.tmp" "${dest}")
endfunction()

set(_vendor_dir "${CMAKE_CURRENT_LIST_DIR}/../vendor")

vendored_fetch("https://raw.githubusercontent.com/nothings/stb/${IMG_STB_COMMIT}/stb_image.h"
               "${_vendor_dir}/stb_image.h"
               594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3)

vendored_fetch("https://raw.githubusercontent.com/lifthrasiir/j40/${IMG_J40_COMMIT}/j40.h"
               "${_vendor_dir}/j40.h"
               981c80c07d46d7a263d7790e71f6d9713278974088235cffcf21dea4fc3b5f6d)
