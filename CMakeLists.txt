cmake_minimum_required(VERSION 3.16)

if(APPLE)
  find_program(GCC_PATH gcc PATHS /opt/homebrew/bin /usr/local/bin NO_DEFAULT_PATH)
  find_program(GXX_PATH g++ PATHS /opt/homebrew/bin /usr/local/bin NO_DEFAULT_PATH)

  if(GCC_PATH AND GXX_PATH)
    set(CMAKE_C_COMPILER "${GCC_PATH}" CACHE STRING "C Compiler")
    set(CMAKE_CXX_COMPILER "${GXX_PATH}" CACHE STRING "C++ Compiler")
  endif()
endif()

project(gpac_plugin VERSION 0.2.1 LANGUAGES C) # x-release-please-version

# Options
option(ENABLE_TESTS "Enable and build tests" OFF)
option(ENABLE_COVERAGE "Enable coverage reporting (only in Debug mode)" OFF)

# Set default build type to Release
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (e.g., Debug, Release)" FORCE)
endif()

# Define allowed values for CMAKE_BUILD_TYPE
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release)

# clang-tidy
find_program(CLANG_TIDY NAMES clang-tidy)

if(CLANG_TIDY)
  message(STATUS "clang-tidy found: ${CLANG_TIDY}")
  set(CMAKE_C_CLANG_TIDY clang-tidy)
else()
  message(STATUS "clang-tidy not found, skipping analysis.")
endif()

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
set_target_properties(${PROJECT_NAME} PROPERTIES
  EXPORT_COMPILE_COMMANDS ON
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
target_compile_options(${PROJECT_NAME} PRIVATE
  -Wall -Wextra -Werror
  -Wcast-align
  -Wno-unused-parameter
  -Wno-unused-variable
  -Wno-missing-field-initializers
  -Wno-cast-function-type
)

if(${CMAKE_BUILD_TYPE} STREQUAL "Debug" OR ENABLE_COVERAGE)
  message(STATUS "Building in Debug mode")
  target_compile_options(${PROJECT_NAME} PRIVATE -g)
else()
  message(STATUS "Building in Release mode")
  target_compile_options(${PROJECT_NAME} PRIVATE -O3)
endif()

# GStreamer
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0>=1.24)
pkg_check_modules(GSTREAMER_BASE REQUIRED gstreamer-base-1.0>=1.24)
pkg_check_modules(GSTREAMER_VIDEO REQUIRED gstreamer-video-1.0>=1.24)

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${GSTREAMER_INCLUDE_DIRS} ${GSTREAMER_BASE_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} ${GSTREAMER_LIBRARIES} ${GSTREAMER_BASE_LIBRARIES} ${GSTREAMER_VIDEO_LIBRARIES})
target_link_directories(${PROJECT_NAME} PUBLIC ${GSTREAMER_LIBRARY_DIRS} ${GSTREAMER_BASE_LIBRARY_DIRS} ${GSTREAMER_VIDEO_LIBRARY_DIRS})

# GIO
pkg_check_modules(GIO REQUIRED gio-2.0)

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${GIO_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} ${GIO_LIBRARIES})
target_link_directories(${PROJECT_NAME} PUBLIC ${GIO_LIBRARY_DIRS})

# GPAC
pkg_check_modules(GPAC REQUIRED gpac>=2.4)

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${GPAC_INCLUDE_DIRS})
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

# Coverage
if(ENABLE_COVERAGE)
  target_compile_options(${PROJECT_NAME} PRIVATE -O0 --coverage)
  target_link_options(${PROJECT_NAME} PRIVATE --coverage)

  # Enable tests
  if(NOT ENABLE_TESTS)
    set(ENABLE_TESTS ON CACHE BOOL "Enable and build tests" FORCE)
    message(STATUS "ENABLE_TESTS was OFF, enabling it for coverage reporting")
  endif()

  # Check required executables
  find_program(LCOV_EXECUTABLE lcov)
  find_program(GENHTML_EXECUTABLE genhtml)

  if(NOT LCOV_EXECUTABLE OR NOT GENHTML_EXECUTABLE)
    message(WARNING "lcov and/or genhtml not found, coverage report target will not be created")
  else()
    # Add coverage report target
    add_custom_target(coverage_report ALL
      DEPENDS ${PROJECT_NAME} gstgpacplugin_test
      COMMAND ${CMAKE_COMMAND} -E env "GST_PLUGIN_PATH=${CMAKE_BINARY_DIR}/lib:$ENV{GST_PLUGIN_PATH}" ./tests/gstgpacplugin_test
      COMMAND ${CMAKE_COMMAND} -E make_directory coverage
      COMMAND lcov --capture --directory CMakeFiles/gpac_plugin.dir/src --output-file coverage/coverage.info --ignore-errors inconsistent,inconsistent
      COMMAND lcov --remove coverage/coverage.info /usr/* --output-file coverage/coverage.filtered.info
      COMMAND genhtml --flat coverage/coverage.filtered.info --output-directory ${CMAKE_SOURCE_DIR}/coverage
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      COMMENT "Running tests and generating coverage report"
      VERBATIM
    )
  endif()
endif()

# Tests
if(ENABLE_TESTS)
  add_subdirectory(tests)
endif()

# Try to get multiarch triple
execute_process(
  COMMAND dpkg-architecture -qDEB_HOST_MULTIARCH
  OUTPUT_VARIABLE DEB_MULTIARCH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Default to plain /usr/lib if detection fails
if(NOT DEB_MULTIARCH)
  set(DEB_MULTIARCH "unknown")
endif()

# Install to GStreamer plugin directory
install(TARGETS ${PROJECT_NAME}
  LIBRARY DESTINATION /usr/lib/${DEB_MULTIARCH}/gstreamer-1.0
)

# Package
set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME gst-gpac-plugin
  CACHE STRING "The resulting package name"
)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "GPAC GStreamer Plugin"
  CACHE STRING "A GStreamer plugin for GPAC" FORCE
)

set(CPACK_PACKAGE_VENDOR "GPAC Project")
set(CPACK_VERBATIM_VARIABLES YES)

set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})

set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Deniz Ugur <deniz@gpac.io>")

set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

set(CPACK_DEBIAN_PACKAGE_SECTION "libs")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

# Always include CPack for packaging functionality
include(CPack)

# Create a custom package target that applies packaging conditions
add_custom_target(release_package
  COMMAND ${CMAKE_COMMAND} -E echo "Building for packaging"
  COMMAND ${CMAKE_COMMAND} -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=OFF -S ${CMAKE_SOURCE_DIR} -B ${CMAKE_BINARY_DIR}
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target all
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target package
  COMMENT "Building with packaging conditions (Release mode, no tests) and creating package"
  VERBATIM
)