cmake_minimum_required(VERSION 3.9 FATAL_ERROR)

set(version 0.0.1)

project(
   cppsocket
   VERSION "${version}"
   DESCRIPTION "Go-esque way of listening for and handling of connections."
)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(
   cppsocket SHARED
   src/cppsocket.cpp
)

set_target_properties(
   cppsocket PROPERTIES
   VERSION "${version}"
   PUBLIC_HEADER include/*.h
)
target_include_directories(cppsocket PRIVATE include src)

install(
   TARGETS cppsocket
   LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
   PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

add_subdirectory(tests)
