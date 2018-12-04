cmake_minimum_required(VERSION 3.9)
project(nx-testcases)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

add_nx_target(NAME thread_suspend_test SOURCES source/main.c)
