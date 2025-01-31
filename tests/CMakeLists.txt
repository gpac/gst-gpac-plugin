# Configure test project
project(gstgpacplugin_test LANGUAGES CXX)
enable_testing()

# GoogleTest requires at least C++14
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Avoid warning about DOWNLOAD_EXTRACT_TIMESTAMP in CMake 3.24:
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
  cmake_policy(SET CMP0135 NEW)
endif()

# Fetch GoogleTest
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG 2b6b042a77446ff322cd7522ca068d9f2a21c1d1
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Sources
file(GLOB_RECURSE SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
)

# Test executable
add_executable(${PROJECT_NAME} ${SOURCES})
target_compile_options(${PROJECT_NAME} PRIVATE
  -g
  -Wall -Wextra -Werror
  -Wcast-align
  -Wno-unused-parameter
  -Wno-unused-variable
  -Wno-missing-field-initializers
  -Wno-cast-function-type
)

# Link libraries
include_directories(${GSTREAMER_INCLUDE_DIRS} ${GSTREAMER_BASE_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} GTest::gtest_main ${GSTREAMER_LIBRARIES} ${GSTREAMER_BASE_LIBRARIES})

# Discover tests
include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME})