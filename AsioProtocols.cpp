#include "AsioProtocols.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <termios.h>
#include <sys/ioctl.h>
#endif

namespace ProtoComm
{
	AsioSerialProtocol::Channel::Channel(asio::io_context& ioCtx, const std::string& portName)
		: portName(portName),
		port(ioCtx)
	{
	}

	AsioSerialProtocol::AsioSerialProtocol()
		: m_runIoThread(true)
	{
		m_ioThread = std::thread(
			[this]()
			{
				while (m_runIoThread)
				{
					(void)m_ioCtx.run();
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			});
	}

	AsioSerialProtocol::~AsioSerialProtocol()
	{
		if (this->IsRunning())
			this->Stop();

		m_runIoThread = false;
		m_ioCtx.stop();
		if (m_ioThread.joinable())
			m_ioThread.join();
	}

	const AsioSerialProtocol::Channel& AsioSerialProtocol::GetChannel(size_t ch) const
	{
		if (ch >= this->ChannelCount())
			throw std::out_of_range(std::format("invalid channel index: {}", ch));

		auto it = m_channels.cbegin();
		std::advance(it, ch);

		return *it;
	}

	std::optional<std::reference_wrapper<const AsioSerialProtocol::Channel>> AsioSerialProtocol::GetChannel(const std::string& portName) const
	{
		auto it = std::find_if(m_channels.cbegin(), m_channels.cend(),
			[&portName](const Channel& ch) {
				return ch.portName == portName;
			});

		if (it == m_channels.cend())
			return std::nullopt;
		return *it;
	}

	size_t AsioSerialProtocol::ChannelCount() const
	{
		return m_channels.size();
	}

	size_t AsioSerialProtocol::AvailableReadSize(size_t ch) const
	{
		if (ch >= this->ChannelCount())
			throw std::out_of_range(std::format("invalid channel index: {}", ch));

		auto it = m_channels.begin();
		std::advance(it, ch);

		if (!it->port.is_open())
			return 0;

		int bytes_available = 0;

#ifdef _WIN32

		COMSTAT comStat;
		DWORD errors;
		if (ClearCommError(it->port.native_handle(), &errors, &comStat))
			bytes_available = static_cast<int>(comStat.cbInQue);
		else
			return 0;

#else

		if (ioctl(it->port.native_handle(), FIONREAD, &bytes_available) < 0)
			return 0;

#endif
		return static_cast<size_t>(bytes_available);
	}

	bool AsioSerialProtocol::IsRunning() const
	{
		return std::find_if(m_channels.begin(), m_channels.end(),
			[](auto& channel) {
				return channel.port.is_open();
			}) != m_channels.end();
	}

	bool AsioSerialProtocol::IsRunning(size_t ch) const
	{
		if (ch >= this->ChannelCount())
			throw std::out_of_range(std::format("invalid channel index: {}", ch));

		return std::next(m_channels.begin(), ch)->port.is_open();
	}

	bool AsioSerialProtocol::Start(
		const std::string& portName,
		asio::serial_port_base::baud_rate baudRate,
		asio::serial_port_base::character_size dataBits,
		asio::serial_port_base::stop_bits stopBits,
		asio::serial_port_base::parity parity,
		asio::serial_port_base::flow_control flowControl)
	{
		if (this->GetChannel(portName).has_value())
			return false;

		Channel& ch = m_channels.emplace_back(m_ioCtx, portName);
		try
		{
			ch.port.open(portName);

			ch.port.set_option(baudRate);
			ch.port.set_option(dataBits);
			ch.port.set_option(stopBits);
			ch.port.set_option(parity);
			ch.port.set_option(flowControl);

			return true;
		}
		catch (const std::exception&)
		{
			m_channels.pop_back();
			return false;
		}
	}

	void AsioSerialProtocol::Stop()
	{
		for (auto& ch : m_channels)
			if (ch.port.is_open())
				ch.port.close();
		m_channels.clear();
	}

	void AsioSerialProtocol::Stop(size_t ch)
	{
		if (ch >= this->ChannelCount())
			throw std::out_of_range(std::format("invalid channel index: {}", ch));

		(void)m_channels.erase(std::next(m_channels.begin(), ch));
	}

	size_t AsioSerialProtocol::Read(size_t ch, std::span<uint8_t> buffer)
	{
		if (ch >= this->ChannelCount())
			throw std::out_of_range(std::format("invalid channel index: {}", ch));

		auto it = m_channels.begin();
		std::advance(it, ch);

		if (!it->port.is_open())
			return 0;

		return asio::read(it->port, asio::buffer(buffer));
	}

	void AsioSerialProtocol::ReadAsync(size_t ch, ProtocolReadCallback callback)
	{
		if (ch >= this->ChannelCount())
			throw std::out_of_range(std::format("invalid channel index: {}", ch));

		auto it = m_channels.begin();
		std::advance(it, ch);

		if (!it->port.is_open())
		{
			callback(std::make_error_code(std::errc::not_connected), 0, std::span<const uint8_t>{});
			return;
		}

		auto readBuffer = std::make_shared<std::vector<uint8_t>>(1024);
		it->port.async_read_some(asio::buffer(*readBuffer),
			[ch, callback, readBuffer](const std::error_code& ec, size_t size)
			{
				callback(ec, ch, std::span<const uint8_t>(readBuffer->begin(), size));
			});
	}

	void AsioSerialProtocol::Write(size_t ch, std::span<const uint8_t> buffer)
	{
		if (ch >= this->ChannelCount())
			throw std::out_of_range(std::format("invalid channel index: {}", ch));

		auto it = m_channels.begin();
		std::advance(it, ch);

		if (!it->port.is_open())
			return;

		(void)asio::write(it->port, asio::buffer(buffer));
	}
}