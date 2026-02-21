cmake_minimum_required(VERSION 3.20)
project(hy_mt_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
  pugixml
  GIT_REPOSITORY https://github.com/zeux/pugixml.git
  GIT_TAG v1.15
)
FetchContent_MakeAvailable(pugixml)

if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../llama.cpp/CMakeLists.txt")
  message(FATAL_ERROR "Expected local llama.cpp at ../llama.cpp")
endif()

set(LLAMA_BUILD_COMMON OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(LLAMA_OPENSSL OFF CACHE BOOL "" FORCE)

option(HYMT_ENABLE_CUDA "Enable GGML CUDA backend in llama.cpp" OFF)
set(GGML_CUDA ${HYMT_ENABLE_CUDA} CACHE BOOL "" FORCE)

if (HYMT_ENABLE_CUDA)
  # Keep CUDA selection deterministic on systems with multiple toolkits.
  set(HYMT_CUDA_COMPILER "/usr/bin/nvcc" CACHE FILEPATH "CUDA compiler path")
  set(HYMT_CUDA_TOOLKIT_ROOT "/usr" CACHE PATH "CUDA toolkit root")
  set(HYMT_CUDA_COMPRESSION_MODE "none" CACHE STRING "GGML CUDA fatbin compression mode")
  set_property(CACHE HYMT_CUDA_COMPRESSION_MODE PROPERTY STRINGS "none;speed;balance;size")

  if (EXISTS "${HYMT_CUDA_COMPILER}")
    set(CMAKE_CUDA_COMPILER "${HYMT_CUDA_COMPILER}" CACHE FILEPATH "" FORCE)
  endif()

  if (HYMT_CUDA_TOOLKIT_ROOT)
    set(CUDAToolkit_ROOT "${HYMT_CUDA_TOOLKIT_ROOT}" CACHE PATH "" FORCE)
  endif()

  # Avoid toolkit-specific default changes unexpectedly affecting local builds.
  set(GGML_CUDA_COMPRESSION_MODE "${HYMT_CUDA_COMPRESSION_MODE}" CACHE STRING "" FORCE)

  message(STATUS "HYMT CUDA compiler: ${CMAKE_CUDA_COMPILER}")
  message(STATUS "HYMT CUDA toolkit root: ${CUDAToolkit_ROOT}")
  message(STATUS "HYMT CUDA compression: ${GGML_CUDA_COMPRESSION_MODE}")
endif()

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../llama.cpp" "${CMAKE_BINARY_DIR}/llama.cpp-build")

add_executable(tei_mt
  src/main.cpp
  src/config.cpp
  src/tei_reader.cpp
  src/translator_llama.cpp
  src/pipeline.cpp
  src/writer_md.cpp
  src/writer_tei.cpp
)

target_include_directories(tei_mt PRIVATE src)
target_link_libraries(tei_mt PRIVATE pugixml::pugixml llama)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(tei_mt PRIVATE -Wall -Wextra -Wpedantic)
endif()
