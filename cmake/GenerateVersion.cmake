# Resolve the project version from the nearest "v*" git tag and (re)generate
# version.h from version.h.in. Runs in CMake script mode:
#
#   cmake -DSRC_DIR=<repo> -DBIN_DIR=<build> -DFALLBACK_VERSION=3.0.0 \
#         [-DIRCABOT_VERSION=<override>] -P cmake/GenerateVersion.cmake
#
# A non-empty -DIRCABOT_VERSION overrides git detection (used by packaging/CI).
# The resolved version is also written to <BIN_DIR>/ircabot_version.txt so the
# parent CMake run can read it back (for CPack, banners, etc.).

set(_version "${IRCABOT_VERSION}")

if(_version STREQUAL "")
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${SRC_DIR}" describe --tags --match "v*" --dirty
            OUTPUT_VARIABLE _desc
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _res
        )
        if(_res EQUAL 0 AND NOT _desc STREQUAL "")
            set(_version "${_desc}")
        endif()
    endif()
endif()

if(_version STREQUAL "")
    set(_version "${FALLBACK_VERSION}")
endif()

# "v3.0.0" -> "3.0.0"
string(REGEX REPLACE "^v" "" _version "${_version}")

# configure_file substitutes @IRCABOT_VERSION@; only rewrites version.h when the
# content actually changes, so unchanged versions do not trigger a rebuild.
set(IRCABOT_VERSION "${_version}")
configure_file("${SRC_DIR}/src/version.h.in" "${BIN_DIR}/version.h" @ONLY)
file(WRITE "${BIN_DIR}/ircabot_version.txt" "${_version}")
