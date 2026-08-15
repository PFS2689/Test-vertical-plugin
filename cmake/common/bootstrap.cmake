# Plugin bootstrap module

include_guard(GLOBAL)

# Map fallback configurations for optimized build configurations
# gersemi: off
set(
  CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO
    RelWithDebInfo
    Release
    MinSizeRel
    None
    ""
)
set(
  CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL
    MinSizeRel
    Release
    RelWithDebInfo
    None
    ""
)
set(
  CMAKE_MAP_IMPORTED_CONFIG_RELEASE
    Release
    RelWithDebInfo
    MinSizeRel
    None
    ""
)
# gersemi: on

# Prohibit in-source builds
if("${CMAKE_CURRENT_BINARY_DIR}" STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")
  message(
    FATAL_ERROR
    "In-source builds are not supported. "
    "Specify a build directory via 'cmake -S <SOURCE DIRECTORY> -B <BUILD_DIRECTORY>' instead."
  )
  file(REMOVE_RECURSE "${CMAKE_CURRENT_SOURCE_DIR}/CMakeCache.txt" "${CMAKE_CURRENT_SOURCE_DIR}/CMakeFiles")
endif()

# Add common module directories to default search path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/common")

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/buildspec.json" buildspec)

string(JSON _name GET ${buildspec} name)
string(JSON _website GET ${buildspec} website)
string(JSON _author GET ${buildspec} author)
string(JSON _email GET ${buildspec} email)
string(JSON _version GET ${buildspec} version)
string(JSON _bundleId GET ${buildspec} platformConfig macos bundleId)

set(PLUGIN_AUTHOR ${_author})
set(PLUGIN_WEBSITE ${_website})
set(PLUGIN_EMAIL ${_email})
set(PLUGIN_VERSION ${_version})
set(MACOS_BUNDLEID ${_bundleId})

# Fresh UTC stamp at configure time — never hard-code a release "Last Updated" date.
# Override from CI with -DPLUGIN_BUILD_TIMESTAMP=... when needed.
if(NOT PLUGIN_BUILD_TIMESTAMP)
  string(TIMESTAMP PLUGIN_BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%SZ" UTC)
endif()

# Human-visible Build ID for proving OBS loaded this exact binary (not product version).
# Override with -DPLUGIN_BUILD_ID=... for a named verification build.
if(NOT PLUGIN_BUILD_ID)
  string(TIMESTAMP _build_id_date "%Y-%m-%d" UTC)
  if(DEFINED ENV{GITHUB_RUN_NUMBER} AND NOT "$ENV{GITHUB_RUN_NUMBER}" STREQUAL "")
    set(PLUGIN_BUILD_ID "${_build_id_date}-TEST-$ENV{GITHUB_RUN_NUMBER}")
  else()
    set(PLUGIN_BUILD_ID "${_build_id_date}-TEST-001")
  endif()
  unset(_build_id_date)
endif()
message(STATUS "Vertical Shorts Plugin Build ID: ${PLUGIN_BUILD_ID}")

string(REPLACE "." ";" _version_canonical "${_version}")
list(GET _version_canonical 0 PLUGIN_VERSION_MAJOR)
list(GET _version_canonical 1 PLUGIN_VERSION_MINOR)
list(GET _version_canonical 2 PLUGIN_VERSION_PATCH)
unset(_version_canonical)

include(buildnumber)
# PE VERSIONINFO fields are 16-bit; clamp build number for FileVersion fourth component.
# ProductVersion stays PLUGIN_VERSION (e.g. 1.0.5) — public version unchanged.
if(PLUGIN_BUILD_NUMBER MATCHES "^[0-9]+$")
  math(EXPR PLUGIN_FILE_VERSION_BUILD "${PLUGIN_BUILD_NUMBER} % 65535")
else()
  set(PLUGIN_FILE_VERSION_BUILD 1)
endif()
if(PLUGIN_FILE_VERSION_BUILD EQUAL 0)
  set(PLUGIN_FILE_VERSION_BUILD 1)
endif()
include(osconfig)

# Allow selection of common build types via UI
if(NOT CMAKE_GENERATOR MATCHES "(Xcode|Visual Studio .+)")
  if(NOT CMAKE_BUILD_TYPE)
    set(
      CMAKE_BUILD_TYPE
      "RelWithDebInfo"
      CACHE STRING
      "OBS build type [Release, RelWithDebInfo, Debug, MinSizeRel]"
      FORCE
    )
    set_property(
      CACHE CMAKE_BUILD_TYPE
      PROPERTY STRINGS Release RelWithDebInfo Debug MinSizeRel
    )
  endif()
endif()

# Disable exports automatically going into the CMake package registry
set(CMAKE_EXPORT_PACKAGE_REGISTRY FALSE)
# Enable default inclusion of targets' source and binary directory
set(CMAKE_INCLUDE_CURRENT_DIR TRUE)
