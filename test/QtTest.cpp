#include <gtest/gtest.h>
#include "TestUtils.hpp"
#include "QtProtocols.hpp"

using namespace ProtoComm;

TEST(QtSerial, Loopback)
{
    const size_t msgCountPerType = 10;
    std::vector<TelemetryMessage> txTelemetryMessages(msgCountPerType);
    std::vector<ImuMessage> txImuMessages(msgCountPerType);
    const size_t totalMsgCount = GenerateTxMessages(msgCountPerType, txTelemetryMessages, txImuMessages);

    CommStream<QtSerialProtocol> stream;
    auto writeChannel = stream.Start("/dev/ttyS10", 9600);
    auto readChannel = stream.Start("/dev/ttyS11", 9600);

    EXPECT_TRUE(writeChannel);
    EXPECT_TRUE(readChannel);

    for (size_t i = 0; i < msgCountPerType; ++i)
    {
        stream.Write(writeChannel, txTelemetryMessages[i]);
        stream.Write(writeChannel, txImuMessages[i]);
    }

    auto rxMessages = stream.Read<TelemetryMessage, ImuMessage>(readChannel, totalMsgCount, std::chrono::milliseconds(5000));

    EXPECT_EQ(rxMessages.size(), totalMsgCount);

    for (size_t i = 0, j = 0, k = 0;
        i < totalMsgCount;
        ++i)
    {
        auto pTelemetryMessage = dynamic_cast<TelemetryMessage*>(rxMessages[i].get());
        auto pImuMessage = dynamic_cast<ImuMessage*>(rxMessages[i].get());

        EXPECT_TRUE(pTelemetryMessage || pImuMessage);

        if (pTelemetryMessage)
            EXPECT_EQ(*pTelemetryMessage, txTelemetryMessages[j++]);
        else if (pImuMessage)
            EXPECT_EQ(*pImuMessage, txImuMessages[k++]);
    }
}

TEST(QtSerial, LoopbackAsync)
{
    const size_t msgCountPerType = 10;
    std::vector<TelemetryMessage> txTelemetryMessages(msgCountPerType);
    std::vector<ImuMessage> txImuMessages(msgCountPerType);
    const size_t totalMsgCount = GenerateTxMessages(msgCountPerType, txTelemetryMessages, txImuMessages);

    CommStream<QtSerialProtocol> stream;
    auto writeChannel = stream.Start("/dev/ttyS10", 9600);
    auto readChannel = stream.Start("/dev/ttyS11", 9600);

    EXPECT_TRUE(writeChannel);
    EXPECT_TRUE(readChannel);

    auto fr = stream.ReadAsync<TelemetryMessage, ImuMessage>(readChannel, totalMsgCount);

    std::vector<std::reference_wrapper<const ITxMessage>> txMessages;
    txMessages.reserve(totalMsgCount);
    for (size_t i = 0; i < msgCountPerType; ++i)
    {
        txMessages.push_back(txTelemetryMessages[i]);
        txMessages.push_back(txImuMessages[i]);
    }

    auto fw = stream.WriteAsync(writeChannel, txMessages);
    const size_t txCount = fw.get();
    EXPECT_EQ(txCount, totalMsgCount);

    auto rxMessages = fr.get();

    EXPECT_EQ(rxMessages.size(), totalMsgCount);

    for (size_t i = 0, j = 0, k = 0;
        i < totalMsgCount;
        ++i)
    {
        auto pTelemetryMessage = dynamic_cast<TelemetryMessage*>(rxMessages[i].get());
        auto pImuMessage = dynamic_cast<ImuMessage*>(rxMessages[i].get());

        EXPECT_TRUE(pTelemetryMessage || pImuMessage);

        if (pTelemetryMessage)
            EXPECT_EQ(*pTelemetryMessage, txTelemetryMessages[j++]);
        else if (pImuMessage)
            EXPECT_EQ(*pImuMessage, txImuMessages[k++]);
    }
}