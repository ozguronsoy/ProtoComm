# C++20 Modules (experimental)
add_library(ProtoComm_Module)

target_sources(ProtoComm_Module
    PUBLIC
        FILE_SET CXX_MODULES FILES
            ProtoComm.cppm
            AsioProtocols.cppm
            QtProtocols.cppm
)

target_compile_features(ProtoComm_Module PUBLIC cxx_std_20)

target_include_directories(ProtoComm_Module PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

add_library(ProtoComm::Module ALIAS ProtoComm_Module)

# Installation
install(TARGETS ProtoComm_Module
    EXPORT ProtoCommTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    FILE_SET CXX_MODULES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ProtoComm/modules
)
