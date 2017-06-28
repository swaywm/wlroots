cmake_minimum_required(VERSION 3.1.0)

project(wlroots C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wextra")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror")
if (CMAKE_COMPILER_IS_GNUCC)
    if(NOT CMAKE_C_COMPILER_VERSION VERSION_LESS 7.0)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wimplicit-fallthrough=0")
    endif()
endif()

list(INSERT CMAKE_MODULE_PATH 0
	${CMAKE_CURRENT_SOURCE_DIR}/CMake
)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.git)
	execute_process(
		COMMAND git describe --always --tags
		OUTPUT_VARIABLE GIT_COMMIT_HASH
		OUTPUT_STRIP_TRAILING_WHITESPACE
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	)
	execute_process(
		COMMAND git rev-parse --abbrev-ref HEAD
		OUTPUT_VARIABLE GIT_BRANCH
		OUTPUT_STRIP_TRAILING_WHITESPACE
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	)
endif()

add_definitions(-DWLR_GIT_VERSION=\"${GIT_COMMIT_HASH}\")
add_definitions(-DWLR_GIT_BRANCH=\"${GIT_BRANCH}\")
add_definitions(-DWLR_SRC_DIR=\"${CMAKE_SOURCE_DIR}\")

string(TIMESTAMP CURRENT_DATE "%Y-%m-%d" UTC)
add_definitions(-DWLR_VERSION_DATE=\"${CURRENT_DATE}\")

find_package(Wayland REQUIRED)
find_package(WaylandProtocols REQUIRED)
find_package(EGL REQUIRED)
find_package(GLESv2 REQUIRED)
find_package(DRM REQUIRED)
find_package(GBM REQUIRED)
find_package(LibInput REQUIRED)
find_package(XKBCommon REQUIRED)
find_package(Udev REQUIRED)
find_package(Systemd)

include(Wayland)
include(Manpage)

include_directories(include)

add_subdirectory(backend)
add_subdirectory(types)
add_subdirectory(session)
add_subdirectory(render)
add_subdirectory(util)

add_subdirectory(example)
