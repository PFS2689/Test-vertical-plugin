cmake_minimum_required(VERSION 3.16...3.30)

project(obs-shorts-vertical VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

option(BUILD_OUT_OF_TREE "Build out-of-tree (standalone plugin)" ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# --- Dependencies -----------------------------------------------------------
find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui)

if(UNIX AND NOT APPLE)
  if(Qt6_VERSION VERSION_LESS "6.9.0")
    find_package(Qt6 COMPONENTS GuiPrivate)
  endif()
endif()

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/version.h"
)

add_library(${PROJECT_NAME} MODULE
  src/plugin-main.cpp
  src/shorts-dock.cpp
  src/shorts-dock.hpp
  src/qt-display.cpp
  src/qt-display.hpp
  src/display-helpers.hpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
  OBS::libobs
  OBS::obs-frontend-api
  Qt6::Core
  Qt6::Widgets
  Qt6::Gui
)

if(TARGET Qt6::GuiPrivate)
  target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::GuiPrivate)
endif()

# Older Qt 6 needs private Gui headers for Wayland native surfaces
if(UNIX AND NOT APPLE AND Qt6_VERSION VERSION_LESS "6.9.0")
  if(NOT TARGET Qt6::GuiPrivate)
    find_package(Qt6 COMPONENTS GuiPrivate)
    if(TARGET Qt6::GuiPrivate)
      target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::GuiPrivate)
    endif()
  endif()
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE
  PLUGIN_NAME="${PROJECT_NAME}"
  PLUGIN_VERSION="${PROJECT_VERSION}"
)

if(MSVC)
  target_compile_options(${PROJECT_NAME} PRIVATE /W3 /wd4996)
else()
  target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
  PREFIX ""
  OUTPUT_NAME "obs-shorts-vertical"
)

# --- Install ----------------------------------------------------------------
if(WIN32)
  install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION "obs-plugins/64bit"
    RUNTIME DESTINATION "obs-plugins/64bit")
  install(DIRECTORY data/ DESTINATION "data/obs-plugins/obs-shorts-vertical")
elseif(APPLE)
  install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION "obs-plugins")
  install(DIRECTORY data/ DESTINATION "data/obs-plugins/obs-shorts-vertical")
else()
  include(GNUInstallDirs)
  # Typical Linux OBS plugin layout
  set(_OBS_PLUGIN_DEST "${CMAKE_INSTALL_LIBDIR}/obs-plugins")
  set(_OBS_DATA_DEST "${CMAKE_INSTALL_DATAROOTDIR}/obs/obs-plugins/obs-shorts-vertical")
  install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION "${_OBS_PLUGIN_DEST}")
  install(DIRECTORY data/ DESTINATION "${_OBS_DATA_DEST}")
endif()

message(STATUS "obs-shorts-vertical ${PROJECT_VERSION} configured")
