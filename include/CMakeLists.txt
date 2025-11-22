# Header-only core library
add_library(ProtoComm INTERFACE)
add_library(ProtoComm::ProtoComm ALIAS ProtoComm)

target_include_directories(ProtoComm INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(ProtoComm INTERFACE cxx_std_20)

# Installation
install(TARGETS ProtoComm
    EXPORT ProtoCommTargets
)

install(FILES
    ProtoComm.hpp
    AsioProtocols.hpp
    QtProtocols.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ProtoComm
)
