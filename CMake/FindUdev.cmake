#.rst:
# FindUdev
# -------
#
# Find udev library
#
# Try to find udev library on UNIX systems. The following values are defined
#
# ::
#
#   UDEV_FOUND         - True if udev is available
#   UDEV_INCLUDE_DIRS  - Include directories for udev
#   UDEV_LIBRARIES     - List of libraries for udev
#   UDEV_DEFINITIONS   - List of definitions for udev
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
set_package_properties(Udev PROPERTIES
   URL "https://www.kernel.org/pub/linux/utils/kernel/hotplug/udev/udev.html"
   DESCRIPTION "Device manager for the Linux kernel")

find_package(PkgConfig)
pkg_check_modules(PC_UDEV QUIET libudev)
find_library(UDEV_LIBRARIES NAMES udev HINTS ${PC_UDEV_LIBRARY_DIRS})
find_path(UDEV_INCLUDE_DIRS libudev.h HINTS ${PC_UDEV_INCLUDE_DIRS})

set(UDEV_DEFINITIONS ${PC_UDEV_CFLAGS_OTHER})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(UDEV DEFAULT_MSG UDEV_INCLUDE_DIRS UDEV_LIBRARIES)
mark_as_advanced(UDEV_INCLUDE_DIRS UDEV_LIBRARIES UDEV_DEFINITIONS)
