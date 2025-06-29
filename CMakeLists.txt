cmake_minimum_required(VERSION 3.16)
project(StremioRevamped)

include(ExternalProject)

# Ensure webapp folder exists
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/webapp)

ExternalProject_Add(stremio_web
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/stremio-web
    BINARY_DIR ${CMAKE_BINARY_DIR}/stremio-web-build
    CONFIGURE_COMMAND ""
    BUILD_COMMAND npm ci && npm run build
    BUILD_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/stremio-web
    INSTALL_COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/webapp
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/stremio-web/dist
                ${CMAKE_SOURCE_DIR}/webapp
)

add_subdirectory(stremio-webViewer)

add_dependencies(stremio stremio_web)

install(TARGETS stremio RUNTIME DESTINATION bin)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/webapp DESTINATION bin/webapp)

include(InstallRequiredSystemLibraries)
set(CPACK_PACKAGE_NAME "StremioRevamped")
set(CPACK_PACKAGE_VERSION_MAJOR "1")
set(CPACK_PACKAGE_VERSION_MINOR "0")
set(CPACK_GENERATOR "ZIP;NSIS;TGZ;DEB;RPM")
include(CPack)
