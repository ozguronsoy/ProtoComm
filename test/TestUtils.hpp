#ifndef PROTOCOMM_TEST_UTILS_HPP
#define PROTOCOMM_TEST_UTILS_HPP

#include "ProtoComm.hpp"
#include <random>

using namespace ProtoComm;

class TelemetryMessage : public IRxMessage, public ITxMessage
{
public:
    float altitude = 0;
    float latitude = 0;
    float longitude = 0;
    float temperature = 0;
    float pressure = 0;
    uint8_t state = 0;

    bool operator==(const TelemetryMessage& rhs) const
    {
        return this->altitude == rhs.altitude &&
            this->latitude == rhs.latitude &&
            this->longitude == rhs.longitude &&
            this->temperature == rhs.temperature &&
            this->pressure == rhs.pressure &&
            this->state == rhs.state;
    }

    std::optional<size_t> FrameSize() const override
    {
        return 21 + HeaderPattern().size() + FooterPattern().size() + 1; // +1 for checksum
    }

    std::span<const uint8_t> HeaderPattern() const override
    {
        static constexpr const std::array<uint8_t, 2> header = { 0xF2, 0xF5 };
        return header;
    }

    std::span<const uint8_t> FooterPattern() const override
    {
        static constexpr const std::array<uint8_t, 2> footer = { 0x0D, 0x0A };
        return footer;
    }

    IFrameHandler& FrameHandler() const override
    {
        return ChecksumFrameHandler<>::Instance();
    }

    std::unique_ptr<IMessage> Clone() const override
    {
        return std::make_unique<TelemetryMessage>(*this);
    }

    void Unpack(std::span<const uint8_t> frame) override
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

    void Pack(std::vector<uint8_t>& frame) const override
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

class ImuMessage : public IRxMessage, public ITxMessage
{
public:
    float ax = 0;
    float ay = 0;
    float az = 0;
    float gx = 0;
    float gy = 0;
    float gz = 0;

    bool operator==(const ImuMessage& rhs) const
    {
        return this->ax == rhs.ax &&
            this->ay == rhs.ay &&
            this->az == rhs.az &&
            this->gx == rhs.gx &&
            this->gy == rhs.gy &&
            this->gz == rhs.gz;
    }

    std::optional<size_t> FrameSize() const override
    {
        return 24 + HeaderPattern().size() + FooterPattern().size() + 2; // +2 for checksum
    }

    std::span<const uint8_t> HeaderPattern() const override
    {
        static constexpr const std::array<uint8_t, 4> header = { 0xF7, 0xA5, 0x02, 0x3A };
        return header;
    }

    std::span<const uint8_t> FooterPattern() const override
    {
        static constexpr const std::array<uint8_t, 2> footer = { 0x2C, 0x73 };
        return footer;
    }

    IFrameHandler& FrameHandler() const override
    {
        return ChecksumFrameHandler<uint16_t>::Instance();
    }

    std::unique_ptr<IMessage> Clone() const override
    {
        return std::make_unique<ImuMessage>(*this);
    }

    void Unpack(std::span<const uint8_t> frame) override
    {
        auto it = frame.begin() + this->HeaderPattern().size();

        (void)std::memcpy(&ax, &(*it), sizeof(float));
        std::advance(it, sizeof(float));
        (void)std::memcpy(&ay, &(*it), sizeof(float));
        std::advance(it, sizeof(float));
        (void)std::memcpy(&az, &(*it), sizeof(float));
        std::advance(it, sizeof(float));

        (void)std::memcpy(&gx, &(*it), sizeof(float));
        std::advance(it, sizeof(float));
        (void)std::memcpy(&gy, &(*it), sizeof(float));
        std::advance(it, sizeof(float));
        (void)std::memcpy(&gz, &(*it), sizeof(float));
    }

    void Pack(std::vector<uint8_t>& frame) const override
    {
        auto it = frame.begin() + this->HeaderPattern().size();

        (void)std::memcpy(&(*it), &ax, sizeof(float));
        std::advance(it, sizeof(float));
        (void)std::memcpy(&(*it), &ay, sizeof(float));
        std::advance(it, sizeof(float));
        (void)std::memcpy(&(*it), &az, sizeof(float));
        std::advance(it, sizeof(float));

        (void)std::memcpy(&(*it), &gx, sizeof(float));
        std::advance(it, sizeof(float));
        (void)std::memcpy(&(*it), &gy, sizeof(float));
        std::advance(it, sizeof(float));
        (void)std::memcpy(&(*it), &gz, sizeof(float));
    }
};

float GenerateRandomFloat()
{
    static std::mt19937 generator(std::random_device{}());
    static std::uniform_real_distribution<float> distribution(0.0f, 1000.0f);
    return distribution(generator);
}

size_t GenerateTxMessages(
    size_t msgCountPerType,
    std::span<TelemetryMessage> telemetryMessages,
    std::span<ImuMessage> imuMessages)
{
    for (size_t i = 0; i < msgCountPerType; ++i)
    {
        telemetryMessages[i].altitude = GenerateRandomFloat();
        telemetryMessages[i].latitude = GenerateRandomFloat();
        telemetryMessages[i].longitude = GenerateRandomFloat();
        telemetryMessages[i].temperature = GenerateRandomFloat();
        telemetryMessages[i].pressure = GenerateRandomFloat();
        telemetryMessages[i].state = i;

        imuMessages[i].ax = GenerateRandomFloat();
        imuMessages[i].ay = GenerateRandomFloat();
        imuMessages[i].az = GenerateRandomFloat();
        imuMessages[i].gx = GenerateRandomFloat();
        imuMessages[i].gy = GenerateRandomFloat();
        imuMessages[i].gz = GenerateRandomFloat();
    }

    return msgCountPerType * 2;
}

#endif