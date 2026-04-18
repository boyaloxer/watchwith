cmake_minimum_required(VERSION 3.28...3.30)

if(NOT ENABLE_FRONTEND)
  return()
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets)

if(NOT TARGET OBS::libobs)
  add_subdirectory("${CMAKE_SOURCE_DIR}/libobs" libobs)
endif()

# libdatachannel for WebRTC
find_package(MbedTLS REQUIRED)
if(NOT TARGET MbedTLS::MbedTLS)
  add_library(MbedTLS::MbedTLS INTERFACE IMPORTED)
  set_target_properties(MbedTLS::MbedTLS PROPERTIES
    INTERFACE_LINK_LIBRARIES "MbedTLS::mbedtls;MbedTLS::mbedx509;MbedTLS::mbedcrypto"
    INTERFACE_INCLUDE_DIRECTORIES "$<TARGET_PROPERTY:MbedTLS::mbedtls,INTERFACE_INCLUDE_DIRECTORIES>"
  )
endif()

# Bridge old-style variables for libsrtp's find_package(MbedTLS)
get_target_property(_mbedtls_inc MbedTLS::mbedtls INTERFACE_INCLUDE_DIRECTORIES)
set(MBEDTLS_INCLUDE_DIRS "${_mbedtls_inc}" CACHE STRING "" FORCE)
set(MBEDTLS_LIBRARIES "MbedTLS::mbedtls;MbedTLS::mbedx509;MbedTLS::mbedcrypto" CACHE STRING "" FORCE)

set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
set(NO_WEBSOCKET ON CACHE BOOL "" FORCE)
set(USE_MBEDTLS ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

# Disable warnings-as-errors for third-party code
set(_saved_c_flags "${CMAKE_C_FLAGS}")
set(_saved_cxx_flags "${CMAKE_CXX_FLAGS}")
string(REPLACE "/WX" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
string(REPLACE "/WX" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W3 /wd4319 /wd4267 /wd4244")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3 /wd4319 /wd4267 /wd4244")

add_subdirectory(deps/libdatachannel EXCLUDE_FROM_ALL)

# Restore flags for our own code
set(CMAKE_C_FLAGS "${_saved_c_flags}")
set(CMAKE_CXX_FLAGS "${_saved_cxx_flags}")

add_executable(watchwith)
add_executable(OBS::watchwith ALIAS watchwith)

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
        DL_${graphics_library_U}="$<$<IF:$<PLATFORM_ID:Windows>,TARGET_FILE_NAME,TARGET_SONAME_FILE_NAME>:OBS::libobs-${graphics_library}>"
    )
  else()
    target_compile_definitions(watchwith PRIVATE DL_${graphics_library_U}="")
  endif()
endforeach()

if(OS_WINDOWS)
  set_target_properties(watchwith PROPERTIES WIN32_EXECUTABLE TRUE)
  target_link_libraries(watchwith PRIVATE dwmapi)
endif()

set_target_properties_obs(watchwith PROPERTIES FOLDER watchwith OUTPUT_NAME watchwith)
