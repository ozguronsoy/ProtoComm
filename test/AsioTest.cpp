#include <gtest/gtest.h>
#include <random>
#include "TestUtils.hpp"
#include "AsioProtocols.hpp"

using namespace ProtoComm;

TEST(AsioSerial, Loopback)
{
    const size_t msgCountPerType = 10;
    std::vector<TelemetryMessage> txTelemetryMessages(msgCountPerType);
    std::vector<ImuMessage> txImuMessages(msgCountPerType);
    const size_t totalMsgCount = GenerateTxMessages(msgCountPerType, txTelemetryMessages, txImuMessages);

    CommStream<AsioSerialProtocol> stream;
    auto writeChannel = stream.Start("/dev/ttyS10", asio::serial_port_base::baud_rate(9600));
    auto readChannel = stream.Start("/dev/ttyS11", asio::serial_port_base::baud_rate(9600));

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

TEST(AsioSerial, LoopbackAsync)
{
    const size_t msgCountPerType = 10;
    std::vector<TelemetryMessage> txTelemetryMessages(msgCountPerType);
    std::vector<ImuMessage> txImuMessages(msgCountPerType);
    const size_t totalMsgCount = GenerateTxMessages(msgCountPerType, txTelemetryMessages, txImuMessages);

    CommStream<AsioSerialProtocol> stream;
    auto writeChannel = stream.Start("/dev/ttyS10", asio::serial_port_base::baud_rate(9600));
    auto readChannel = stream.Start("/dev/ttyS11", asio::serial_port_base::baud_rate(9600));

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
    fw.wait_for(std::chrono::milliseconds(5000));
    const size_t txCount = fw.get();
    EXPECT_EQ(txCount, totalMsgCount);

    fr.wait_for(std::chrono::milliseconds(5000));
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

TEST(AsioTcp, Loopback)
{
    const size_t msgCountPerType = 10;
    std::vector<TelemetryMessage> txTelemetryMessages(msgCountPerType);
    std::vector<ImuMessage> txImuMessages(msgCountPerType);

    CommStream<AsioTcpServer> server;
    std::array<CommStream<AsioTcpClient>, 5> clients;

    (void)server.Start("127.0.0.1", "5000");

    EXPECT_TRUE(server.IsRunning());

    for (size_t i = 0; i < clients.size(); ++i)
    {
        (void)clients[i].Start("127.0.0.1", "5000");
        EXPECT_TRUE(clients[i].IsRunning());
    }

    {
        size_t i = 0;
        while (server.ChannelCount() != clients.size() && (i++ < 500))
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        EXPECT_EQ(server.ChannelCount(), clients.size());
        if (server.ChannelCount() != clients.size())
            return;
    }

    for (size_t c = 0; c < clients.size(); ++c)
    {
        auto& client = clients[c];

        const size_t totalMsgCount = GenerateTxMessages(msgCountPerType, txTelemetryMessages, txImuMessages);

        for (size_t i = 0; i < msgCountPerType; ++i)
        {
            client.Write(client.GetChannel(0), txTelemetryMessages[i]);
            client.Write(client.GetChannel(0), txImuMessages[i]);
        }

        auto rxMessages = server.Read<TelemetryMessage, ImuMessage>(server.GetChannel(c), totalMsgCount, std::chrono::milliseconds(5000));

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

        rxMessages.clear();

        for (size_t i = 0; i < msgCountPerType; ++i)
        {
            server.Write(server.GetChannel(c), txTelemetryMessages[i]);
            server.Write(server.GetChannel(c), txImuMessages[i]);
        }

        rxMessages = client.Read<TelemetryMessage, ImuMessage>(client.GetChannel(0), totalMsgCount, std::chrono::milliseconds(5000));
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
}

TEST(AsioTcp, LoopbackAsync)
{
    const size_t msgCountPerType = 10;
    std::vector<TelemetryMessage> txTelemetryMessages(msgCountPerType);
    std::vector<ImuMessage> txImuMessages(msgCountPerType);

    CommStream<AsioTcpServer> server;
    std::array<CommStream<AsioTcpClient>, 5> clients;

    (void)server.Start("127.0.0.1", "5000");

    EXPECT_TRUE(server.IsRunning());

    for (size_t i = 0; i < clients.size(); ++i)
    {
        (void)clients[i].Start("127.0.0.1", "5000");
        EXPECT_TRUE(clients[i].IsRunning());
    }

    {
        size_t i = 0;
        while (server.ChannelCount() != clients.size() && (i++ < 500))
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        EXPECT_EQ(server.ChannelCount(), clients.size());
        if (server.ChannelCount() != clients.size())
            return;
    }

    for (size_t c = 0; c < clients.size(); ++c)
    {
        auto& client = clients[c];

        const size_t totalMsgCount = GenerateTxMessages(msgCountPerType, txTelemetryMessages, txImuMessages);

        std::vector<std::reference_wrapper<const ITxMessage>> txMessages;
        txMessages.reserve(totalMsgCount);
        for (size_t i = 0; i < msgCountPerType; ++i)
        {
            txMessages.push_back(txTelemetryMessages[i]);
            txMessages.push_back(txImuMessages[i]);
        }

        auto fr = server.ReadAsync<TelemetryMessage, ImuMessage>(server.GetChannel(c), totalMsgCount);
        auto fw = client.WriteAsync(client.GetChannel(0), txMessages);

        EXPECT_EQ(fw.get(), totalMsgCount);
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

        rxMessages.clear();

        fr = client.ReadAsync<TelemetryMessage, ImuMessage>(client.GetChannel(0), totalMsgCount);
        fw = server.WriteAsync(server.GetChannel(c), txMessages);

        EXPECT_EQ(fw.get(), totalMsgCount);
        rxMessages = fr.get();
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
}