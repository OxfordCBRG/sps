cmake_minimum_required(VERSION 3.6)
project(
  sps
  VERSION 3.3
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)

include(CheckIncludeFile)
include(CheckLanguage)

check_language(CUDA)
if(CMAKE_CUDA_COMPILER)
  enable_language(CUDA)
  find_package(CUDAToolkit)
endif()

# Find spank header
check_include_file(slurm/spank.h HAVE_SPANK_HEADER)

if (HAVE_SPANK_HEADER)
  add_library(launch_sps MODULE launch_sps.c)
  set_target_properties(launch_sps PROPERTIES PREFIX "")
  install(TARGETS launch_sps DESTINATION lib/slurm)
endif()

add_executable(sps sps.cpp)
target_link_libraries(sps PRIVATE stdc++fs)
if(CMAKE_CUDA_COMPILER)
  target_link_libraries(sps PRIVATE CUDA::nvml)
  target_compile_definitions(sps PRIVATE HAVE_NVML)
  target_include_directories(sps PRIVATE ${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES})
endif()


install(TARGETS sps DESTINATION bin)
