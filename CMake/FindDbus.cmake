#.rst:
# FindDbus
# -----------
#
# Find Dbus library
#
# Try to find Dbus library on UNIX systems. The following values are defined
#
# ::
#
#   DBUS_FOUND         - True if dbus is available
#   DBUS_INCLUDE_DIRS  - Include directories for dbus
#   DBUS_LIBRARIES     - List of libraries for dbus
#   DBUS_DEFINITIONS   - List of definitions for dbus
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
set_package_properties(Dbus PROPERTIES
   URL "http://www.freedesktop.org/wiki/Software/dbus/"
   DESCRIPTION "Message bus system")

find_package(PkgConfig)
pkg_check_modules(PC_DBUS QUIET dbus-1)
find_path(DBUS_SYSTEM_INCLUDES dbus/dbus.h PATH_SUFFIXES dbus-1.0)
find_path(DBUS_LIB_INCLUDES dbus/dbus-arch-deps.h HINTS ${PC_DBUS_INCLUDE_DIRS} ${CMAKE_LIBRARY_PATH}/dbus-1.0/include ${CMAKE_SYSTEM_LIBRARY_PATH}/dbus-1.0/include)
find_library(DBUS_LIBRARIES NAMES dbus-1 HINTS ${PC_DBUS_LIBRARY_DIRS})

set(DBUS_INCLUDE_DIRS ${DBUS_SYSTEM_INCLUDES} ${DBUS_LIB_INCLUDES})
set(DBUS_DEFINITIONS ${PC_DBUS_CFLAGS_OTHER})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(DBUS DEFAULT_MSG DBUS_INCLUDE_DIRS DBUS_LIBRARIES)
mark_as_advanced(DBUS_INCLUDE_DIRS DBUS_LIBRARIES DBUS_SYSTEM_INCLUDES DBUS_LIB_INCLUDES DBUS_DEFINITIONS)
