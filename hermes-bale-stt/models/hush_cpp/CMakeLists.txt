cmake_minimum_required(VERSION 3.14)
project(hush_enhance_onnx LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find ONNX Runtime
find_library(ONNX_RUNTIME_LIB onnxruntime HINTS /usr/local/lib)
find_path(ONNX_RUNTIME_INCLUDE onnxruntime_cxx_api.h HINTS /usr/local/include/onnxruntime)

if(NOT ONNX_RUNTIME_LIB)
    message(FATAL_ERROR "ONNX Runtime library not found")
endif()
if(NOT ONNX_RUNTIME_INCLUDE)
    message(FATAL_ERROR "ONNX Runtime headers not found")
endif()

message(STATUS "ONNX Runtime lib: ${ONNX_RUNTIME_LIB}")
message(STATUS "ONNX Runtime include: ${ONNX_RUNTIME_INCLUDE}")

# Source files
set(KNF_SOURCES
    src/knf/stft.cc
    src/knf/istft.cc
    src/knf/rfft.cc
    src/knf/feature-window.cc
    src/knf/kaldi-math.cc
    src/knf/log.cc
)

set(KISSFFT_SOURCES
    src/knf/kiss_fft.c
    src/knf/kiss_fftr.c
)

set(MAIN_SOURCES
    src/hush_enhance_onnx.cpp
    src/wav_io.cc
    src/feature_extract.cc
)

# Build kaldi-native-fbank as a static library
add_library(knf_static STATIC ${KNF_SOURCES} ${KISSFFT_SOURCES})
target_include_directories(knf_static PUBLIC src/knf)
target_compile_definitions(knf_static PRIVATE KISSFFT_STATIC)

# Build main executable
add_executable(hush_enhance_onnx ${MAIN_SOURCES})
target_include_directories(hush_enhance_onnx PRIVATE
    src
    src/knf
    ${ONNX_RUNTIME_INCLUDE}
)
target_link_libraries(hush_enhance_onnx PRIVATE
    knf_static
    ${ONNX_RUNTIME_LIB}
    m  # math library for log, exp, etc.
)

# Copy the ONNX models directory to build output
add_custom_command(TARGET hush_enhance_onnx POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/onnx
        $<TARGET_FILE_DIR:hush_enhance_onnx>/onnx
    COMMENT "Copying ONNX model directory"
)
