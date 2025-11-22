module;

#ifdef PROTOCOMM_BUILD_QT
#include "QtProtocols.hpp"
#endif

export module ProtoComm:QtProtocols;

export namespace ProtoComm {
    #ifdef PROTOCOMM_BUILD_QT
    using ProtoComm::QtSerialProtocol;
    #endif
}
