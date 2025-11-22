# Fetch GoogleTest
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(googletest)

# Enable testing
include(GoogleTest)

# Core tests (header-only, no Qt needed)
add_executable(ProtoCommTest
    ProtoCommTest.cpp
)

target_link_libraries(ProtoCommTest PRIVATE
    ProtoComm::ProtoComm
    gtest
    gtest_main
)

gtest_discover_tests(ProtoCommTest)

# Asio tests (if Asio support is enabled)
if(TARGET ProtoComm::Asio)
    add_executable(AsioTest
        AsioTest.cpp
    )

    target_link_libraries(AsioTest PRIVATE
        ProtoComm::Asio
        gtest
        gtest_main
    )

    gtest_discover_tests(AsioTest)
endif()

# Qt tests (if Qt support is enabled)
if(TARGET ProtoComm::Qt)
    add_executable(QtTest
        QtTest.cpp
        test_main.cpp
    )

    target_link_libraries(QtTest PRIVATE
        ProtoComm::Qt
        gtest
    )

    gtest_discover_tests(QtTest)
endif()
