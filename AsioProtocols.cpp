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
		port(ioCtx),
		strand(asio::make_strand(ioCtx))
	{
		id = std::hash<std::string>()(portName);
	}

	AsioSerialProtocol::~AsioSerialProtocol()
	{
		if (this->IsRunning())
			this->Stop();
	}

	const AsioSerialProtocol::Channel& AsioSerialProtocol::GetChannel(ICommProtocol::ChannelId channelId) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return this->FindChannel(channelId);
	}

	std::optional<std::reference_wrapper<const AsioSerialProtocol::Channel>> AsioSerialProtocol::GetChannel(const std::string& portName) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		auto it = std::find_if(m_channels.cbegin(), m_channels.cend(), [&portName](const Channel& ch) { return ch.portName == portName; });
		if (it == m_channels.cend())
			return std::nullopt;
		return *it;
	}

	size_t AsioSerialProtocol::ChannelCount() const
	{
		return m_channels.size();
	}

	size_t AsioSerialProtocol::AvailableReadSize(ICommProtocol::ChannelId channelId) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		Channel& ch = this->FindChannel(channelId);
		if (!ch.port.is_open())
			return 0;

		int bytesAvailable = 0;

#ifdef _WIN32

		COMSTAT comStat;
		DWORD errors;
		if (ClearCommError(ch.port.native_handle(), &errors, &comStat))
			bytesAvailable = static_cast<int>(comStat.cbInQue);
		else
			return 0;

#else

		if (ioctl(ch.port.native_handle(), FIONREAD, &bytesAvailable) < 0)
			return 0;

#endif
		return static_cast<size_t>(bytesAvailable);
	}

	bool AsioSerialProtocol::IsRunning() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		return std::find_if(m_channels.begin(), m_channels.end(), [](auto& channel) { return channel.port.is_open(); }) != m_channels.end();
	}

	bool AsioSerialProtocol::IsRunning(ICommProtocol::ChannelId channelId) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return this->FindChannel(channelId).port.is_open();
	}

	void AsioSerialProtocol::SetChannelEventCallback(ICommProtocol::ChannelEventCallback callback)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		if (!callback)
			throw std::invalid_argument("channel event callback cannot be null");
		m_channelEventCallback = callback;
	}

	std::optional<ICommProtocol::ChannelId> AsioSerialProtocol::Start(
		const std::string& portName,
		asio::serial_port_base::baud_rate baudRate,
		asio::serial_port_base::character_size dataBits,
		asio::serial_port_base::stop_bits stopBits,
		asio::serial_port_base::parity parity,
		asio::serial_port_base::flow_control flowControl)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		if (!m_channelEventCallback)
			throw std::logic_error("channel event callback must be set before starting the protocol");

		if (std::find_if(m_channels.begin(), m_channels.end(), [&portName](const Channel& channel) { return channel.portName == portName; }) != m_channels.end())
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

			m_channelEventCallback(ch.id, true);

			return ch.id;
		}
		catch (const std::exception&)
		{
			m_channels.pop_back();
			return std::nullopt;
		}
	}

	void AsioSerialProtocol::Stop()
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		for (auto& ch : m_channels)
		{
			if (ch.port.is_open())
			{
				Channel* pChannel = &ch;
				asio::post(m_ioCtx,
					asio::bind_executor(ch.strand,
						[this, pChannel]()
						{
							std::lock_guard<std::mutex> lock(m_mutex);

							auto it = std::find_if(m_channels.begin(), m_channels.end(), [pChannel](const Channel& ch) { return &ch == pChannel; });
							if (it != m_channels.end())
							{
								m_channelEventCallback(pChannel->id, false);
								pChannel->port.close();
								(void)m_channels.erase(it);
							}
						}));
			}
		}

		(void)m_ioThreads.emplace_back(&AsioSerialProtocol::RunIoContext, this);
	}

	void AsioSerialProtocol::Stop(ICommProtocol::ChannelId channelId)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		Channel& ch = this->FindChannel(channelId);
		Channel* pChannel = &ch;
		if (ch.port.is_open())
		{
			asio::post(m_ioCtx,
				asio::bind_executor(ch.strand,
					[this, pChannel]()
					{
						std::lock_guard<std::mutex> lock(m_mutex);

						auto it = std::find_if(m_channels.begin(), m_channels.end(), [pChannel](const Channel& ch) { return &ch == pChannel; });
						if (it != m_channels.end())
						{
							m_channelEventCallback(pChannel->id, false);
							pChannel->port.close();
							(void)m_channels.erase(it);
						}
					}));

			(void)m_ioThreads.emplace_back(&AsioSerialProtocol::RunIoContext, this);
		}
	}

	size_t AsioSerialProtocol::Read(ICommProtocol::ChannelId channelId, std::span<uint8_t> buffer)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		Channel& ch = this->FindChannel(channelId);
		if (!ch.port.is_open())
			return 0;
		return asio::read(ch.port, asio::buffer(buffer));
	}

	void AsioSerialProtocol::ReadAsync(ICommProtocol::ChannelId channelId, ICommProtocol::ReadCallback callback)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		if (!callback)
			throw std::invalid_argument("callback cannot be null");

		Channel& ch = this->FindChannel(channelId);
		if (!ch.port.is_open())
		{
			callback(std::make_error_code(std::errc::not_connected), ch.id, std::span<const uint8_t>{});
			return;
		}

		auto readBuffer = std::make_shared<std::vector<uint8_t>>(1024);
		ch.port.async_read_some(asio::buffer(*readBuffer),
			asio::bind_executor(ch.strand,
				[channelId, callback, readBuffer](const std::error_code& ec, size_t size)
				{
					callback(ec, channelId, std::span<const uint8_t>(readBuffer->begin(), size));
				}));

		(void)m_ioThreads.emplace_back(&AsioSerialProtocol::RunIoContext, this);
	}

	void AsioSerialProtocol::Write(ICommProtocol::ChannelId channelId, std::span<const uint8_t> buffer)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		if (buffer.empty())
			return;

		Channel& ch = this->FindChannel(channelId);
		if (!ch.port.is_open())
			return;
		(void)asio::write(ch.port, asio::buffer(buffer));
	}

	void AsioSerialProtocol::WriteAsync(ICommProtocol::ChannelId channelId, std::span<const uint8_t> buffer, ICommProtocol::WriteCallback callback)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		if (!callback)
			throw std::invalid_argument("callback cannot be null");

		Channel& ch = this->FindChannel(channelId);
		if (!ch.port.is_open())
		{
			callback(std::make_error_code(std::errc::not_connected), ch.id, 0);
			return;
		}

		asio::async_write(ch.port, asio::buffer(buffer),
			asio::bind_executor(ch.strand,
				[channelId, callback](const asio::error_code& ec, size_t size)
				{
					callback(ec, channelId, size);
				}));

		(void)m_ioThreads.emplace_back(&AsioSerialProtocol::RunIoContext, this);
	}

	AsioSerialProtocol::Channel& AsioSerialProtocol::FindChannel(ICommProtocol::ChannelId channelId) const
	{
		auto it = std::find_if(m_channels.begin(), m_channels.end(), [&channelId](const Channel& ch) { return ch.id == channelId; });
		if (it == m_channels.end())
			throw std::invalid_argument(std::format("channel with the id '{}' not found.", channelId));
		return *it;
	}

	void AsioSerialProtocol::RunIoContext()
	{
		(void)m_ioCtx.run();
	}
}