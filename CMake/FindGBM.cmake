#.rst:
# FindGBM
# -------
#
# Find GBM library
#
# Try to find GBM library on UNIX systems. The following values are defined
#
# ::
#
#   GBM_FOUND         - True if gbm is available
#   GBM_INCLUDE_DIRS  - Include directories for gbm
#   GBM_LIBRARIES     - List of libraries for gbm
#   GBM_DEFINITIONS   - List of definitions for gbm
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

set_package_properties(GBM PROPERTIES
   URL "http://www.mesa3d.org/"
   DESCRIPTION "Generic buffer manager")

find_package(PkgConfig)
pkg_check_modules(PC_GBM QUIET gbm)
find_library(GBM_LIBRARIES NAMES gbm HINTS ${PC_GBM_LIBRARY_DIRS})
find_path(GBM_INCLUDE_DIRS gbm.h HINTS ${PC_GBM_INCLUDE_DIRS})

set(GBM_DEFINITIONS ${PC_GBM_CFLAGS_OTHER})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GBM DEFAULT_MSG GBM_INCLUDE_DIRS GBM_LIBRARIES)
mark_as_advanced(GBM_INCLUDE_DIRS GBM_LIBRARIES GBM_DEFINITIONS)
