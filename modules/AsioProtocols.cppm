module;

#ifdef PROTOCOMM_BUILD_ASIO
#include "AsioProtocols.hpp"
#endif

export module ProtoComm:AsioProtocols;

export namespace ProtoComm {
    #ifdef PROTOCOMM_BUILD_ASIO
    using ProtoComm::AsioSerialProtocol;
    using ProtoComm::AsioTcpClient;
    using ProtoComm::AsioTcpServer;
    #endif
}
