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

find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(Qt6 REQUIRED COMPONENTS Core Gui)
find_package(KF6 REQUIRED COMPONENTS Config CoreAddons GlobalAccel)

# KWin 6 exports its effect API as the single linkable target KWin::kwin from
# the `KWin` CMake package (KWinConfig.cmake), with headers under
# <prefix>/include/kwin/{effect,core,opengl,...}. On Arch this comes from the
# `kwin` package itself; on Debian/Ubuntu from `kwin-dev`.
find_package(KWin CONFIG QUIET)

if(NOT TARGET KWin::kwin)
    message(FATAL_ERROR
        "Could not find KWin's effect development files (KWin::kwin target).\n"
        "Install them and re-run cmake. Arch has no -dev split, so `kwin` alone is\n"
        "enough there (and note Arch uses kconfig, not kf6-kconfig):\n"
        "  Arch / CachyOS : sudo pacman -S --needed cmake extra-cmake-modules \\\n"
        "                       qt6-tools kwin \\\n"
        "                       kconfig kcoreaddons kglobalaccel libepoxy qt6-base\n"
        "  Debian / Ubuntu: sudo apt install cmake extra-cmake-modules pkg-config \\\n"
        "                       kwin-dev qt6-base-dev libepoxy-dev \\\n"
        "                       libkf6config-dev libkf6coreaddons-dev libkf6globalaccel-dev\n"
        "  Fedora         : sudo dnf install cmake extra-cmake-modules pkgconf-pkg-config \\\n"
        "                       kwin-devel qt6-qtbase-devel libepoxy-devel \\\n"
        "                       kf6-kconfig-devel kf6-kcoreaddons-devel kf6-kglobalaccel-devel\n"
        "If they are installed but not found, point cmake at them:\n"
        "  cmake -B build -DKWin_DIR=/usr/lib/cmake/KWin\n")
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
