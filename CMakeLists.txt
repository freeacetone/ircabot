cmake_minimum_required(VERSION 3.16)
project(ircabot VERSION 3.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)

# --- Version from the latest v* git tag ------------------------------------
# Override with -DIRCABOT_VERSION=x.y.z (packaging/CI); empty = detect from git,
# falling back to the project() version when there is no tag / no git.
set(IRCABOT_VERSION "" CACHE STRING "Project version override (default: nearest v* git tag)")

set(_version_script ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateVersion.cmake)
set(_version_args
    -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}
    -DBIN_DIR=${CMAKE_CURRENT_BINARY_DIR}
    -DFALLBACK_VERSION=${PROJECT_VERSION}
    -DIRCABOT_VERSION=${IRCABOT_VERSION}
)

# Resolve once at configure time so Version.h exists and CPack knows the version.
execute_process(
    COMMAND ${CMAKE_COMMAND} ${_version_args} -P ${_version_script}
)
file(READ ${CMAKE_CURRENT_BINARY_DIR}/ircabot_version.txt IRCABOT_VERSION_RESOLVED)
string(STRIP "${IRCABOT_VERSION_RESOLVED}" IRCABOT_VERSION_RESOLVED)
message(STATUS "IRCaBot version: ${IRCABOT_VERSION_RESOLVED}")

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Network HttpServer Concurrent)
find_package(OpenSSL REQUIRED) # libcrypto: AES-256 for the stateless captcha nonce

add_executable(ircabot
    src/Main.cpp
    src/Captcha.cpp
    src/Captcha.h
    src/CaptchaImage.cpp
    src/CaptchaImage.h
    src/Config.cpp
    src/Config.h
    src/IrcClient.cpp
    src/IrcClient.h
    src/LogStore.cpp
    src/LogStore.h
    src/Render.cpp
    src/Render.h
    src/State.cpp
    src/State.h
    src/Util.cpp
    src/Util.h
    src/VoiceGate.cpp
    src/VoiceGate.h
    src/WebUi.cpp
    src/WebUi.h
    assets/assets.qrc
)

# Re-resolve the version on every build so new commits/tags are reflected
# without reconfiguring (Version.h is rewritten only when it actually changes).
add_custom_target(ircabot_version ALL
    BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/Version.h
    COMMAND ${CMAKE_COMMAND} ${_version_args} -P ${_version_script}
    COMMENT "Resolving IRCaBot version from git"
    VERBATIM
)
add_dependencies(ircabot ircabot_version)

# The generated Version.h lives in the build tree.
target_include_directories(ircabot PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(ircabot PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::HttpServer
    Qt6::Concurrent
    OpenSSL::Crypto
)

# --- Install ---------------------------------------------------------------
install(TARGETS ircabot RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# systemd unit with ExecStart pointing at the real install location.
set(IRCABOT_EXEC ${CMAKE_INSTALL_FULL_BINDIR}/ircabot)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/systemd/ircabot.service.in
    ${CMAKE_CURRENT_BINARY_DIR}/ircabot.service
    @ONLY
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ircabot.service
        DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system)

# Reference config shipped for the packaging postinst (and for humans).
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/config.example.json
        DESTINATION ${CMAKE_INSTALL_DATADIR}/ircabot)

# --- Debian package (CPack) ------------------------------------------------
set(CPACK_PACKAGE_NAME "ircabot")
set(CPACK_PACKAGE_VERSION "${IRCABOT_VERSION_RESOLVED}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "IRC chat logger with a JS-free web interface")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/freeacetone/ircabot")

set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "acetone <63557806+freeacetone@users.noreply.github.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
# Resolve Qt (and other) runtime dependencies automatically from the binary.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "adduser, init-system-helpers (>= 1.52)")
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/deb/postinst;${CMAKE_CURRENT_SOURCE_DIR}/cmake/deb/prerm;${CMAKE_CURRENT_SOURCE_DIR}/cmake/deb/postrm")

include(CPack)
