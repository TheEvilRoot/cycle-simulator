cmake_minimum_required(VERSION 3.10)

project(cycle_simulator)

set(SOURCE_FILES
    simulator.cpp
    event.h
    instruction.h
    source.h
    utilities.h
)

message(STATUS "Loading sources for ${PROJECT_NAME} ...")
message(STATUS ${SOURCE_FILES}) 

add_executable(${PROJECT_NAME} ${SOURCE_FILES}) 

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -pedantic -Werror -Wextra)
