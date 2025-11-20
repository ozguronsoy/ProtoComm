module;

#include "ProtoComm.hpp"

export module ProtoComm;

export import :AsioProtocols;
export import :QtProtocols;

export namespace ProtoComm {
    using ProtoComm::IMessage;
    using ProtoComm::IRxMessage;
    using ProtoComm::ITxMessage;
    using ProtoComm::IFrameHandler;
    using ProtoComm::FrameHandler;
    using ProtoComm::ChecksumFrameHandler;
    using ProtoComm::ICommProtocol;
    using ProtoComm::Startable;
    using ProtoComm::CommStream;
}
