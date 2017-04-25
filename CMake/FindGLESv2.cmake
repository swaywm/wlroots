#.rst:
# FindGLESv2
# -------
#
# Find GLESv2 library
#
# Try to find GLESv2 library. The following values are defined
#
# ::
#
#   GLESv2_FOUND         - True if glesv2 is available
#   GLESv2_INCLUDE_DIRS  - Include directories for glesv2
#   GLESv2_LIBRARIES     - List of libraries for glesv2
#   GLESv2_DEFINITIONS   - List of definitions for glesv2
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
set_package_properties(GLESv2 PROPERTIES
   URL "https://www.khronos.org/opengles/"
   DESCRIPTION "The Standard for Embedded Accelerated 3D Graphics")

find_package(PkgConfig)
pkg_check_modules(PC_GLES2 QUIET glesv2)
find_library(GLESv2_LIBRARIES NAMES GLESv2 ${PC_GLES2_LIBRARY_DIRS})
find_path(GLESv2_INCLUDE_DIRS NAMES GLES2/gl2.h HINTS ${PC_GLES2_INCLUDE_DIRS})

set(GLESv2_DEFINITIONS ${PC_GLES2_CFLAGS_OTHER})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GLESv2 DEFAULT_MSG GLESv2_LIBRARIES GLESv2_INCLUDE_DIRS)
mark_as_advanced(GLESv2_INCLUDE_DIRS GLESv2_LIBRARIES GLESv2_DEFINITIONS)

