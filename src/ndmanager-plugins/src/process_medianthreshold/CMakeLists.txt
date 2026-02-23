include(CheckLanguage)
check_language(CUDA)

if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD          17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    set(CMAKE_CUDA_ARCHITECTURES     86 89 100 120)
    add_executable(process_medianthreshold
        process_medianthreshold.cpp
        process_medianthreshold_cuda.cu)
    target_compile_definitions(process_medianthreshold PRIVATE USE_CUDA)
    set_target_properties(process_medianthreshold PROPERTIES
        CUDA_SEPARABLE_COMPILATION ON)
    message(STATUS "process_medianthreshold: CUDA build (${CMAKE_CUDA_ARCHITECTURES})")
else()
    add_executable(process_medianthreshold process_medianthreshold.cpp)
    message(STATUS "process_medianthreshold: CPU build")
endif()
target_compile_features(process_medianthreshold PRIVATE cxx_std_17)
install(TARGETS process_medianthreshold DESTINATION "${CMAKE_INSTALL_BINDIR}")
add_manpage(process_medianthreshold)
