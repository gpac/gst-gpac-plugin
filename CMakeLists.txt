cmake_minimum_required(VERSION 3.16)

if(APPLE)
  find_program(GCC_PATH gcc PATHS /opt/homebrew/bin /usr/local/bin NO_DEFAULT_PATH)
  find_program(GXX_PATH g++ PATHS /opt/homebrew/bin /usr/local/bin NO_DEFAULT_PATH)

  if(GCC_PATH AND GXX_PATH)
    set(CMAKE_C_COMPILER "${GCC_PATH}" CACHE STRING "C Compiler")
    set(CMAKE_CXX_COMPILER "${GXX_PATH}" CACHE STRING "C++ Compiler")
  endif()
endif()

project(gpac_plugin VERSION 0.1.1 LANGUAGES C) # x-release-please-version

# Options
option(ENABLE_TESTS "Enable and build tests" OFF)

# Set default build type to Release
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (e.g., Debug, Release)" FORCE)
endif()

# Define allowed values for CMAKE_BUILD_TYPE
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release)

# Source
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
file(GLOB_RECURSE SOURCES

  # Elements
  ${CMAKE_CURRENT_SOURCE_DIR}/src/elements/*.[ch]
  ${CMAKE_CURRENT_SOURCE_DIR}/src/elements/**/*.[ch]

  # Library
  ${CMAKE_CURRENT_SOURCE_DIR}/src/lib/*.[ch]
  ${CMAKE_CURRENT_SOURCE_DIR}/src/lib/**/*.[ch]

  # Plugin
  ${CMAKE_CURRENT_SOURCE_DIR}/src/gstgpacplugin.c
)

add_library(${PROJECT_NAME} SHARED ${SOURCES})
target_compile_options(${PROJECT_NAME} PRIVATE
  -Wall -Wextra -Werror
  -Wcast-align
  -Wno-unused-parameter
  -Wno-unused-variable
  -Wno-missing-field-initializers
  -Wno-cast-function-type
)

if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
  message(STATUS "Building in Debug mode")
  target_compile_options(${PROJECT_NAME} PRIVATE -g)
else()
  message(STATUS "Building in Release mode")
  target_compile_options(${PROJECT_NAME} PRIVATE -O3)
endif()

# GStreamer
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GSTREAMER_BASE REQUIRED gstreamer-base-1.0)

include_directories(${GSTREAMER_INCLUDE_DIRS} ${GSTREAMER_BASE_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} ${GSTREAMER_LIBRARIES} ${GSTREAMER_BASE_LIBRARIES})
target_link_directories(${PROJECT_NAME} PUBLIC ${GSTREAMER_LIBRARY_DIRS} ${GSTREAMER_BASE_LIBRARY_DIRS})

# GPAC
pkg_check_modules(GPAC REQUIRED gpac)

include_directories(${GPAC_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} ${GPAC_LIBRARIES})
target_link_directories(${PROJECT_NAME} PUBLIC ${GPAC_LIBRARY_DIRS})

# Create a symlink to gst-launch-1.0
find_program(GST_LAUNCH_EXECUTABLE NAMES gst-launch-1.0)

# Check if gst-launch-1.0 was found
if(GST_LAUNCH_EXECUTABLE)
  message(STATUS "Found gst-launch-1.0 at: ${GST_LAUNCH_EXECUTABLE}")

  # Create a symbolic link in the build directory
  add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/gst-launch
    COMMAND ${CMAKE_COMMAND} -E create_symlink ${GST_LAUNCH_EXECUTABLE} ${CMAKE_BINARY_DIR}/gst-launch
    COMMENT "Creating symbolic link to gst-launch-1.0"
    VERBATIM
  )

  # Define a custom target that uses this command
  add_custom_target(create_gst_link ALL DEPENDS ${CMAKE_BINARY_DIR}/gst-launch)
endif()

# Tests
if(ENABLE_TESTS)
  add_subdirectory(tests)
endif()