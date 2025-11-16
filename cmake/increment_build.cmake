# Increment build number and write generated header
# Paths
set(BUILD_NUM_FILE "${CMAKE_SOURCE_DIR}/build_number.txt")
set(OUT_DIR "${CMAKE_BINARY_DIR}/generated")
set(OUT_HEADER "${OUT_DIR}/build_info.h")

# Read current build number
if(NOT EXISTS "${BUILD_NUM_FILE}")
  file(WRITE "${BUILD_NUM_FILE}" "0\n")
endif()
file(READ "${BUILD_NUM_FILE}" BUILD_NUM_CONTENT)
string(STRIP "${BUILD_NUM_CONTENT}" BUILD_NUM_CONTENT)
if(BUILD_NUM_CONTENT STREQUAL "")
  set(BUILD_NUM 0)
else()
  string(REGEX MATCH "[0-9]+" BUILD_NUM_MATCH "${BUILD_NUM_CONTENT}")
  if(BUILD_NUM_MATCH)
    set(BUILD_NUM ${BUILD_NUM_MATCH})
  else()
    set(BUILD_NUM 0)
  endif()
endif()

math(EXPR NEW_BUILD_NUM "${BUILD_NUM} + 1")

# Overwrite build_number.txt with the new number (persisting across builds)
file(WRITE "${BUILD_NUM_FILE}" "${NEW_BUILD_NUM}\n")

# Ensure output dir exists
file(MAKE_DIRECTORY "${OUT_DIR}")

# Plugin version string - prefer PLUGIN_VERSION passed from CMake, fallback to hard-coded string
if(DEFINED PLUGIN_VERSION)
  # Ensure it starts with 'v' if the project version is numeric
  string(REGEX MATCH "^v" _has_v "${PLUGIN_VERSION}")
  if(_has_v)
    set(PLUGIN_VERSION_STRING "${PLUGIN_VERSION}")
  else()
    set(PLUGIN_VERSION_STRING "v${PLUGIN_VERSION}")
  endif()
else()
  set(PLUGIN_VERSION_STRING "v3.0.0")
endif()

# Compose version with build string
string(CONCAT PLUGIN_VERSION_WITH_BUILD "${PLUGIN_VERSION_STRING} - build " "${NEW_BUILD_NUM}")

# Write generated header
file(WRITE "${OUT_HEADER}" "#pragma once\n")
file(APPEND "${OUT_HEADER}" "#define PLUGIN_VERSION \"${PLUGIN_VERSION_STRING}\"\n")
file(APPEND "${OUT_HEADER}" "#define PLUGIN_BUILD_NUMBER ${NEW_BUILD_NUM}\n")
file(APPEND "${OUT_HEADER}" "#define PLUGIN_VERSION_WITH_BUILD \"${PLUGIN_VERSION_WITH_BUILD}\"\n")

message(STATUS "[increment_build] updated build number to ${NEW_BUILD_NUM}")
