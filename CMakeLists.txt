cmake_minimum_required(VERSION 3.24)
project(libcu VERSION 2.0.0 LANGUAGES CXX CUDA)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(LIBCU_BUILD_FILEUTILS "Build libcu.fileutils and the d* shell tools" ON)
option(LIBCU_BUILD_TESTS "Build the test runner (needs a GPU to run)" ${PROJECT_IS_TOP_LEVEL})
option(LIBCU_BUILD_JIMTCL "Build the jimtcl 0.77 interpreter device port" OFF)
option(LIBCU_BUILD_JIMTCL78 "Build the jimtcl 0.78 interpreter device port" OFF)
option(LIBCU_BUILD_TINYTCL "Build the tinytcl interpreter device port" OFF)

# Compute capability. CUDA 13 dropped everything below sm_75, so that is the
# portable default. Use -DCMAKE_CUDA_ARCHITECTURES=native on a machine with a GPU.
if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES OR CMAKE_CUDA_ARCHITECTURES STREQUAL "")
  set(CMAKE_CUDA_ARCHITECTURES 75)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_EXTENSIONS OFF)

# Every target here carries device code that other targets call, so build all
# of it as relocatable device code and let the final executables device-link.
set(CMAKE_CUDA_SEPARABLE_COMPILATION ON)

find_package(CUDAToolkit REQUIRED)
find_package(Threads REQUIRED)
include(CTest)

# Diagnostics that the code base trips on purpose (unused set variables in
# ported C code, jumps past initialisers in switch bodies).
set(LIBCU_CUDA_DIAG
  $<$<COMPILE_LANGUAGE:CUDA>:-Xcudafe=--diag_suppress=set_but_not_used>
  $<$<COMPILE_LANGUAGE:CUDA>:-Xcudafe=--diag_suppress=branch_past_initialization>)

# ---------------------------------------------------------------------------
# libcu: the device-side C library and the Sentinel bus
# ---------------------------------------------------------------------------
add_library(libcu STATIC
  src/libcu/sentinel-msg.cpp
  src/libcu/sentinel.cpp
  src/libcu/cuda_runtime.cpp
  src/libcu/stringcu.cu
  src/libcu/libcu.cu
  src/libcu/libcu.stdlib.cu)
add_library(libcu::libcu ALIAS libcu)
target_include_directories(libcu PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/include>
  $<INSTALL_INTERFACE:include>)
target_compile_options(libcu PRIVATE ${LIBCU_CUDA_DIAG})
target_link_libraries(libcu PUBLIC CUDA::cudart_static Threads::Threads)
# libcu++ (CCCL) is used for the system-scope atomics in Sentinel. CUDA 13 moved it to
# include/cccl, which nvcc finds on its own but the host compiler does not.
foreach(dir IN LISTS CUDAToolkit_INCLUDE_DIRS)
  if(EXISTS "${dir}/cccl")
    target_include_directories(libcu SYSTEM PUBLIC "${dir}/cccl")
  endif()
endforeach()
set_target_properties(libcu PROPERTIES PREFIX "" OUTPUT_NAME libcu)

# Headers only, for host programs that include the Sentinel client (the d* tools).
add_library(libcu_headers INTERFACE)
add_library(libcu::headers ALIAS libcu_headers)
target_include_directories(libcu_headers INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/include>
  ${CUDAToolkit_INCLUDE_DIRS})
target_compile_features(libcu_headers INTERFACE cxx_std_17)
target_link_libraries(libcu_headers INTERFACE Threads::Threads)
foreach(dir IN LISTS CUDAToolkit_INCLUDE_DIRS)
  if(EXISTS "${dir}/cccl")
    target_include_directories(libcu_headers SYSTEM INTERFACE "${dir}/cccl")
  endif()
endforeach()

# ---------------------------------------------------------------------------
# libcu.fileutils: device-side ls, cat, ... plus the d* host front ends
# ---------------------------------------------------------------------------
if(LIBCU_BUILD_FILEUTILS)
  add_library(libcu.fileutils STATIC
    src/libcu.fileutils/libcu.fileutils.cu
    src/libcu.fileutils/sentinel-msg.cpp)
  add_library(libcu::fileutils ALIAS libcu.fileutils)
  target_include_directories(libcu.fileutils PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/libcu.fileutils>)
  target_compile_options(libcu.fileutils PRIVATE ${LIBCU_CUDA_DIAG})
  target_link_libraries(libcu.fileutils PUBLIC libcu)
  set_target_properties(libcu.fileutils PROPERTIES PREFIX "" OUTPUT_NAME libcu.fileutils)

  # One small host executable per shell command, on purpose: each maps onto the
  # standard tool of the same name (dls -> ls, dcat -> cat, ...). They are plain C++
  # programs that include the Sentinel client and talk to a running libcu server over
  # the host bus; they contain no device code and do not link libcu.
  set(LIBCU_TOOLS cat cd chgrp chmod chown cmp cp grep ls mkdir more mv pwd rm rmdir)
  foreach(tool IN LISTS LIBCU_TOOLS)
    add_executable(d${tool} src/libcu.fileutils/d${tool}.cpp)
    target_link_libraries(d${tool} PRIVATE libcu::headers)
  endforeach()
endif()

# ---------------------------------------------------------------------------
# Tcl interpreters, both device ports, off by default
# ---------------------------------------------------------------------------
if(LIBCU_BUILD_JIMTCL)
  add_library(libcu.jimtcl STATIC
    src/tcl/libcu.jimtcl/jim.cu
    src/tcl/libcu.jimtcl/libcu.jimtcl.cu)
  target_include_directories(libcu.jimtcl PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/tcl/libcu.jimtcl>)
  target_compile_definitions(libcu.jimtcl PRIVATE
    jim_ext_aio jim_ext_namespace jim_ext_package JIM_MAINTAINER JIM_REFERENCES)
  target_compile_options(libcu.jimtcl PRIVATE ${LIBCU_CUDA_DIAG})
  target_link_libraries(libcu.jimtcl PUBLIC libcu)
  set_target_properties(libcu.jimtcl PROPERTIES PREFIX "" OUTPUT_NAME libcu.jimtcl)

  add_executable(jimtcl src/tcl/jimtcl/jimsh.cu)
  target_link_libraries(jimtcl PRIVATE libcu.jimtcl)
endif()

if(LIBCU_BUILD_JIMTCL78)
  add_library(libcu.jimtcl.78 STATIC
    src/tcl/libcu.jimtcl.78/jim.cu
    src/tcl/libcu.jimtcl.78/libcu.jimtcl.78.cu)
  target_include_directories(libcu.jimtcl.78 PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/tcl/libcu.jimtcl.78>)
  target_compile_definitions(libcu.jimtcl.78 PRIVATE
    jim_ext_aio jim_ext_namespace jim_ext_package JIM_MAINTAINER JIM_REFERENCES)
  target_compile_options(libcu.jimtcl.78 PRIVATE ${LIBCU_CUDA_DIAG})
  target_link_libraries(libcu.jimtcl.78 PUBLIC libcu)
  set_target_properties(libcu.jimtcl.78 PROPERTIES PREFIX "" OUTPUT_NAME libcu.jimtcl.78)
endif()

if(LIBCU_BUILD_TINYTCL)
  add_library(libcu.tinytcl STATIC src/tcl/libcu.tinytcl/libcu.tinytcl.cu)
  target_include_directories(libcu.tinytcl PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/tcl/libcu.tinytcl>)
  target_compile_definitions(libcu.tinytcl PRIVATE TCL_MEM_DEBUG)
  target_compile_options(libcu.tinytcl PRIVATE ${LIBCU_CUDA_DIAG})
  target_link_libraries(libcu.tinytcl PUBLIC libcu)
  set_target_properties(libcu.tinytcl PROPERTIES PREFIX "" OUTPUT_NAME libcu.tinytcl)

  add_executable(tinytcl src/tcl/tinytcl/tinySh.cu)
  target_link_libraries(tinytcl PRIVATE libcu.tinytcl)
endif()

# ---------------------------------------------------------------------------
# Tests: one runner, one ctest entry per case. They need a GPU to run, so CI
# only builds them; run `ctest -L gpu` on a machine with a device.
# ---------------------------------------------------------------------------
if(LIBCU_BUILD_TESTS AND LIBCU_BUILD_FILEUTILS)
  add_executable(libcu_tests
    src/libcu.tests/libcu.tests.cu
    src/libcu.tests/program.cu)
  target_compile_options(libcu_tests PRIVATE ${LIBCU_CUDA_DIAG})
  target_link_libraries(libcu_tests PRIVATE libcu.fileutils)

  # Keep in step with the switch in src/libcu.tests/program.cu.
  set(LIBCU_TEST_CASES
    1 crtdefs_test1 2 ctype_test1 3 dirent_test1 4 errno_test1 5 fcntl_test1
    6 fsystem_test1 7 grp_test1 8 host_test1 9 pwd_test1 10 regex_test1
    11 sentinel_test1 12 setjmp_test1 13 stddef_test1 14 stdio_test1 15 stdio_64bit
    16 stdio_ganging 17 stdio_scanf 18 stdlib_test1 19 stdlib_strtol 20 stdlib_strtoq
    21 string_test1 22 time_test1 23 unistd_test1)
  while(LIBCU_TEST_CASES)
    list(POP_FRONT LIBCU_TEST_CASES id name)
    add_test(NAME ${name} COMMAND libcu_tests ${id})
    set_tests_properties(${name} PROPERTIES LABELS gpu)
  endwhile()

  add_executable(libcu_bench src/libcu/program.cu)
  target_link_libraries(libcu_bench PRIVATE libcu)
endif()

# Host-only loopback test of the Sentinel host bus; runs without a GPU, so CI runs it.
if(LIBCU_BUILD_TESTS)
  add_executable(sentinel_host_test src/libcu.tests/sentinel_host_test.cpp)
  target_link_libraries(sentinel_host_test PRIVATE libcu)
  add_test(NAME sentinel_host COMMAND sentinel_host_test)
  set_tests_properties(sentinel_host PROPERTIES LABELS host TIMEOUT 120)
endif()
