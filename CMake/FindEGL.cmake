#.rst:
# FindEGL
# -------
#
# Find EGL library
#
# Try to find EGL library. The following values are defined
#
# ::
#
#   EGL_FOUND         - True if egl is available
#   EGL_INCLUDE_DIRS  - Include directories for egl
#   EGL_LIBRARIES     - List of libraries for egl
#   EGL_DEFINITIONS   - List of definitions for egl
#
#=============================================================================
# Copyright (c) 2015 Jari Vetoniemi
#
# Distributed under the OSI-approved BSD License (the "License");
#
# This software is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the License for more information.
#=============================================================================

include(FeatureSummary)
set_package_properties(EGL PROPERTIES
   URL "http://www.khronos.org/egl/"
   DESCRIPTION "Native Platform Interface")

find_package(PkgConfig)
pkg_check_modules(PC_EGL QUIET egl)
find_library(EGL_LIBRARIES NAMES egl EGL HINTS ${PC_EGL_LIBRARY_DIRS})
find_path(EGL_INCLUDE_DIRS NAMES EGL/egl.h HINTS ${PC_EGL_INCLUDE_DIRS})

set(EGL_DEFINITIONS ${PC_EGL_CFLAGS_OTHER})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(EGL DEFAULT_MSG EGL_LIBRARIES EGL_INCLUDE_DIRS)
mark_as_advanced(EGL_INCLUDE_DIRS EGL_LIBRARIES EGL_DEFINITIONS)

