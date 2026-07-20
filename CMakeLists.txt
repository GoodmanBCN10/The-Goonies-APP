cmake_minimum_required(VERSION 3.14)

set(BOREALIS_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/vendor/borealis/library)
set(PLATFORM_SWITCH ON CACHE BOOL "" FORCE)
set(USE_DEKO3D ON CACHE BOOL "" FORCE)
set(USE_GLFW OFF CACHE BOOL "" FORCE)
set(USE_SDL2 OFF CACHE BOOL "" FORCE)
set(USE_SYSTEM_FMT OFF CACHE BOOL "" FORCE)
set(USE_SYSTEM_TINYXML2 OFF CACHE BOOL "" FORCE)
set(USE_SYSTEM_TWEENY OFF CACHE BOOL "" FORCE)
set(BRLS_UNITY_BUILD OFF CACHE BOOL "" FORCE)

include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

project(TheGooniesInstaller VERSION 2.0.0 LANGUAGES C CXX)
set(APP_TITLE "The Goonies APP")
set(APP_AUTHOR "GoodmanBCN")
set(APP_ICON ${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg)
set(ROMFS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/romfs)

add_subdirectory(vendor/borealis/library)
target_include_directories(borealis PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/glm
)

# ZSTD
file(GLOB ZSTD_COMMON_SOURCES CONFIGURE_DEPENDS vendor/zstd/lib/common/*.c)
file(GLOB ZSTD_DECOMPRESS_SOURCES CONFIGURE_DEPENDS vendor/zstd/lib/decompress/*.c)
add_library(pipensx_zstd STATIC ${ZSTD_COMMON_SOURCES} ${ZSTD_DECOMPRESS_SOURCES})
target_include_directories(pipensx_zstd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/zstd/lib)
target_compile_definitions(pipensx_zstd PRIVATE ZSTD_MULTITHREAD=0 ZSTD_DISABLE_ASM)

# Goonies sources
file(GLOB_RECURSE GOONIES_SOURCES
    source/installer/*.cpp source/installer/*.c
    source/utils/*.cpp source/utils/*.c
    source/yati/*.cpp source/yati/*.c
    source/minini/*.cpp source/minini/*.c
    source/libhaze/*.cpp source/libhaze/*.c
    source/mtp/*.cpp source/mtp/*.c
    source/ui/*.cpp source/ui/*.c
    source/ui/forwarders/*.cpp
    source/app/*.cpp source/app/*.c
    source/core/*.cpp source/core/*.c
    source/install/*.cpp source/install/*.c
)

list(APPEND GOONIES_SOURCES
    source/platform/storage.c
    source/platform/switch_crashlog.c
    source/platform/switch_performance.cpp
)


add_executable(TheGooniesInstaller
    source/main.cpp
    source/catalog.cpp
    source/torrent_engine.cpp
    source/fs.cpp
    source/log.cpp
    source/nro.cpp
    source/forwarder.cpp
    vendor/dht/dht.c
    vendor/qrcodegen/qrcodegen.cpp
    vendor/libnx-ext/libnx-ipcext/source/es.c
    vendor/libnx-ext/libnx-ipcext/source/ns-ext.c
    vendor/borealis/library/lib/platforms/switch/switch_wrapper.c
    ${GOONIES_SOURCES}
)

set_target_properties(TheGooniesInstaller PROPERTIES
    C_STANDARD 11
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS ON
)

# Copy Borealis Resources
file(MAKE_DIRECTORY ${ROMFS_DIR})
file(COPY
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/borealis/resources/img/sys
    DESTINATION ${ROMFS_DIR}/img
)
file(COPY
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/borealis/resources/material
    DESTINATION ${ROMFS_DIR}
)
file(COPY
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/borealis/resources/i18n/en-US
    DESTINATION ${ROMFS_DIR}/i18n
)


# Includes
target_include_directories(TheGooniesInstaller PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/minini
    ${CMAKE_CURRENT_SOURCE_DIR}/include/yati/nx/nxdumptool
    ${CMAKE_CURRENT_SOURCE_DIR}/include/libhaze
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/borealis/library/include
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libnx-ext/libnx-ipcext/include
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libnx-ext/libnx-ext/include
)

target_link_directories(TheGooniesInstaller PRIVATE
    /opt/devkitpro/libnx/lib
    /opt/devkitpro/portlibs/switch/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
)

target_link_libraries(TheGooniesInstaller PRIVATE
    borealis
    pipensx_zstd
    usbhsfs
    ntfs-3g
    lwext4
    curl
    mbedtls mbedcrypto mbedx509
    z
    minizip
    miniupnpc
)

target_compile_definitions(TheGooniesInstaller PRIVATE
    PIPENSX_VERSION="${APP_VERSION}"
)

if (PLATFORM_SWITCH)
    target_link_libraries(TheGooniesInstaller PRIVATE nx)
    
    add_custom_target(TheGooniesInstaller_nro ALL
        DEPENDS TheGooniesInstaller
        COMMAND ${NX_NACPTOOL_EXE}
            --create "${APP_TITLE}" "${APP_AUTHOR}" "${PROJECT_VERSION}"
            TheGooniesInstaller.nacp
        COMMAND ${NX_ELF2NRO_EXE}
            $<TARGET_FILE:TheGooniesInstaller> TheGooniesInstaller.nro
            --icon=${APP_ICON}
            --nacp=TheGooniesInstaller.nacp
            --romfsdir=${ROMFS_DIR}
        BYPRODUCTS TheGooniesInstaller.nacp TheGooniesInstaller.nro
        VERBATIM
    )
endif()
