# Install script for directory: /home/home/goodsol/workspace/QCON/webrtc/libwebsockets/lib

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/home/home/goodsol/workspace/QCON/webrtc/libwebsockets/install")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xcorex" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/home/goodsol/workspace/QCON/webrtc/libwebsockets/out/lib/libwebsockets.a")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig" TYPE FILE FILES "/home/home/goodsol/workspace/QCON/webrtc/libwebsockets/out/libwebsockets.pc")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig" TYPE FILE FILES "/home/home/goodsol/workspace/QCON/webrtc/libwebsockets/out/libwebsockets_static.pc")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("/home/home/goodsol/workspace/QCON/webrtc/libwebsockets/out/lib/plat/unix/cmake_install.cmake")
  include("/home/home/goodsol/workspace/QCON/webrtc/libwebsockets/out/lib/tls/cmake_install.cmake")
  include("/home/home/goodsol/workspace/QCON/webrtc/libwebsockets/out/lib/core/cmake_install.cmake")
  include("/home/home/goodsol/workspace/QCON/webrtc/libwebsockets/out/lib/misc/cmake_install.cmake")
  include("/home/home/goodsol/workspace/QCON/webrtc/libwebsockets/out/lib/system/cmake_install.cmake")
  include("/home/home/goodsol/workspace/QCON/webrtc/libwebsockets/out/lib/core-net/cmake_install.cmake")
  include("/home/home/goodsol/workspace/QCON/webrtc/libwebsockets/out/lib/roles/cmake_install.cmake")
  include("/home/home/goodsol/workspace/QCON/webrtc/libwebsockets/out/lib/event-libs/cmake_install.cmake")

endif()

