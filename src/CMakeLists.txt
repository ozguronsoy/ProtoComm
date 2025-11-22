# Optional Asio protocol implementations
if(PROTOCOMM_BUILD_ASIO)
    # Try to find asio (standalone or boost)
    find_package(asio QUIET)
    if(NOT asio_FOUND)
        find_package(Boost QUIET COMPONENTS system)
        if(Boost_FOUND)
            set(ASIO_FOUND TRUE)
            set(ASIO_INCLUDE_DIRS ${Boost_INCLUDE_DIRS})
            set(ASIO_LIBRARIES ${Boost_LIBRARIES})
        else()
            # Fetch standalone asio if not found
            include(FetchContent)
            FetchContent_Declare(
                asio
                GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
                GIT_TAG asio-1-30-2
            )
            FetchContent_MakeAvailable(asio)
            set(ASIO_FOUND TRUE)
            set(ASIO_INCLUDE_DIRS ${asio_SOURCE_DIR}/asio/include)
        endif()
    else()
        set(ASIO_FOUND TRUE)
        set(ASIO_INCLUDE_DIRS ${asio_INCLUDE_DIRS})
    endif()

    if(ASIO_FOUND)
        add_library(ProtoComm_Asio STATIC
            AsioProtocols.cpp
        )
        add_library(ProtoComm::Asio ALIAS ProtoComm_Asio)

        target_link_libraries(ProtoComm_Asio 
            PUBLIC ProtoComm::ProtoComm
        )

        target_include_directories(ProtoComm_Asio PUBLIC
            $<BUILD_INTERFACE:${ASIO_INCLUDE_DIRS}>
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        )

        if(ASIO_LIBRARIES)
            target_link_libraries(ProtoComm_Asio PUBLIC ${ASIO_LIBRARIES})
        endif()

        # Platform-specific libraries
        if(WIN32)
            target_link_libraries(ProtoComm_Asio PUBLIC ws2_32 wsock32)
        elseif(UNIX AND NOT APPLE)
            target_link_libraries(ProtoComm_Asio PUBLIC pthread)
        endif()

        target_compile_definitions(ProtoComm_Asio PUBLIC ASIO_STANDALONE PROTOCOMM_BUILD_ASIO)

        # Installation
        install(TARGETS ProtoComm_Asio
            EXPORT ProtoCommTargets
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()
endif()

# Optional Qt protocol implementations
if(PROTOCOMM_BUILD_QT)
    find_package(Qt6 QUIET COMPONENTS Core SerialPort)
    
    if(Qt6_FOUND)
        set(CMAKE_AUTOMOC ON)
        set(CMAKE_AUTORCC ON)
        set(CMAKE_AUTOUIC ON)

        add_library(ProtoComm_Qt STATIC
            QtProtocols.cpp
        )
        add_library(ProtoComm::Qt ALIAS ProtoComm_Qt)

        target_link_libraries(ProtoComm_Qt
            PUBLIC 
                ProtoComm::ProtoComm
                Qt6::Core
                Qt6::SerialPort
        )

        target_include_directories(ProtoComm_Qt PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        )

        # Installation
        install(TARGETS ProtoComm_Qt
            EXPORT ProtoCommTargets
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    else()
        message(WARNING "Qt6 not found. Skipping Qt protocol implementations.")
    endif()
endif()
