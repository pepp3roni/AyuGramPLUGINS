# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

# AyuGram plugin system: embeds CPython 3.11 for running .plugin files.
#
# This module locates an embeddable CPython 3.11 interpreter and exposes the
# vendored pybind11 headers (Telegram/SourceFiles/ayu/libs/pybind11/include)
# through the ayugram::python INTERFACE target. The main Telegram target links
# against ayugram::python, and ayu/plugins/* uses pybind11 to drive the
# interpreter.
#
# Define AYUGRAM_DISABLE_PLUGINS to build without the plugin system; in that
# case ayugram::python is still created but carries only the
# AYUGRAM_DISABLE_PLUGINS compile definition and no Python dependency, so the
# ayu/plugins/* sources compile to inert stubs.

add_library(ayugram_python INTERFACE)
add_library(ayugram::python ALIAS ayugram_python)

if (AYUGRAM_DISABLE_PLUGINS)
    target_compile_definitions(ayugram_python INTERFACE AYUGRAM_DISABLE_PLUGINS)
    message(STATUS "AyuGram: plugin system disabled (AYUGRAM_DISABLE_PLUGINS).")
    return()
endif()

find_package(Python3 3.11 COMPONENTS Development.Embed Interpreter)

if (NOT Python3_FOUND)
    target_compile_definitions(ayugram_python INTERFACE AYUGRAM_DISABLE_PLUGINS)
    message(WARNING
        "AyuGram: embeddable Python3 (>=3.11, Development.Embed) was not found. "
        "Building with the plugin system disabled. Install the Python "
        "development package or set Python3_ROOT_DIR to enable plugins.")
    return()
endif()

set(ayugram_pybind11_loc
    ${CMAKE_CURRENT_SOURCE_DIR}/SourceFiles/ayu/libs/pybind11/include)

target_include_directories(ayugram_python INTERFACE
    ${ayugram_pybind11_loc})

target_link_libraries(ayugram_python INTERFACE
    Python3::Python)

message(STATUS
    "AyuGram: plugin system enabled with Python ${Python3_VERSION} "
    "(${Python3_EXECUTABLE}).")
