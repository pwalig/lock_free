cmake_minimum_required(VERSION 3.15)

project(lock_free)

add_executable(tests tests.cpp)

set_target_properties(tests PROPERTIES CXX_STANDARD 20)

target_include_directories(tests PRIVATE include)

if(MSVC)
    target_compile_options(tests PRIVATE /W4 /WX)
else()
    target_compile_options(tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()