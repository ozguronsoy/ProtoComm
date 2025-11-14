cmake_minimum_required(VERSION 3.28)

project(ProtoCommTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core SerialPort)

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
    ../QtProtocols.cpp
    
    AsioTest.cpp
    QtTest.cpp
    test_main.cpp
)

target_link_libraries(
    ProtoCommTest PRIVATE
    
    gtest
    
    Qt6::Core
    Qt6::SerialPort
)