cmake_minimum_required(VERSION 3.22)
project(ibus-wetypex VERSION 2.2.3.657 LANGUAGES C CXX)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/PKGBUILD.in"
  WETYPE_PACKAGE_RELEASE_LINE REGEX "^pkgrel=[0-9]+$")
string(REGEX REPLACE "^pkgrel=" "" WETYPE_PACKAGE_RELEASE "${WETYPE_PACKAGE_RELEASE_LINE}")

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)
endif()

include(GNUInstallDirs)
include(FetchContent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(JSONC REQUIRED IMPORTED_TARGET json-c)
# pkg_check_modules(WETYPE_WAYLAND REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(IBUS REQUIRED IMPORTED_TARGET ibus-1.0)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4>=4.6)
pkg_check_modules(ADW REQUIRED IMPORTED_TARGET libadwaita-1>=1.1)
pkg_check_modules(GIO_UNIX REQUIRED IMPORTED_TARGET gio-unix-2.0)

# set(WETYPE_QT_VERSION "AUTO" CACHE STRING "UI toolkit: AUTO, 5 or 6")
# set_property(CACHE WETYPE_QT_VERSION PROPERTY STRINGS AUTO 5 6)
# if(NOT WETYPE_QT_VERSION MATCHES "^(AUTO|5|6)$")
#   message(FATAL_ERROR "WETYPE_QT_VERSION must be AUTO, 5 or 6")
# endif()

# if(NOT WETYPE_QT_VERSION STREQUAL "5")
#   find_package(Qt6 6.2 QUIET COMPONENTS Widgets Gui DBus Network Svg WebEngineWidgets)
#   if(TARGET Qt6::WebEngineWidgets AND TARGET Qt6::Svg)
#     set(WETYPE_QT_PACKAGE Qt6)
#     set(WETYPE_UI_QT_MAJOR 6)
#   elseif(WETYPE_QT_VERSION STREQUAL "6")
#     message(FATAL_ERROR "Qt 6.2+ with WebEngine and the selected frontend development files is required")
#   endif()
# endif()

# if(NOT WETYPE_QT_PACKAGE)
#   find_package(Qt5 5.12 REQUIRED COMPONENTS Widgets Gui DBus Network Svg WebEngineWidgets)
#   set(WETYPE_QT_PACKAGE Qt5)
#   set(WETYPE_UI_QT_MAJOR 5)
# endif()

# set(QT_DEFAULT_MAJOR_VERSION ${WETYPE_UI_QT_MAJOR})
# message(STATUS "WeTypeX UI toolkit: ${WETYPE_QT_PACKAGE}")

# add_executable(wetypex-ai-viewer src/ai/main.cpp)
# target_link_libraries(wetypex-ai-viewer PRIVATE ${WETYPE_QT_PACKAGE}::Widgets ${WETYPE_QT_PACKAGE}::Network ${WETYPE_QT_PACKAGE}::WebEngineWidgets)
# target_compile_definitions(wetypex-ai-viewer PRIVATE
#   WETYPE_AI_HELPER="${CMAKE_INSTALL_FULL_BINDIR}/wetypex-ai")

# add_executable(wetypex-transfer src/transfer/main.cpp)
# target_link_libraries(wetypex-transfer PRIVATE ${WETYPE_QT_PACKAGE}::Widgets ${WETYPE_QT_PACKAGE}::Network)
# target_compile_definitions(wetypex-transfer PRIVATE
#   WETYPE_ACCOUNT_TOOL="${CMAKE_INSTALL_FULL_BINDIR}/wetypex-account"
#   WETYPE_ENGINE_HOST="${CMAKE_INSTALL_FULL_LIBDIR}/${PROJECT_NAME}/wetypex-engine-host"
#   WETYPE_SUPPORT_DIR="${CMAKE_INSTALL_FULL_LIBDIR}/${PROJECT_NAME}"
#   WETYPE_SETTINGS="${CMAKE_INSTALL_FULL_BINDIR}/wetypex-settings")

# add_executable(wetypex-vmode src/vmode.cpp)
# target_link_libraries(wetypex-vmode PRIVATE ${WETYPE_QT_PACKAGE}::Widgets ${WETYPE_QT_PACKAGE}::Network)
# set(WETYPE_LAYER_SHELL "AUTO" CACHE STRING "Wayland V toolbar: AUTO, ON or OFF")
# set_property(CACHE WETYPE_LAYER_SHELL PROPERTY STRINGS AUTO ON OFF)
# if(NOT WETYPE_LAYER_SHELL MATCHES "^(AUTO|ON|OFF)$")
#   message(FATAL_ERROR "WETYPE_LAYER_SHELL must be AUTO, ON or OFF")
# endif()
# if(NOT WETYPE_LAYER_SHELL STREQUAL "OFF")
#   find_package(LayerShellQt ${WETYPE_UI_QT_MAJOR} QUIET)
#   if(TARGET LayerShellQt::Interface AND TARGET PkgConfig::WETYPE_WAYLAND)
#     target_link_libraries(wetypex-vmode PRIVATE LayerShellQt::Interface PkgConfig::WETYPE_WAYLAND)
#     target_compile_definitions(wetypex-vmode PRIVATE WETYPE_LAYER_SHELL)
#     message(STATUS "WeTypeX Wayland V toolbar: layer-shell")
#   elseif(WETYPE_LAYER_SHELL STREQUAL "ON")
#     message(FATAL_ERROR "LayerShellQt for Qt${WETYPE_UI_QT_MAJOR} and wayland-client are required")
#   endif()
# endif()

add_executable(wetypex-clipboard src/clipboard/client.cpp)
target_link_libraries(wetypex-clipboard PRIVATE PkgConfig::GIO_UNIX PkgConfig::JSONC)
target_compile_definitions(wetypex-clipboard PRIVATE
  WETYPE_ACCOUNT_TOOL="${CMAKE_INSTALL_FULL_BINDIR}/wetypex-account")
target_compile_options(wetypex-clipboard PRIVATE -Wall -Wextra)

if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
  message(FATAL_ERROR "The verified original engine sample requires Linux x86-64")
endif()

set(ENABLE_TESTING OFF CACHE BOOL "libkqueue tests" FORCE)
FetchContent_Declare(kqueue
  GIT_REPOSITORY https://github.com/mheily/libkqueue.git
  GIT_TAG 46a3e130f88b0b0742575dcb01d77e336538024b
)
FetchContent_GetProperties(kqueue)
if(NOT kqueue_POPULATED)
  FetchContent_Populate(kqueue)
  add_subdirectory(${kqueue_SOURCE_DIR} ${kqueue_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

set(WETYPE_SUPPORT_DIR "${CMAKE_INSTALL_FULL_LIBDIR}/${PROJECT_NAME}")

add_executable(ibus-engine-wetypex src/ibus/main.cpp)
target_link_libraries(ibus-engine-wetypex PRIVATE PkgConfig::IBUS PkgConfig::JSONC)
target_compile_definitions(ibus-engine-wetypex PRIVATE
  WETYPE_LAUNCHER="${CMAKE_INSTALL_FULL_BINDIR}/wetypex-backend"
  WETYPE_SYNCD="${CMAKE_INSTALL_FULL_BINDIR}/wetypex-clipboard")
target_compile_options(ibus-engine-wetypex PRIVATE -Wall -Wextra)

add_executable(wetypex-settings
  src/settings/main.cpp
  src/settings/settings_store.cpp
  src/settings/engine_client.cpp
  src/settings/account_client.cpp)
target_link_libraries(wetypex-settings PRIVATE
  PkgConfig::ADW PkgConfig::GTK4 PkgConfig::GIO_UNIX PkgConfig::JSONC)
target_compile_definitions(wetypex-settings PRIVATE
  WETYPE_ACCOUNT_TOOL="${CMAKE_INSTALL_FULL_BINDIR}/wetypex-account"
  WETYPE_BACKEND="${CMAKE_INSTALL_FULL_BINDIR}/wetypex-backend"
  WETYPE_VERSION="${PROJECT_VERSION}")
target_compile_options(wetypex-settings PRIVATE -Wall -Wextra)

add_executable(wetypex-engine-host src/backend/macos_host.cpp)
add_dependencies(wetypex-engine-host kqueue)
target_link_libraries(wetypex-engine-host PRIVATE PkgConfig::JSONC Threads::Threads ${CMAKE_DL_LIBS})
target_compile_options(wetypex-engine-host PRIVATE -Wall -Wextra)

find_program(WETYPE_CLANGXX clang++ REQUIRED)
if(NOT WETYPE_CLANGXX)
  message(FATAL_ERROR "clang++ is required for the original libc++ ABI")
endif()

option(WETYPE_BUNDLED_CURL "Embed pinned WebSocket-capable libcurl in the transport bridge" ON)
if(WETYPE_BUNDLED_CURL)
  include(cmake/BundledCurl.cmake)
else()
  pkg_check_modules(CURL REQUIRED libcurl>=8.9)
  set(WETYPE_CURL_COMPILE_FLAGS ${CURL_CFLAGS})
  set(WETYPE_CURL_LINK_FLAGS ${CURL_LDFLAGS} -lcurl)
endif()

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/wetypex-locale.so
  COMMAND ${WETYPE_CLANGXX} -std=c++17 -stdlib=libc++ -shared -fPIC -O2
  ${CMAKE_CURRENT_SOURCE_DIR}/src/backend/darwin_locale.cpp -ldl -pthread
  -Wl,-z,relro,-z,now -o ${CMAKE_CURRENT_BINARY_DIR}/wetypex-locale.so
  DEPENDS src/backend/darwin_locale.cpp VERBATIM)
add_custom_target(wetypex-locale ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/wetypex-locale.so)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/wcwss_bridge.so
  COMMAND ${WETYPE_CLANGXX} -std=c++17 -stdlib=libc++ -shared -fPIC -O2
  ${WETYPE_CURL_COMPILE_FLAGS} ${CMAKE_CURRENT_SOURCE_DIR}/src/backend/wcwss_bridge.cpp
  ${WETYPE_CURL_LINK_FLAGS} -pthread -Wl,-z,relro,-z,now,-z,defs
  -o ${CMAKE_CURRENT_BINARY_DIR}/wcwss_bridge.so
  DEPENDS src/backend/wcwss_bridge.cpp ${WETYPE_CURL_LIBRARY} VERBATIM COMMAND_EXPAND_LISTS)
add_custom_target(wetypex-wcwss-bridge ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/wcwss_bridge.so)
if(WETYPE_BUNDLED_CURL)
  add_dependencies(wetypex-wcwss-bridge wetypex-curl)
endif()

configure_file(data/wetypex.xml.in wetypex.xml @ONLY)
configure_file(scripts/wetypex-account.sh.in wetypex-account @ONLY)
configure_file(scripts/wetypex-ai.sh.in wetypex-ai @ONLY)
configure_file(scripts/wetypex-backend.sh.in wetypex-backend @ONLY)
configure_file(scripts/wetypex-sync.sh.in wetypex-sync @ONLY)
configure_file(scripts/wetypex-update.sh.in wetypex-update @ONLY)
configure_file(scripts/wetypex-voice.sh.in wetypex-voice @ONLY)
configure_file(scripts/wetypex-setup.sh.in wetypex-setup @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/wetypex-locale.so DESTINATION ${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME} RENAME locale.so)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/wcwss_bridge.so DESTINATION ${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME})

# install(TARGETS wetypex-ai-viewer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS wetypex-clipboard RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS wetypex-settings RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# install(TARGETS wetypex-transfer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# install(TARGETS wetypex-vmode RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS ibus-engine-wetypex RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
install(TARGETS kqueue LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME})
install(TARGETS wetypex-engine-host RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME})

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/wetypex-account DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/wetypex-ai DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/wetypex-backend DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/wetypex-sync DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/wetypex-update DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/wetypex-voice DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/wetypex-setup DESTINATION ${CMAKE_INSTALL_BINDIR})

install(PROGRAMS scripts/setup_runtime.py tools/prepare_macos_runtime.py DESTINATION ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME})
install(PROGRAMS scripts/wechat_emoticons.py DESTINATION ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME})
install(PROGRAMS scripts/voice_stream.py DESTINATION ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME})

install(DIRECTORY src/clipboard/wetypex-clip-watcher@caidy.cc DESTINATION ${CMAKE_INSTALL_DATADIR}/gnome-shell/extensions)
install(DIRECTORY data/icons/hicolor/ DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor)

install(FILES data/wechat-emoticons.json DESTINATION ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME})
install(FILES data/wechat-emoticons.json DESTINATION ${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME})
install(FILES data/wetypex-symbols.json DESTINATION ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME})
install(FILES data/wetypex-settings.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/wetypex.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/ibus/component)

# Install docs and license
install(FILES ${kqueue_SOURCE_DIR}/LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME libkqueue-LICENSE)
install(FILES ${kqueue_SOURCE_DIR}/LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME libkqueue-ISC-LICENSE)
install(FILES ${kqueue_SOURCE_DIR}/LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME libkqueue-BSD-2-Clause-LICENSE)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES NOTICE DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(DIRECTORY docs DESTINATION ${CMAKE_INSTALL_DOCDIR})

set(CPACK_PACKAGE_CONTACT "WeTypeX community project")
set(CPACK_GENERATOR TXZ)
include(CPack)
