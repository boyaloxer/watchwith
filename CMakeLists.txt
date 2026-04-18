cmake_minimum_required(VERSION 3.28...3.30)

project(WatchWith VERSION 0.1.0)

# OBS_BUILDDIR must be set by the build script (points to the OBS build tree)
if(NOT DEFINED OBS_BUILDDIR)
  message(FATAL_ERROR "OBS_BUILDDIR not set. Use scripts/build-windows.ps1 to build.")
endif()

# OBS source directory (submodule)
set(OBS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/deps/obs-studio")

# Only add OBS's common cmake helpers (not finders - they conflict with config-mode packages)
list(APPEND CMAKE_MODULE_PATH "${OBS_SRC}/cmake/common")

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Import pre-built OBS targets
if(NOT TARGET OBS::libobs)
  add_library(OBS::libobs SHARED IMPORTED)
  set_target_properties(OBS::libobs PROPERTIES
    IMPORTED_LOCATION "${OBS_BUILDDIR}/rundir/RelWithDebInfo/bin/64bit/obs.dll"
    IMPORTED_IMPLIB "${OBS_BUILDDIR}/libobs/RelWithDebInfo/obs.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${OBS_SRC}/libobs;${OBS_BUILDDIR}/config"
  )
endif()

# Graphics backend libraries
foreach(_gfx d3d11 opengl)
  if(EXISTS "${OBS_BUILDDIR}/libobs-${_gfx}/RelWithDebInfo/libobs-${_gfx}.dll")
    add_library(OBS::libobs-${_gfx} SHARED IMPORTED)
    set_target_properties(OBS::libobs-${_gfx} PROPERTIES
      IMPORTED_LOCATION "${OBS_BUILDDIR}/rundir/RelWithDebInfo/bin/64bit/libobs-${_gfx}.dll"
      IMPORTED_IMPLIB "${OBS_BUILDDIR}/libobs-${_gfx}/RelWithDebInfo/libobs-${_gfx}.lib"
    )
  endif()
endforeach()

# libdatachannel for WebRTC
find_package(MbedTLS CONFIG REQUIRED)

# Create unified alias target for libdatachannel
if(NOT TARGET MbedTLS::MbedTLS)
  add_library(MbedTLS::MbedTLS INTERFACE IMPORTED)
  set_target_properties(MbedTLS::MbedTLS PROPERTIES
    INTERFACE_LINK_LIBRARIES "MbedTLS::mbedtls;MbedTLS::mbedx509;MbedTLS::mbedcrypto"
    INTERFACE_INCLUDE_DIRECTORIES "$<TARGET_PROPERTY:MbedTLS::mbedtls,INTERFACE_INCLUDE_DIRECTORIES>"
  )
endif()

# Some builds expect CamelCase target names
foreach(_alias MbedTls MbedX509 MbedCrypto)
  string(TOLOWER "${_alias}" _lower)
  if(NOT TARGET MbedTLS::${_alias} AND TARGET MbedTLS::${_lower})
    add_library(MbedTLS::${_alias} ALIAS MbedTLS::${_lower})
  endif()
endforeach()

get_target_property(_mbedtls_inc MbedTLS::mbedtls INTERFACE_INCLUDE_DIRECTORIES)
set(MBEDTLS_INCLUDE_DIRS "${_mbedtls_inc}" CACHE STRING "" FORCE)
set(MBEDTLS_LIBRARIES "MbedTLS::mbedtls;MbedTLS::mbedx509;MbedTLS::mbedcrypto" CACHE STRING "" FORCE)

set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
set(NO_WEBSOCKET ON CACHE BOOL "" FORCE)
set(USE_MBEDTLS ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

if(MSVC)
  set(_saved_c "${CMAKE_C_FLAGS}")
  set(_saved_cxx "${CMAKE_CXX_FLAGS}")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W3 /wd4319 /wd4267 /wd4244")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3 /wd4319 /wd4267 /wd4244")
endif()

add_subdirectory(deps/libdatachannel EXCLUDE_FROM_ALL)

if(MSVC)
  set(CMAKE_C_FLAGS "${_saved_c}")
  set(CMAKE_CXX_FLAGS "${_saved_cxx}")
endif()

# WatchWith application
add_executable(watchwith)

set_target_properties(
  watchwith
  PROPERTIES AUTOMOC ON
             AUTOUIC ON
             AUTORCC ON
)

target_sources(
  watchwith
  PRIVATE
    src/main.cpp
    src/app.h
    src/app.cpp
    src/ui/main-window.h
    src/ui/main-window.cpp
    src/ui/canvas-view.h
    src/ui/canvas-view.cpp
    src/ui/session-dialog.h
    src/ui/session-dialog.cpp
    src/net/session.h
    src/net/session.cpp
    src/net/peer-connection.h
    src/net/peer-connection.cpp
    src/net/media-bridge.h
    src/net/media-bridge.cpp
)

target_include_directories(watchwith PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(
  watchwith
  PRIVATE
    OBS::libobs
    Qt6::Widgets
    LibDataChannel::LibDataChannelStatic
)

foreach(graphics_library IN ITEMS opengl metal d3d11)
  string(TOUPPER ${graphics_library} graphics_library_U)
  if(TARGET OBS::libobs-${graphics_library})
    target_compile_definitions(
      watchwith
      PRIVATE
        DL_${graphics_library_U}="$<TARGET_FILE_NAME:OBS::libobs-${graphics_library}>"
    )
  else()
    target_compile_definitions(watchwith PRIVATE DL_${graphics_library_U}="")
  endif()
endforeach()

if(WIN32)
  set_target_properties(watchwith PROPERTIES WIN32_EXECUTABLE TRUE)
  target_link_libraries(watchwith PRIVATE dwmapi)
endif()

# Post-build: copy watchwith.exe into the OBS rundir so it can find plugins/DLLs
add_custom_command(
  TARGET watchwith POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:watchwith>"
    "${OBS_BUILDDIR}/rundir/RelWithDebInfo/bin/64bit/watchwith.exe"
  COMMENT "Copying watchwith to OBS rundir"
)
