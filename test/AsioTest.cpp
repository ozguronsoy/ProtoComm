#include <gtest/gtest.h>
#include <random>
#include "ProtoComm.hpp"
#include "AsioProtocols.hpp"

using namespace ProtoComm;

class MyMessage : public IRxMessage, public ITxMessage
{
public:
    float altitude = 0;
    float latitude = 0;
    float longitude = 0;
    float temperature = 0;
    float pressure = 0;
    uint8_t state = 0;

    bool operator==(const MyMessage& rhs) const
    {
        return this->altitude == rhs.altitude &&
            this->latitude == rhs.latitude &&
            this->longitude == rhs.longitude &&
            this->temperature == rhs.temperature &&
            this->pressure == rhs.pressure &&
            this->state == rhs.state;
    }

    std::optional<size_t> FrameSize() const
    {
        return 21 + HeaderPattern().size() + FooterPattern().size() + 1; // +1 for checksum
    }

    std::span<const uint8_t> HeaderPattern() const
    {
        static constexpr const std::array<uint8_t, 2> header = { 0xF2, 0xF5 };
        return header;
    }

    std::span<const uint8_t> FooterPattern() const
    {
        static constexpr const std::array<uint8_t, 2> footer = { 0x0D, 0x0A };
        return footer;
    }

    void Unpack(std::span<const uint8_t> frame)
    {
        auto it = frame.begin() + this->HeaderPattern().size();

        (void)std::memcpy(&altitude, &(*it), sizeof(float));
        std::advance(it, sizeof(float));

        (void)std::memcpy(&latitude, &(*it), sizeof(float));
        std::advance(it, sizeof(float));
        (void)std::memcpy(&longitude, &(*it), sizeof(float));
        std::advance(it, sizeof(float));

        (void)std::memcpy(&temperature, &(*it), sizeof(float));
        std::advance(it, sizeof(float));
        (void)std::memcpy(&pressure, &(*it), sizeof(float));
        std::advance(it, sizeof(float));

        state = *it;
    }

    void Pack(std::vector<uint8_t>& frame) const
    {
        auto it = frame.begin() + this->HeaderPattern().size();

        (void)std::memcpy(&(*it), &altitude, sizeof(float));
        std::advance(it, sizeof(float));

        (void)std::memcpy(&(*it), &latitude, sizeof(float));
        std::advance(it, sizeof(float));
        (void)std::memcpy(&(*it), &longitude, sizeof(float));
        std::advance(it, sizeof(float));

        (void)std::memcpy(&(*it), &temperature, sizeof(float));
        std::advance(it, sizeof(float));
        (void)std::memcpy(&(*it), &pressure, sizeof(float));
        std::advance(it, sizeof(float));

        (*it) = state;
    }
};

float random_float_0_100() {
    static std::mt19937 generator(std::random_device{}());
    static std::uniform_real_distribution<float> distribution(0.0f, 100.0f);
    return distribution(generator);
}

TEST(ProtoCommTest, Asio_SerialLoopback)
{
    using SerialStream = CommStream<AsioSerialProtocol, MyMessage, MyMessage, ChecksumFrameHandler<>>;

    constexpr const size_t msgCount = 10;
    constexpr const size_t chWrite = 0;
    constexpr const size_t chRead = 1;

    SerialStream serialStream;

    EXPECT_TRUE(serialStream.Start("/dev/ttyS10", asio::serial_port_base::baud_rate(9600)));
    EXPECT_TRUE(serialStream.Start("/dev/ttyS11", asio::serial_port_base::baud_rate(9600)));

    std::vector<MyMessage> txMessages(msgCount);
    for (size_t i = 0; i < msgCount; ++i)
    {
        txMessages[i].altitude = random_float_0_100();
        txMessages[i].latitude = random_float_0_100();
        txMessages[i].longitude = random_float_0_100();
        txMessages[i].temperature = random_float_0_100();
        txMessages[i].pressure = random_float_0_100();
        txMessages[i].state = i;
    }

    serialStream.Write(chWrite, txMessages);

    auto rxMessages = serialStream.Read(chRead, msgCount, std::chrono::milliseconds(5000));

    EXPECT_EQ(rxMessages.size(), msgCount);

    for (size_t i = 0; i < msgCount; ++i)
        EXPECT_EQ(txMessages[i], rxMessages[i]);

}