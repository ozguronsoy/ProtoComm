cmake_minimum_required(VERSION 3.28)

project(ProtoComm 
    VERSION 1.0.0
    DESCRIPTION "A header-only modern C++ library for message-based communication"
    LANGUAGES CXX
)

# Set C++20 as the standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options for building optional components
option(PROTOCOMM_BUILD_ASIO "Build Asio protocol implementations" OFF)
option(PROTOCOMM_BUILD_QT "Build Qt protocol implementations" OFF)
option(PROTOCOMM_BUILD_MODULES "Build C++20 modules" OFF)
option(PROTOCOMM_BUILD_TESTS "Build tests" OFF)
option(PROTOCOMM_BUILD_EXAMPLES "Build examples" OFF)

# Add subdirectories
add_subdirectory(include)
add_subdirectory(src)

# C++20 Modules (experimental)
if(PROTOCOMM_BUILD_MODULES)
    add_subdirectory(modules)
endif()

# Tests
if(PROTOCOMM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

# Examples
if(PROTOCOMM_BUILD_EXAMPLES)
    add_subdirectory(docs/examples)
endif()

# Installation
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Install targets (handled in subdirectories)

# Generate and install CMake config files
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/ProtoCommConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ProtoCommConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ProtoCommConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ProtoComm
)

install(EXPORT ProtoCommTargets
    FILE ProtoCommTargets.cmake
    NAMESPACE ProtoComm::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ProtoComm
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/ProtoCommConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/ProtoCommConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ProtoComm
)

# Export targets for build tree usage
export(EXPORT ProtoCommTargets
    FILE "${CMAKE_CURRENT_BINARY_DIR}/ProtoCommTargets.cmake"
    NAMESPACE ProtoComm::
)
