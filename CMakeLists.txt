cmake_minimum_required(VERSION 3.6)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

set(PROJECT_NAME sps)
project(
  ${PROJECT_NAME}
  LANGUAGES C CXX)

option(VERSION_UPDATE_FROM_GIT "get version from git tag" on)
find_package(Git)
include(GetVersionFromGitTag)

option(sps-docs "build SPS documentation" ON)

set(CMAKE_CXX_STANDARD 17)
set(POSITION_INDEPENDENT_CODE ON)

include(CheckIncludeFile)
include(CheckLanguage)
include(GNUInstallDirs)

# Documentation install directory (default to cmake project docdir)
set(SPS_DOCDIR "${CMAKE_INSTALL_DOCDIR}")

if(sps-docs)
  add_subdirectory(docs)
endif()


check_language(CUDA)
if(CMAKE_CUDA_COMPILER)
  enable_language(CUDA)
  find_package(CUDAToolkit)
endif()

add_definitions(-DBOOST_LOG_DYN_LINK)

find_package(Boost REQUIRED COMPONENTS program_options log log_setup)
find_package(rocm_smi)

# enable warnings when debug mode
if (MSVC)
  # warning level 4
  add_compile_options("$<$<CONFIG:Debug>:/W4>")
else()
  # additional warnings
  add_compile_options("$<$<CONFIG:Debug>:-Wall;-Wextra;-Wpedantic>")
endif()

# Find spank header
check_include_file(slurm/spank.h HAVE_SPANK_HEADER)

if (HAVE_SPANK_HEADER)
  add_library(launch_sps MODULE launch_sps.c)
  set_target_properties(launch_sps PROPERTIES PREFIX "")
  install(TARGETS launch_sps DESTINATION ${CMAKE_INSTALL_LIBDIR}/slurm)
endif()

add_executable(sps sps.cpp)
target_link_libraries(sps PRIVATE stdc++fs Boost::program_options Boost::log Boost::log_setup)

if(CMAKE_CUDA_COMPILER)
  set(CUDA_SEPARABLE_COMPILATION ON)
  target_link_libraries(sps PRIVATE CUDA::nvml)
  target_compile_definitions(sps PRIVATE HAVE_NVML)
  target_include_directories(sps PRIVATE ${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES})
endif()

if(ROCM_SMI_INCLUDE_DIRS)
  target_compile_definitions(sps PRIVATE HAVE_RSMI)
  target_include_directories(sps PRIVATE ${ROCM_SMI_INCLUDE_DIRS})
  target_link_libraries(sps PRIVATE ${ROCM_SMI_LIBRARIES})
endif()

install(TARGETS sps DESTINATION bin)
install(PROGRAMS ckill sps-pyplot sps-stop DESTINATION bin)
