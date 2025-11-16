#include <gtest/gtest.h>
#include "TestUtils.hpp"

using namespace ProtoComm;

TEST(ProtoCommTest, Read)
{
    CommStream<TestProtocol> stream;
    auto channel = stream.Start();

    auto rxMessages = stream.Read<GPGGAMessage>(channel, 100, std::chrono::milliseconds(100));
    EXPECT_EQ(rxMessages.size(), 3);
    if (rxMessages.size() != 3)
        return;

    EXPECT_DOUBLE_EQ(dynamic_cast<GPGGAMessage&>(*rxMessages[0]).latitude, 4807.038);
    EXPECT_DOUBLE_EQ(dynamic_cast<GPGGAMessage&>(*rxMessages[0]).longitude, 1131.000);

    EXPECT_DOUBLE_EQ(dynamic_cast<GPGGAMessage&>(*rxMessages[1]).latitude, 3723.247);
    EXPECT_DOUBLE_EQ(dynamic_cast<GPGGAMessage&>(*rxMessages[1]).longitude, 12202.518);

    EXPECT_DOUBLE_EQ(dynamic_cast<GPGGAMessage&>(*rxMessages[2]).latitude, 1563.247);
    EXPECT_DOUBLE_EQ(dynamic_cast<GPGGAMessage&>(*rxMessages[2]).longitude, 15001.518);
}