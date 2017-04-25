#.rst:
# FindWaylandProtocols
# -------
#
# Find wayland protocol description files
#
# Try to find wayland protocol files. The following values are defined
#
# ::
#
#   WAYLANDPROTOCOLS_FOUND         - True if wayland protocol files are available
#   WAYLANDPROTOCOLS_PATH          - Path to wayland protocol files
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
set_package_properties(WaylandProtocols PROPERTIES
   URL "https://cgit.freedesktop.org/wayland/wayland-protocols"
   DESCRIPTION "Wayland protocol development")

unset(WAYLANDPROTOCOLS_PATH)

find_package(PkgConfig)
if (PKG_CONFIG_FOUND)
   execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=pkgdatadir wayland-protocols
       OUTPUT_VARIABLE WAYLANDPROTOCOLS_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
endif ()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(WaylandProtocols DEFAULT_MSG WAYLANDPROTOCOLS_PATH)
mark_as_advanced(WAYLANDPROTOCOLS_PATH)
