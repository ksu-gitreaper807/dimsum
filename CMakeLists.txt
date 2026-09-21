# SPDX-FileCopyrightText: 2026 The dimsum contributors
# SPDX-License-Identifier: MIT
#
# Out-of-tree native KWin effect.
#
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#   sudo cmake --install build          # or: ./scripts/install.sh
#
# See scripts/verify-api.sh — run it *before* building to confirm the
# installed KWin headers still expose every symbol this effect uses.

cmake_minimum_required(VERSION 3.16)

project(kwin4_effect_software_dim
    VERSION 1.0.0
    DESCRIPTION "Compositor-wide software dimmer for KWin 6.7"
    LANGUAGES CXX
)

# KWin 6.7 itself builds as C++23; this effect only needs C++20 (none of the
# KWin headers it includes use newer language features).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# --------------------------------------------------------------------------
# Dependencies
# --------------------------------------------------------------------------

find_package(ECM 6.0.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_MODULE_PATH})

# Must be set *before* KDECMakeSettings / KDECompilerSettings, otherwise
# ECM's QtVersionOption defaults to Qt5 and KF6Config's dependency on
# Qt6Qml then errors with:
#   CMake Error at Qt6QmlMacros.cmake: qt_generate_foreign_qml_types() is only available in Qt 6.
# See https://github.com/KDAB/GammaRay/issues/742
set(QT_MAJOR_VERSION 6)
set(KF_MAJOR_VERSION 6)
set(QT_MIN_VERSION "6.6.0")
set(KF_MIN_VERSION "6.0.0")
set(KF6_MIN_VERSION "6.0.0")

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

# KDECompilerSettings may reset the standard; enforce C++20 after it.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS Config CoreAddons GlobalAccel)

# KWin 6 exports its effect API as the single linkable target KWin::kwin from
# the `KWin` CMake package (KWinConfig.cmake), with headers under
# <prefix>/include/kwin/{effect,core,opengl,...}. On Arch this comes from the
# `kwin` package itself; on Debian/Ubuntu from `kwin-dev`.
#
# KWinConfig.cmake itself does find_dependency() for Qt6Quick, KF6WindowSystem,
# Wayland, epoxy, Libdrm and Vulkan. If any of those are missing (e.g.
# vulkan-headers on Arch, which is a make-dep of kwin but not a runtime dep),
# find_package(KWin) will fail even though kwin is installed.
# We first try the normal package, and if that does not provide KWin::kwin,
# we fall back to manually locating the headers + libkwin.so and creating an
# imported target ourselves — enough to build this effect.
find_package(KWin CONFIG QUIET)

if(NOT TARGET KWin::kwin)
    message(STATUS "KWin::kwin target not found via KWinConfig.cmake, trying fallback manual detection...")
    # Show the real error from KWinConfig.cmake (e.g. missing Vulkan) by
    # re-running without QUIET.
    find_package(KWin CONFIG)
    if(NOT TARGET KWin::kwin)
        find_path(KWIN_FALLBACK_INCLUDE_DIR
            NAMES kwin/effect/effect.h
            PATHS /usr/include /usr/local/include /usr/include/x86_64-linux-gnu
            NO_DEFAULT_PATH
        )
        if(NOT KWIN_FALLBACK_INCLUDE_DIR)
            find_path(KWIN_FALLBACK_INCLUDE_DIR NAMES kwin/effect/effect.h)
        endif()
        find_library(KWIN_FALLBACK_LIBRARY NAMES kwin libkwin
            PATHS /usr/lib /usr/lib64 /usr/lib/x86_64-linux-gnu
        )
        if(KWIN_FALLBACK_INCLUDE_DIR AND KWIN_FALLBACK_LIBRARY)
            message(STATUS "Fallback: found KWin headers at ${KWIN_FALLBACK_INCLUDE_DIR} and library at ${KWIN_FALLBACK_LIBRARY}")
            add_library(KWin::kwin UNKNOWN IMPORTED)
            set_target_properties(KWin::kwin PROPERTIES
                IMPORTED_LOCATION "${KWIN_FALLBACK_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${KWIN_FALLBACK_INCLUDE_DIR}"
            )
            # KWin's headers need these as well; if they are missing the build
            # will still fail, but with a more obvious error.
            find_package(Wayland QUIET COMPONENTS Server)
            if(TARGET Wayland::Server)
                set_property(TARGET KWin::kwin APPEND PROPERTY INTERFACE_LINK_LIBRARIES Wayland::Server)
            endif()
        endif()
    endif()
endif()

if(NOT TARGET KWin::kwin)
    message(FATAL_ERROR
        "Could not find KWin's effect development files (KWin::kwin target).\n"
        "The KWin CMake package was found at ${KWin_DIR} but did not provide KWin::kwin,\n"
        "most often because one of its own dependencies is missing (check the lines above\n"
        "for 'Could NOT find ...'). On Arch, kwin's make-deps include vulkan-headers,\n"
        "wayland-protocols, plasma-wayland-protocols which are NOT pulled as runtime deps.\n"
        "Install them and re-run cmake. Arch has no -dev split, so `kwin` alone is\n"
        "enough there (and note Arch uses kconfig, not kf6-kconfig):\n"
        "  Arch / CachyOS : sudo pacman -S --needed cmake extra-cmake-modules \\\n"
        "                       qt6-tools kwin vulkan-headers wayland wayland-protocols \\\n"
        "                       plasma-wayland-protocols libdrm \\\n"
        "                       kconfig kcoreaddons kglobalaccel libepoxy qt6-base\n"
        "  Debian / Ubuntu: sudo apt install cmake extra-cmake-modules pkg-config \\\n"
        "                       kwin-dev qt6-base-dev libepoxy-dev libwayland-dev \\\n"
        "                       libvulkan-dev libdrm-dev \\\n"
        "                       libkf6config-dev libkf6coreaddons-dev libkf6globalaccel-dev \\\n"
        "                       libkf6windowsystem-dev\n"
        "  Fedora         : sudo dnf install cmake extra-cmake-modules pkgconf-pkg-config \\\n"
        "                       kwin-devel qt6-qtbase-devel libepoxy-devel wayland-devel \\\n"
        "                       vulkan-headers libdrm-devel \\\n"
        "                       kf6-kconfig-devel kf6-kcoreaddons-devel kf6-kglobalaccel-devel \\\n"
        "                       kf6-kwindowsystem-devel\n"
        "If they are installed but not found, point cmake at them:\n"
        "  cmake -B build -DKWin_DIR=/usr/lib/cmake/KWin\n"
        "Or bypass KWinConfig.cmake entirely by ensuring /usr/include/kwin and libkwin.so exist:\n"
        "  ls /usr/include/kwin/effect/effect.h && ls /usr/lib/libkwin.so\n")
endif()

# libepoxy provides the GL headers (epoxy/gl.h) that KWin's own effects use.
find_package(PkgConfig REQUIRED)
pkg_check_modules(EPOXY REQUIRED IMPORTED_TARGET epoxy)

# --------------------------------------------------------------------------
# Target
# --------------------------------------------------------------------------

set(softwaredim_SOURCES
    src/main.cpp
    src/softwaredim.cpp
    src/softwaredim.qrc
)

add_library(kwin4_effect_software_dim MODULE ${softwaredim_SOURCES})

# KWin's own effect libraries are named kwin4_effect_<id>.so with no "lib"
# prefix; keep that convention so the plugin sits next to the built-ins.
set_target_properties(kwin4_effect_software_dim PROPERTIES
    PREFIX ""
    OUTPUT_NAME "kwin4_effect_software_dim"
)

target_link_libraries(kwin4_effect_software_dim PRIVATE
    KWin::kwin
    Qt::Core
    Qt::Gui
    KF6::ConfigCore
    KF6::CoreAddons
    KF6::GlobalAccel
    PkgConfig::EPOXY
)

# --------------------------------------------------------------------------
# Install
# --------------------------------------------------------------------------

# KWin scans <qt plugin dir>/kwin/effects/plugins for effect plugins, e.g.
#   /usr/lib/qt6/plugins/kwin/effects/plugins/kwin4_effect_software_dim.so
# (src/effect/effectloader.cpp: PluginEffectLoader::m_pluginSubDirectory).
# Note the built-in effects are compiled statically into kwin_wayland, so an
# empty plugins directory on a fresh system is normal — it is still the right
# place. scripts/verify-api.sh confirms the Qt plugin dir on your machine.
set(KWIN_EFFECTS_INSTALL_DIR "${KDE_INSTALL_PLUGINDIR}/kwin/effects/plugins"
    CACHE PATH "Directory KWin loads native effect plugins from")

install(TARGETS kwin4_effect_software_dim
    LIBRARY DESTINATION "${KWIN_EFFECTS_INSTALL_DIR}"
)

# Keep a copy of the metadata next to the build for reference; the copy
# compiled into the .so is the authoritative one.
install(FILES src/metadata.json
    DESTINATION "${KDE_INSTALL_DATADIR}/kwin/effects/metadata.json/kwin4_effect_software_dim"
)

# --------------------------------------------------------------------------
# Report
# --------------------------------------------------------------------------

message(STATUS "")
message(STATUS "Software Dim ${PROJECT_VERSION}")
message(STATUS "  KWin package       : ${KWin_DIR} (version ${KWin_VERSION})")
message(STATUS "  plugin install dir  : ${KWIN_EFFECTS_INSTALL_DIR}")
message(STATUS "  build type          : ${CMAKE_BUILD_TYPE}")
message(STATUS "")
