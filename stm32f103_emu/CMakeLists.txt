cmake_minimum_required(VERSION 3.10.0)

project(stm32f103_emu VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(stm32f103_emu_lib
    src/emu.cpp
    src/registers.cpp
    src/programparser.cpp
    src/elfloader.cpp
    src/memory.cpp
    src/peripherals.cpp
    src/cpu.cpp
    src/fuzzer.cpp
)

target_include_directories(stm32f103_emu_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

add_executable(stm32f103_emu
    main.cpp
)
target_link_libraries(stm32f103_emu PRIVATE stm32f103_emu_lib)

add_executable(stm32f103_fuzzer
    fuzz_main.cpp
)
target_link_libraries(stm32f103_fuzzer PRIVATE stm32f103_emu_lib)
