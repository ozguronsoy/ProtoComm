# ProtoComm Examples

# NMEA Parser Example (synchronous)
add_executable(nmea_parser
    nvea-parser.cpp
)

target_link_libraries(nmea_parser PRIVATE
    ProtoComm::ProtoComm
)

target_include_directories(nmea_parser PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

# NMEA Parser Example (asynchronous with futures)
add_executable(nmea_parser_async
    nvea-parser-async.cpp
)

target_link_libraries(nmea_parser_async PRIVATE
    ProtoComm::ProtoComm
)

target_include_directories(nmea_parser_async PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

# Optional: Install examples
install(TARGETS nmea_parser nmea_parser_async
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/ProtoComm/examples
)

# Optional: Install example source files for reference
install(FILES
    nvea-parser.cpp
    nvea-parser-async.cpp
    message.md
    protocols.md
    frame-handlers.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/examples
)
