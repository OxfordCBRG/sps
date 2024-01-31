cmake_minimum_required(VERSION 3.6)
project(
  sps
  VERSION 3.3
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(POSITION_INDEPENDENT_CODE ON)

include(CheckIncludeFile)
include(CheckLanguage)
include(GNUInstallDirs)

check_language(CUDA)
if(CMAKE_CUDA_COMPILER)
  enable_language(CUDA)
  find_package(CUDAToolkit)
endif()

find_package(Boost REQUIRED COMPONENTS program_options)

# Find spank header
check_include_file(slurm/spank.h HAVE_SPANK_HEADER)

if (HAVE_SPANK_HEADER)
  add_library(launch_sps MODULE launch_sps.c)
  set_target_properties(launch_sps PROPERTIES PREFIX "")
  install(TARGETS launch_sps DESTINATION ${CMAKE_INSTALL_LIBDIR}/slurm)
endif()

add_executable(sps sps.cpp)
target_link_libraries(sps PRIVATE stdc++fs Boost::program_options)

if(CMAKE_CUDA_COMPILER)
  set(CUDA_SEPARABLE_COMPILATION ON)
  target_link_libraries(sps PRIVATE CUDA::nvml)
  target_compile_definitions(sps PRIVATE HAVE_NVML)
  target_include_directories(sps PRIVATE ${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES})
endif()


install(TARGETS sps DESTINATION bin)
install(PROGRAMS ckill sps-pyplot sps-stop DESTINATION bin)
