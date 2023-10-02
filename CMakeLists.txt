cmake_minimum_required(VERSION 3.6)
project(
  sps
  VERSION 3.3)

include(CheckIncludeFile)


# Find spank header
check_include_file(slurm/spank.h HAVE_SPANK_HEADER)

add_library(launch_sps MODULE launch_sps.c)
set_target_properties(launch_sps PROPERTIES PREFIX "")

add_executable(sps sps.cpp)

install(TARGETS launch_sps DESTINATION lib/slurm)
install(TARGETS sps DESTINATION bin)
