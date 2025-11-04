cmake_minimum_required(VERSION 3.28)

project(ProtoCommTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(googletest)

include_directories(
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/..
    ${GTEST_INCLUDE_DIRS}
)

add_executable(
    ProtoCommTest
    
    ../AsioProtocols.cpp

    AsioTest.cpp
)

target_link_libraries(
    ProtoCommTest PRIVATE
    gtest
    gtest_main
)