cmake_minimum_required(VERSION 3.24)

project(video_engine LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(VIDEO_ENGINE_ENABLE_NATIVE_ARCH "Enable native architecture optimizations" OFF)

if(VIDEO_ENGINE_ENABLE_NATIVE_ARCH)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-march=native>)
endif()

find_package(CUDAToolkit REQUIRED)
find_package(pybind11 CONFIG REQUIRED)

add_library(video_engine_ffmpeg INTERFACE)
set(VIDEO_ENGINE_FFMPEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg-dev")

if(EXISTS "${VIDEO_ENGINE_FFMPEG_ROOT}/include" AND EXISTS "${VIDEO_ENGINE_FFMPEG_ROOT}/lib")
  find_library(FFMPEG_AVFORMAT_LIBRARY NAMES avformat PATHS "${VIDEO_ENGINE_FFMPEG_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
  find_library(FFMPEG_AVCODEC_LIBRARY NAMES avcodec PATHS "${VIDEO_ENGINE_FFMPEG_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
  find_library(FFMPEG_AVUTIL_LIBRARY NAMES avutil PATHS "${VIDEO_ENGINE_FFMPEG_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
  find_library(FFMPEG_SWSCALE_LIBRARY NAMES swscale PATHS "${VIDEO_ENGINE_FFMPEG_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
  target_include_directories(video_engine_ffmpeg INTERFACE "${VIDEO_ENGINE_FFMPEG_ROOT}/include")
  target_link_libraries(video_engine_ffmpeg INTERFACE
    "${FFMPEG_AVFORMAT_LIBRARY}"
    "${FFMPEG_AVCODEC_LIBRARY}"
    "${FFMPEG_AVUTIL_LIBRARY}"
    "${FFMPEG_SWSCALE_LIBRARY}"
  )
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
    libswscale
  )
  target_link_libraries(video_engine_ffmpeg INTERFACE PkgConfig::FFMPEG)
endif()

set(VIDEO_ENGINE_SOURCES
  src/bindings/py_module.cpp
  src/core/Logger.cpp
  src/core/RenderEngine.cpp
  src/core/RenderJob.cpp
  src/codec/FFmpegDecoder.cpp
  src/codec/FFmpegEncoder.cpp
  src/cuda/CudaContext.cpp
  src/cuda/CudaSubtitleRectEffect.cu
)

pybind11_add_module(video_engine MODULE ${VIDEO_ENGINE_SOURCES})

target_include_directories(video_engine
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(video_engine
  PRIVATE
    CUDA::cudart
    CUDA::cuda_driver
    video_engine_ffmpeg
)

set_target_properties(video_engine PROPERTIES
  CUDA_SEPARABLE_COMPILATION ON
  CUDA_RESOLVE_DEVICE_SYMBOLS ON
)

if(NOT CMAKE_CUDA_ARCHITECTURES)
  set_property(TARGET video_engine PROPERTY CUDA_ARCHITECTURES native)
endif()
