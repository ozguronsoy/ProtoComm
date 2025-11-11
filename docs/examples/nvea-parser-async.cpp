#include <iostream>
#include <sstream>
#include <deque>

#include "ProtoComm.hpp"

using namespace ProtoComm;

/*
Implement a custom frame handler for NMEA checksums

This handler inherits from ChecksumFrameHandler to get header/footer logic, and checksum calculation.
but overrides Validate and Seal to correctly handle the NMEA-style
8-bit XOR checksum, which is represented as two ASCII hex characters.

*/
class NmeaFrameHandler : public FrameHandler
{
public:
	uint8_t CalculateChecksum(const IMessage& msg, std::span<const uint8_t> frame)
	{
		uint8_t checksum = 0;

		auto it = frame.begin() + 1; // only skip '$' of header
		auto end = frame.end() - msg.FooterPattern().size() - 3; // *XX
		for (; it != end; ++it)
			checksum ^= *it;

		return checksum;
	}

	virtual bool Validate(const IRxMessage& msg, std::span<const uint8_t> frame) override
	{
		// check header and footer
		if (!FrameHandler::Validate(msg, frame))
			return false;

		if (frame.size() < 6)
			return false;

		// find the checksum delimiter '*'
		auto itChecksum = frame.end() - 5;
		if ((*itChecksum) != '*')
			return false;
		itChecksum++; // get to checksum

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
	// NMEA is variable-size
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

	// --- IRxMessage Contract ---
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

class LatLonMessage : public ITxMessage
{
public:
	std::optional<size_t> FrameSize() const override
	{
		return std::nullopt;
	}

	std::span<const uint8_t> HeaderPattern() const override
	{
		static constexpr const std::array<uint8_t, 7> header = { '$', 'L', 'A', 'T', 'L', 'N', ',' };
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
		return std::make_unique<LatLonMessage>(*this);
	}

	void Pack(std::vector<uint8_t>& frame) const override
	{
		std::stringstream ss;
		ss.precision(4);
		ss << std::fixed << latitude << "," << longitude;
		std::string payload = ss.str();

		// a variable-sized message must resize the frame.
		const size_t totalSize = HeaderPattern().size() + payload.length() + 5;
		frame.resize(totalSize);

		// copy payload after the header, frame handler will fill the rest (header, footer, '*', checksum)
		auto it = frame.begin() + HeaderPattern().size();
		it = std::copy(payload.begin(), payload.end(), it);
	}

	double latitude = 0.0;
	double longitude = 0.0;
};

/*

Implement a custom comm protocol.

*/
class CustomProtocol final : public ICommProtocol
{
public:
	CustomProtocol()
		: m_isRunning(false)
	{
	}

	~CustomProtocol()
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
		return m_rxBuffer.size();
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

		// this function must be invoked when a channel is added or removed.
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
		throw std::logic_error("LoopbackProtocol::Read is not implemented.");
	}

	void Write(ICommProtocol::ChannelId channelId, std::span<const uint8_t> buffer) override
	{
		throw std::logic_error("LoopbackProtocol::Write is not implemented.");
	}

	// asynchronous io not implemented for this example
	void ReadAsync(ICommProtocol::ChannelId channelId, ICommProtocol::ReadCallback callback) override
	{
		if (!this->IsRunning(channelId) || !callback) return;
		std::thread t(
			[this, channelId, callback]()
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1000));
				std::vector<uint8_t> buffer(m_rxBuffer.size());
				std::copy(m_rxBuffer.begin(), m_rxBuffer.end(), buffer.begin());
				m_rxBuffer.clear();
				callback(std::error_code(0, std::generic_category()), channelId, buffer);
			});
		t.detach();
	}

	void WriteAsync(ICommProtocol::ChannelId channelId, std::span<const uint8_t> buffer, ICommProtocol::WriteCallback callback) override
	{
		if (!this->IsRunning(channelId) || !callback) return;

		std::thread t(
			[this, channelId, buffer, callback]()
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
				std::cout << std::string(buffer.begin(), buffer.end()) << std::endl;
				callback(std::error_code(0, std::generic_category()), channelId, buffer.size());
			});
		t.detach();
	}

private:
	ICommProtocol::ChannelEventCallback m_channelEventCallback;
	bool m_isRunning;

	std::string m_rxBuffer =
		"fa61234521sdgasdfsdaxf$GPGGA,af411345dsafdsad*1A\r\nfsa" // random noise
		"$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n"
		"gsad*47\r\nfasdfqwer$GPGGA,421352134fzdasgd326rasefdas*\r\nfes" // random noise
		"$GPGGA,154303,3723.247,N,12202.518,W,1,10,0.8,15.3,M,45.0,M,,*64\r\n"
		"641\r\n$GPGGA,f45ads64f5sad65acgdsfgasgsd645fa9/87944865arsdfasfd*3C\r\n"; // random noise
};

double ConvertLatLong(double degreesMinutes)
{
	int degrees = static_cast<int>(degreesMinutes) / 100;
	double minutes = degreesMinutes - degrees * 100;
	return degrees + (minutes / 60.0);
}

int main()
{
	CommStream<CustomProtocol> stream;
	auto ch = stream.Start();

	if (!stream.IsRunning(ch))
	{
		std::cerr << "failed to start the stream";
		return 1;
	}

	auto readFuture = stream.ReadAsync<GPGGAMessage>(ch, 2);
	auto rxMessages = readFuture.get();
	std::cout << std::format("received message count: {}", rxMessages.size()) << std::endl;

	std::vector<LatLonMessage> latlonMessages;
	for (size_t i = 0; i < rxMessages.size(); ++i)
	{
		const GPGGAMessage& msg = *dynamic_cast<GPGGAMessage*>(rxMessages[i].get());

		std::cout
			<< std::format("Message[{}]: ", i) << std::endl
			<< std::format(
				"\ttime: {}\n"
				"\tlatitude: {} {}\n"
				"\tlongitude: {} {}\n"
				"\tquality: {}\n"
				"\t#sat: {}\n"
				"\taltitude: {}\n",
				msg.time,
				msg.latitude, msg.latDirection,
				msg.longitude, msg.lonDirection,
				msg.quality, msg.numSatellites, msg.altitude)
			<< std::endl;

		LatLonMessage& txMessage = latlonMessages.emplace_back();
		txMessage.latitude = ConvertLatLong(msg.latitude);
		txMessage.longitude = ConvertLatLong(msg.longitude);
	}

	// create a vector that WriteAsync can get
	std::vector<std::reference_wrapper<const ITxMessage>> txMessages;
	txMessages.reserve(latlonMessages.size());
	for (const auto& latlonMsg : latlonMessages)
		txMessages.push_back(std::cref(latlonMsg));

	std::cout << std::format("writing '{}' messages", txMessages.size()) << std::endl << std::endl;
	auto writeFuture = stream.WriteAsync(ch, txMessages);
	const size_t writtenCount = writeFuture.get();
	std::cout << std::format("written '{}' messages", writtenCount) << std::endl;

	std::cout << "Press any key to exit" << std::endl;
	std::string _s;
	std::cin >> _s;

	return 0;
}