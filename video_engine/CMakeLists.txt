cmake_minimum_required(VERSION 3.24)

project(video_engine LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_CUDA_ARCHITECTURES)
  set(CMAKE_CUDA_ARCHITECTURES native)
endif()

option(VIDEO_ENGINE_ENABLE_NATIVE_ARCH "Enable native architecture optimizations" OFF)

if(VIDEO_ENGINE_ENABLE_NATIVE_ARCH)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-march=native>)
endif()

find_package(CUDAToolkit REQUIRED)
find_package(pybind11 CONFIG REQUIRED)
find_package(PkgConfig QUIET)

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
  if(NOT PkgConfig_FOUND)
    find_package(PkgConfig REQUIRED)
  endif()
  pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
    libswscale
  )
  target_link_libraries(video_engine_ffmpeg INTERFACE PkgConfig::FFMPEG)
endif()

add_library(video_engine_subtitle INTERFACE)
if(PkgConfig_FOUND)
  pkg_check_modules(LIBASS QUIET IMPORTED_TARGET libass)
  pkg_check_modules(HARFBUZZ QUIET IMPORTED_TARGET harfbuzz)
endif()
if(TARGET PkgConfig::LIBASS)
  target_link_libraries(video_engine_subtitle INTERFACE PkgConfig::LIBASS)
  target_compile_definitions(video_engine_subtitle INTERFACE VIDEO_ENGINE_HAS_LIBASS=1)
endif()
find_package(Freetype QUIET)
if(Freetype_FOUND AND TARGET PkgConfig::HARFBUZZ)
  target_link_libraries(video_engine_subtitle INTERFACE Freetype::Freetype PkgConfig::HARFBUZZ)
  target_compile_definitions(video_engine_subtitle INTERFACE VIDEO_ENGINE_HAS_TEXTBOX_RENDERER=1)
endif()

set(VIDEO_ENGINE_SOURCES
  src/bindings/py_module.cpp
  src/core/AssSubtitleRenderer.cpp
  src/core/BlurBoxEffect.cpp
  src/core/Logger.cpp
  src/core/OverlayLayerRenderer.cpp
  src/core/RenderEngine.cpp
  src/core/RenderJob.cpp
  src/core/SrtParser.cpp
  src/core/SubtitleLayerRenderer.cpp
  src/core/SubtitleRenderer.cpp
  src/core/TextBoxRenderer.cpp
  src/core/timeline/RenderJobAdapter.cpp
  src/codec/FFmpegDecoder.cpp
  src/codec/FFmpegEncoder.cpp
  src/cuda/CudaBuffer.cpp
  src/cuda/CudaContext.cpp
)

add_library(video_engine_cuda STATIC
  src/cuda/CudaSubtitleRectEffect.cu
)

target_include_directories(video_engine_cuda
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(video_engine_cuda
  PRIVATE
    CUDA::cudart
    CUDA::cuda_driver
    video_engine_ffmpeg
)

if(TARGET CUDA::cudadevrt)
  target_link_libraries(video_engine_cuda PRIVATE CUDA::cudadevrt)
endif()

set_target_properties(video_engine_cuda PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  CUDA_SEPARABLE_COMPILATION ON
  CUDA_RESOLVE_DEVICE_SYMBOLS ON
)

pybind11_add_module(_video_engine_native MODULE ${VIDEO_ENGINE_SOURCES})

target_include_directories(_video_engine_native
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(_video_engine_native
  PRIVATE
    CUDA::cudart
    CUDA::cuda_driver
    video_engine_cuda
    video_engine_ffmpeg
    video_engine_subtitle
)

set_target_properties(_video_engine_native PROPERTIES
  LINKER_LANGUAGE CXX
)
