cmake_minimum_required(VERSION 3.6)
project(
  sps
  VERSION 3.3
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)

include(CheckIncludeFile)


# Find spank header
check_include_file(slurm/spank.h HAVE_SPANK_HEADER)

if (HAVE_SPANK_HEADER)
  add_library(launch_sps MODULE launch_sps.c)
  set_target_properties(launch_sps PROPERTIES PREFIX "")
  install(TARGETS launch_sps DESTINATION lib/slurm)
endif()

add_executable(sps sps.cpp)
target_link_libraries(sps stdc++fs)
install(TARGETS sps DESTINATION bin)
