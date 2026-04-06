cmake_minimum_required(VERSION 3.20)
project(hy_mt_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# mold: faster linker. Prefer MSYS2/MinGW + Ninja + g++/clang++ from ucrt64/mingw64 so OpenMP and libm match
# (avoid mixing MSVC-ABI Clang from PowerShell with MSYS OpenMP, which used to pull m.lib).
set(HYMT_MOLD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../mold" CACHE PATH
    "Directory containing mold.exe (or a bin/ subdir). Used when HYMT_USE_MOLD is ON.")

option(HYMT_USE_MOLD "Link with mold (-fuse-ld=...). Use with MSYS2 MinGW g++/clang + Ninja for OpenMP." ON)

find_program(
  HYMT_MOLD_PROGRAM
  NAMES mold mold.exe
  HINTS "${HYMT_MOLD_ROOT}" "${HYMT_MOLD_ROOT}/bin"
  DOC "mold linker executable"
)

if (HYMT_USE_MOLD)
  if (NOT HYMT_MOLD_PROGRAM)
    message(
      WARNING
      "hy-mt: HYMT_USE_MOLD=ON but mold was not found (HYMT_MOLD_ROOT='${HYMT_MOLD_ROOT}' and PATH). "
      "Disabling mold; pass -DHYMT_USE_MOLD=OFF to silence, or install mold under HYMT_MOLD_ROOT."
    )
    set(HYMT_USE_MOLD OFF CACHE BOOL "Link with mold (-fuse-ld=...). Use with MSYS2 MinGW g++/clang + Ninja for OpenMP." FORCE)
  else()
    # Full path so Ninja builds from PowerShell still invoke mold without MSYS PATH.
    file(TO_CMAKE_PATH "${HYMT_MOLD_PROGRAM}" _hymt_mold_cmake)
    string(REPLACE "\\" "/" _hymt_mold_slash "${_hymt_mold_cmake}")
    set(_hymt_fuse_mold " -fuse-ld=${_hymt_mold_slash}")
    string(APPEND CMAKE_EXE_LINKER_FLAGS "${_hymt_fuse_mold}")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS "${_hymt_fuse_mold}")
    string(APPEND CMAKE_MODULE_LINKER_FLAGS "${_hymt_fuse_mold}")
    message(STATUS "hy-mt: using mold linker: ${HYMT_MOLD_PROGRAM}")
  endif()
endif()

include(FetchContent)

FetchContent_Declare(
  pugixml
  GIT_REPOSITORY https://github.com/zeux/pugixml.git
  GIT_TAG v1.15
)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(pugixml)
FetchContent_MakeAvailable(nlohmann_json)

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
  set(HYMT_CUDA_COMPRESSION_MODE "none" CACHE STRING "GGML CUDA fatbin compression mode")
  set_property(CACHE HYMT_CUDA_COMPRESSION_MODE PROPERTY STRINGS "none;speed;balance;size")

  if (WIN32)
    # nvcc on Windows uses MSVC (cl.exe) as the host compiler; MinGW g++ cannot drive a supported CUDA build.
    if (MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
      message(
        FATAL_ERROR
        "HYMT_ENABLE_CUDA on Windows requires MSVC (cl.exe), not MinGW g++. "
        "Open 'x64 Native Tools Command Prompt for VS 2022', or run vcvars64.bat, then configure with "
        "-G Ninja -DCMAKE_CXX_COMPILER=cl -DCMAKE_C_COMPILER=cl (or use -G \"Visual Studio 17 2022\" -A x64)."
      )
    endif()

    set(HYMT_CUDA_COMPILER "" CACHE FILEPATH "Path to nvcc.exe (optional if CUDA is on PATH)")
    if (NOT HYMT_CUDA_TOOLKIT_ROOT)
      if (DEFINED ENV{CUDA_PATH})
        set(HYMT_CUDA_TOOLKIT_ROOT "$ENV{CUDA_PATH}" CACHE PATH "CUDA toolkit root (default: CUDA_PATH)")
      else()
        set(HYMT_CUDA_TOOLKIT_ROOT "" CACHE PATH "CUDA toolkit root (e.g. C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.9)")
      endif()
    endif()
  else()
    set(HYMT_CUDA_COMPILER "/usr/bin/nvcc" CACHE FILEPATH "CUDA compiler path")
    set(HYMT_CUDA_TOOLKIT_ROOT "/usr" CACHE PATH "CUDA toolkit root")
  endif()

  if (HYMT_CUDA_COMPILER AND EXISTS "${HYMT_CUDA_COMPILER}")
    set(CMAKE_CUDA_COMPILER "${HYMT_CUDA_COMPILER}" CACHE FILEPATH "" FORCE)
  endif()

  if (HYMT_CUDA_TOOLKIT_ROOT)
    set(CUDAToolkit_ROOT "${HYMT_CUDA_TOOLKIT_ROOT}" CACHE PATH "" FORCE)
  endif()

  set(GGML_CUDA_COMPRESSION_MODE "${HYMT_CUDA_COMPRESSION_MODE}" CACHE STRING "" FORCE)

  message(STATUS "HYMT_ENABLE_CUDA: CMAKE_CUDA_COMPILER=${CMAKE_CUDA_COMPILER}")
  message(STATUS "HYMT_ENABLE_CUDA: CUDAToolkit_ROOT=${CUDAToolkit_ROOT}")
  message(STATUS "HYMT_ENABLE_CUDA: GGML_CUDA_COMPRESSION_MODE=${GGML_CUDA_COMPRESSION_MODE}")
endif()

# Windows: Clang in "MSVC mode" (lld-link, msvcrt) + MSYS FindOpenMP was a bad mix with ggml's old -lm rule.
# Prefer MSYS2 + MinGW + mold (HYMT_USE_MOLD); then OpenMP and libm behave like a normal Unix-y toolchain.
option(
  HYMT_WIN32_CLANG_OPENMP_WORKAROUND
  "Disable ggml OpenMP for Clang on Windows without MinGW (MSVC ABI). Ignored when HYMT_USE_MOLD is ON."
  ON
)
if (WIN32
    AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
    AND NOT MINGW
    AND HYMT_WIN32_CLANG_OPENMP_WORKAROUND
    AND NOT HYMT_USE_MOLD)
  message(
    STATUS
    "hy-mt: GGML_OPENMP=OFF (Windows Clang + non-MinGW: avoids broken OpenMP/libm mix with MSVC linker). "
    "For OpenMP use MSYS2: ucrt64/mingw64 shell, -G Ninja, -DCMAKE_CXX_COMPILER=g++ (or clang++), HYMT_USE_MOLD=ON."
  )
  set(GGML_OPENMP OFF CACHE BOOL "ggml: use OpenMP" FORCE)
endif()

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../llama.cpp" "${CMAKE_BINARY_DIR}/llama.cpp-build")

if (MSVC AND TARGET llama)
  target_compile_options(llama PRIVATE /Zc:char8_t-)
endif()

add_executable(tei_mt
  src/main.cpp
  src/config.cpp
  src/tei_reader.cpp
  src/segment_batch.cpp
  src/translator_llama.cpp
  src/pipeline.cpp
  src/sorting_filter.cpp
  src/writer_md.cpp
  src/writer_tei.cpp
)

target_include_directories(tei_mt PRIVATE src)
target_link_libraries(tei_mt PRIVATE pugixml::pugixml nlohmann_json::nlohmann_json llama)

# Keep executable location predictable across generators:
# - Ninja/Unix Makefiles (single-config): <build>/bin/tei_mt(.exe)
# - Visual Studio (multi-config): <build>/bin/<Config>/tei_mt.exe
set_target_properties(tei_mt PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
if (CMAKE_CONFIGURATION_TYPES)
  foreach(cfg ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER "${cfg}" cfg_upper)
    set_target_properties(tei_mt PROPERTIES
      "RUNTIME_OUTPUT_DIRECTORY_${cfg_upper}" "${CMAKE_BINARY_DIR}/bin/${cfg}"
    )
  endforeach()
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(tei_mt PRIVATE -Wall -Wextra -Wpedantic)
endif()
