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

	AsioSerialProtocol::~AsioSerialProtocol()
	{
		if (this->IsRunning())
		{
			this->Stop();
		}
	}

	const AsioSerialProtocol::Channel& AsioSerialProtocol::GetChannel(size_t ch) const
	{
		if (ch >= m_channels.size())
			throw std::out_of_range("channel index out of range");

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
		if (ch >= m_channels.size())
			throw std::out_of_range("channel index out of range");

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
		return !m_channels.empty();
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

	size_t AsioSerialProtocol::Read(size_t ch, std::span<uint8_t> buffer)
	{
		if (ch >= m_channels.size())
			throw std::out_of_range("channel index out of range");

		auto it = m_channels.begin();
		std::advance(it, ch);

		if (!it->port.is_open())
			return 0;

		(void)asio::read(it->port, asio::buffer(buffer));
	}

	void AsioSerialProtocol::Write(size_t ch, std::span<const uint8_t> buffer)
	{
		if (ch >= m_channels.size())
			throw std::out_of_range("channel index out of range");

		auto it = m_channels.begin();
		std::advance(it, ch);

		if (!it->port.is_open())
			return;

		(void)asio::write(it->port, asio::buffer(buffer));
	}
}