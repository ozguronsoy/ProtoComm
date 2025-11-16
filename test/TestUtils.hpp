#ifndef PROTOCOMM_TEST_UTILS_HPP
#define PROTOCOMM_TEST_UTILS_HPP

#include "ProtoComm.hpp"
#include <iostream>
#include <random>

using namespace ProtoComm;

class TelemetryMessage : public IRxMessage, public ITxMessage
{
public:
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

    float altitude = 0;
    float latitude = 0;
    float longitude = 0;
    float temperature = 0;
    float pressure = 0;
    uint8_t state = 0;
};

class ImuMessage : public IRxMessage, public ITxMessage
{
public:
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

    float ax = 0;
    float ay = 0;
    float az = 0;
    float gx = 0;
    float gy = 0;
    float gz = 0;
};

class NmeaFrameHandler : public FrameHandler
{
public:
    uint8_t CalculateChecksum(const IMessage& msg, std::span<const uint8_t> frame)
    {
        uint8_t checksum = 0;

        auto it = frame.begin() + 1;
        auto end = frame.end() - msg.FooterPattern().size() - 3;
        for (; it != end; ++it)
            checksum ^= *it;

        return checksum;
    }

    virtual bool Validate(const IRxMessage& msg, std::span<const uint8_t> frame) override
    {
        if (!FrameHandler::Validate(msg, frame))
            return false;

        if (frame.size() < 6)
            return false;

        auto itChecksum = frame.end() - 5;
        if ((*itChecksum) != '*')
            return false;
        itChecksum++;

        const uint8_t expected = static_cast<uint8_t>(std::stoi(std::string(itChecksum, itChecksum + 2), nullptr, 16));
        const uint8_t calculated = this->CalculateChecksum(msg, frame);

        return calculated == expected;
    }

    virtual void Seal(const ITxMessage& msg, std::span<uint8_t> frame) override
    {
        if (frame.size() < 6)
            throw std::logic_error("invalid frame size");

        // write header and footer
        FrameHandler::Seal(msg, frame);

        auto itChecksum = frame.end() - 5;
        (*itChecksum) = '*';

        const uint8_t checksum = this->CalculateChecksum(msg, frame);

        // convert the checksum byte to two hex chars
        std::stringstream ss;
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(checksum);
        std::string hexChars = ss.str();

        // 5. Write the two hex chars into the frame
        *(itChecksum + 1) = (uint8_t)std::toupper(hexChars[0]);
        *(itChecksum + 2) = (uint8_t)std::toupper(hexChars[1]);
    }

    // singleton that will be used by all nmea message instances
    static NmeaFrameHandler& Instance()
    {
        static NmeaFrameHandler instance;
        return instance;
    }
};

class GPGGAMessage : public IRxMessage
{
public:
    std::optional<size_t> FrameSize() const override
    {
        return std::nullopt;
    }

    std::span<const uint8_t> HeaderPattern() const override
    {
        static constexpr const std::array<uint8_t, 7> header = { '$', 'G', 'P', 'G', 'G', 'A', ',' };
        return header;
    }

    std::span<const uint8_t> FooterPattern() const override
    {
        static constexpr const std::array<uint8_t, 2> footer = { '\r', '\n' };
        return footer;
    }

    IFrameHandler& FrameHandler() const override
    {
        return NmeaFrameHandler::Instance();
    }

    std::unique_ptr<IMessage> Clone() const override
    {
        return std::make_unique<GPGGAMessage>(*this);
    }

    void Unpack(std::span<const uint8_t> frame) override
    {
        auto itPayloadBegin = frame.begin() + HeaderPattern().size();
        auto itPayloadEnd = std::find(itPayloadBegin, frame.end(), (uint8_t)'*');

        std::string payload(itPayloadBegin, itPayloadEnd);
        std::stringstream ss(payload);
        std::string segment;

        try
        {
            std::getline(ss, time, ',');

            std::getline(ss, segment, ',');
            latitude = std::stod(segment);
            std::getline(ss, segment, ',');
            latDirection = segment[0];

            std::getline(ss, segment, ',');
            longitude = std::stod(segment);
            std::getline(ss, segment, ',');
            lonDirection = segment[0];

            std::getline(ss, segment, ',');
            quality = std::stoi(segment);

            std::getline(ss, segment, ',');
            numSatellites = std::stoi(segment);

            std::getline(ss, segment, ','); // Skip HDOP

            std::getline(ss, segment, ',');
            altitude = std::stod(segment);
        }
        catch (const std::exception& e)
        {
            std::cerr << "Failed to unpack GPGGA: " << e.what() << std::endl;
        }
    }

    std::string time;
    double latitude = 0.0;
    char latDirection = 'N';
    double longitude = 0.0;
    char lonDirection = 'E';
    int quality = 0;
    int numSatellites = 0;
    double altitude = 0.0;
};

class TestProtocol final : public ICommProtocol
{
public:
    TestProtocol()
        : m_isRunning(false),
        m_sb(0)
    {
    }

    ~TestProtocol()
    {
        if (this->IsRunning())
            this->Stop();
    }

    size_t ChannelCount() const override
    {
        return (m_isRunning) ? (1) : (0);
    }

    size_t AvailableReadSize(ICommProtocol::ChannelId channelId) const override
    {
        if (!this->IsRunning(channelId)) return 0;
        if (m_sb == 0)
            return m_buffer1.size();
        else if (m_sb == 1)
            return m_buffer2.size();
        return 0;
    }

    bool IsRunning() const override
    {
        return m_isRunning;
    }

    bool IsRunning(ICommProtocol::ChannelId channelId) const override
    {
        return m_isRunning;
    }

    void SetChannelEventCallback(ChannelEventCallback callback) override
    {
        if (!callback)
            throw std::invalid_argument("callback cannot be null");
        m_channelEventCallback = callback;
    }

    std::optional<ICommProtocol::ChannelId> Start()
    {
        if (this->IsRunning())
            return std::nullopt;

        const ICommProtocol::ChannelId channelId = 0;
        m_isRunning = true;

        m_channelEventCallback(channelId, ICommProtocol::ChannelEventType::ChannelAdded);

        return channelId;
    }

    void Stop() override
    {
        m_isRunning = false;
    }

    void Stop(ICommProtocol::ChannelId channelId) override
    {
        m_isRunning = false;
    }

    size_t Read(ICommProtocol::ChannelId channelId, std::span<uint8_t> buffer) override
    {
        if (!this->IsRunning(channelId)) return 0;
        if (m_sb > 1) return 0;

        const std::string* rxBuffer = (m_sb == 0) ? (&m_buffer1) : (&m_buffer2);
        const size_t bytesToRead = std::min(buffer.size(), rxBuffer->size());
        std::copy(rxBuffer->begin(), rxBuffer->begin() + bytesToRead, buffer.begin());

        m_sb++;

        return bytesToRead;
    }

    void Write(ICommProtocol::ChannelId channelId, std::span<const uint8_t> buffer) override
    {
        throw std::logic_error("TestProtocol::Write is not implemented.");
    }

    void ReadAsync(ICommProtocol::ChannelId channelId, ICommProtocol::ReadCallback callback) override
    {
        throw std::logic_error("TestProtocol::ReadAsync is not implemented.");
    }

    void WriteAsync(ICommProtocol::ChannelId channelId, std::span<const uint8_t> buffer, ICommProtocol::WriteCallback callback) override
    {
        throw std::logic_error("TestProtocol::WriteAsync is not implemented.");
    }

private:
    ICommProtocol::ChannelEventCallback m_channelEventCallback;
    bool m_isRunning;

    int m_sb;
    std::string m_buffer1 =
        "fa61234521sdgasdfsdaxf$GPGGA,af411345dsafdsad*1A\r\nfsa"
        "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n"
        "gsad*47\r\nfasdfqwer$GPGGA,421352134fzdasgd326rasefdas*\r\nfes"
        "$GPGGA,154303,3723.247,N,12202.518,W,1,10,0.8,15.3,M,45.0,M,,*64\r\n"
        "641\r\n$GPGGA,f45ads64f5sad65acgdsfgasgsd645fa9/87944865arsdfasfd*3C\r\n$GPG";

    std::string m_buffer2 =
        "GA,154303,1563.247,N,15001.518,W,1,10,0.8,15.3,M,45.0,M,,*66\r\n"
        "fsdafdasgasdfasdfs\r\n";

};

static float GenerateRandomFloat()
{
    static std::mt19937 generator(std::random_device{}());
    static std::uniform_real_distribution<float> distribution(0.0f, 1000.0f);
    return distribution(generator);
}

static size_t GenerateTxMessages(
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