#include "AsioProtocols.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <termios.h>
#include <sys/ioctl.h>
#endif

namespace ProtoComm
{

#pragma region Serial

	AsioSerialProtocol::Channel::Channel(asio::io_context& ioCtx, const std::string& portName)
		: portName(portName),
		port(ioCtx),
		strand(asio::make_strand(ioCtx))
	{
		id = std::hash<std::string>()(portName);
	}

	AsioSerialProtocol::AsioSerialProtocol()
		: m_disposing(false),
		m_workGuard(asio::make_work_guard(m_ioCtx))
	{
	}

	AsioSerialProtocol::~AsioSerialProtocol()
	{
		m_disposing = true;
		if (this->IsRunning())
			this->Stop();
		m_workGuard.reset();
	}

	const asio::serial_port& AsioSerialProtocol::Port(ICommProtocol::ChannelId channelId) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return this->FindChannel(channelId).port;
	}

	std::optional<std::reference_wrapper<const asio::serial_port>> AsioSerialProtocol::Port(const std::string& name) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		auto it = std::find_if(m_channels.cbegin(), m_channels.cend(), [&name](const Channel& ch) { return ch.portName == name; });
		if (it == m_channels.cend())
			return std::nullopt;
		return it->port;
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

			m_channelEventCallback(ch.id, ICommProtocol::ChannelEventType::ChannelAdded);

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
				asio::post(m_ioCtx,
					asio::bind_executor(ch.strand,
						[this, &ch]()
						{

							m_channelEventCallback(ch.id, ICommProtocol::ChannelEventType::ChannelRemoved);
							ch.port.close();

							std::lock_guard<std::mutex> lock(m_mutex);
							(void)m_channels.erase(std::find_if(m_channels.begin(), m_channels.end(), [&ch](const Channel& rhs) { return &ch == &rhs; }));
						}));
				(void)m_ioThreads.emplace_back(&AsioSerialProtocol::RunIoContext, this);
			}
		}
	}

	void AsioSerialProtocol::Stop(ICommProtocol::ChannelId channelId)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		Channel& ch = this->FindChannel(channelId);
		if (ch.port.is_open())
		{
			asio::post(m_ioCtx,
				asio::bind_executor(ch.strand,
					[this, &ch]()
					{

						m_channelEventCallback(ch.id, ICommProtocol::ChannelEventType::ChannelRemoved);
						ch.port.close();

						std::lock_guard<std::mutex> lock(m_mutex);
						(void)m_channels.erase(std::find_if(m_channels.begin(), m_channels.end(), [&ch](const Channel& rhs) { return &ch == &rhs; }));
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

		std::lock_guard<std::mutex> lock(m_removeThreadMutex);
		m_threadIdsToRemove.push_back(std::this_thread::get_id());
	}

	void AsioSerialProtocol::CleanFinishedThreads()
	{
		while (!m_disposing)
		{
			std::lock_guard<std::mutex> l1(m_removeThreadMutex);
			if (!m_threadIdsToRemove.empty())
			{
				std::lock_guard<std::mutex> l2(m_mutex);
				for (const auto& tid : m_threadIdsToRemove)
				{
					auto it = std::find_if(m_ioThreads.begin(), m_ioThreads.end(), [tid](const std::jthread& t) { return t.get_id() == tid; });
					if (it != m_ioThreads.end())
						(void)m_ioThreads.erase(it);
				}
				m_threadIdsToRemove.clear();
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}

#pragma endregion Serial

#pragma region TCP Client

	AsioTcpClient::AsioTcpClient()
		: m_socket(m_ioCtx),
		m_strand(asio::make_strand(m_ioCtx)),
		m_workGuard(asio::make_work_guard(m_ioCtx))
	{
	}

	AsioTcpClient::~AsioTcpClient()
	{
		if (this->IsRunning())
			this->Stop();
		m_workGuard.reset();
	}

	const asio::ip::tcp::socket& AsioTcpClient::Socket() const
	{
		return m_socket;
	}

	size_t AsioTcpClient::ChannelCount() const
	{
		return (this->IsRunning()) ? (1) : (0);
	}

	size_t AsioTcpClient::AvailableReadSize(ICommProtocol::ChannelId channelId) const
	{
		return (this->IsRunning()) ? (m_socket.available()) : (0);
	}

	bool AsioTcpClient::IsRunning() const
	{
		return m_socket.is_open();
	}

	bool AsioTcpClient::IsRunning(ICommProtocol::ChannelId channelId) const
	{
		return this->IsRunning();
	}

	void AsioTcpClient::SetChannelEventCallback(ICommProtocol::ChannelEventCallback callback)
	{
		if (!callback)
			throw std::invalid_argument("channel event callback cannot be null");
		m_channelEventCallback = callback;
	}

	std::optional<ICommProtocol::ChannelId> AsioTcpClient::Start(const std::string& host, const std::string& port)
	{
		if (this->IsRunning())
			return std::nullopt;

		try
		{
			asio::ip::tcp::resolver resolver(m_ioCtx);
			auto endpoints = resolver.resolve(host, port);
			(void)asio::connect(m_socket, endpoints);
			m_channelEventCallback(k_channelId, ICommProtocol::ChannelEventType::ChannelAdded);
			return k_channelId;
		}
		catch (const std::exception& e)
		{
			if (m_socket.is_open())
				m_socket.close();
			return std::nullopt;
		}
	}

	void AsioTcpClient::Stop()
	{
		if (this->IsRunning())
		{
			asio::post(m_ioCtx,
				asio::bind_executor(m_strand,
					[this]()
					{
						m_channelEventCallback(k_channelId, ICommProtocol::ChannelEventType::ChannelRemoved);
						m_socket.close();
					}));
			(void)m_ioThreads.emplace_back(&AsioTcpClient::RunIoContext, this);
		}
	}

	void AsioTcpClient::Stop(ICommProtocol::ChannelId channelId)
	{
		this->Stop();
	}

	size_t AsioTcpClient::Read(ICommProtocol::ChannelId channelId, std::span<uint8_t> buffer)
	{
		if (!this->IsRunning())
			return 0;
		return asio::read(m_socket, asio::buffer(buffer));
	}

	void AsioTcpClient::ReadAsync(ICommProtocol::ChannelId channelId, ICommProtocol::ReadCallback callback)
	{
		if (!callback)
			throw std::invalid_argument("callback cannot be null");

		if (!this->IsRunning())
		{
			callback(std::make_error_code(std::errc::not_connected), k_channelId, std::span<const uint8_t>{});
			return;
		}

		auto readBuffer = std::make_shared<std::vector<uint8_t>>(1024);
		m_socket.async_read_some(asio::buffer(*readBuffer),
			asio::bind_executor(m_strand,
				[channelId, callback, readBuffer](const std::error_code& ec, size_t size)
				{
					callback(ec, channelId, std::span<const uint8_t>(readBuffer->begin(), size));
				}));
		(void)m_ioThreads.emplace_back(&AsioTcpClient::RunIoContext, this);
	}

	void AsioTcpClient::Write(ICommProtocol::ChannelId channelId, std::span<const uint8_t> buffer)
	{
		if (!this->IsRunning())
			return;
		(void)asio::write(m_socket, asio::buffer(buffer));
	}

	void AsioTcpClient::WriteAsync(ICommProtocol::ChannelId channelId, std::span<const uint8_t> buffer, ICommProtocol::WriteCallback callback)
	{
		if (!callback)
			throw std::invalid_argument("callback cannot be null");

		if (!this->IsRunning())
		{
			callback(std::make_error_code(std::errc::not_connected), k_channelId, 0);
			return;
		}

		asio::async_write(m_socket, asio::buffer(buffer),
			asio::bind_executor(m_strand,
				[channelId, callback](const asio::error_code& ec, size_t size)
				{
					callback(ec, channelId, size);
				}));
		(void)m_ioThreads.emplace_back(&AsioTcpClient::RunIoContext, this);
	}

	void AsioTcpClient::RunIoContext()
	{
		(void)m_ioCtx.run();
	}

#pragma endregion TCP Client

#pragma region TCP Server

	AsioTcpServer::Channel::Channel(asio::io_context& ioCtx, const std::string& fullAddr)
		: socket(ioCtx),
		strand(asio::make_strand(ioCtx))
	{
		id = std::hash<std::string>()(fullAddr);
	}

	AsioTcpServer::AsioTcpServer()
		: m_disposing(false),
		m_acceptor(m_ioCtx),
		m_strand(asio::make_strand(m_ioCtx)),
		m_workGuard(asio::make_work_guard(m_ioCtx))
	{
		(void)m_ioThreads.emplace_back(&AsioTcpServer::CleanFinishedThreads, this);
	}

	AsioTcpServer::~AsioTcpServer()
	{
		m_disposing = true;
		if (this->IsRunning())
			this->Stop();
		m_workGuard.reset();
	}

	size_t AsioTcpServer::ChannelCount() const
	{
		return m_channels.size();
	}

	size_t AsioTcpServer::AvailableReadSize(ICommProtocol::ChannelId channelId) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		const Channel& ch = this->FindChannel(channelId);
		return ch.socket.available();
	}

	bool AsioTcpServer::IsRunning() const
	{
		return m_acceptor.is_open();
	}

	bool AsioTcpServer::IsRunning(ICommProtocol::ChannelId channelId) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		const Channel& ch = this->FindChannel(channelId);
		return ch.socket.is_open();
	}

	void AsioTcpServer::SetChannelEventCallback(ICommProtocol::ChannelEventCallback callback)
	{
		if (!callback)
			throw std::invalid_argument("channel event callback cannot be null");
		m_channelEventCallback = callback;
	}

	std::optional<ICommProtocol::ChannelId> AsioTcpServer::Start(const std::string& host, const std::string& port)
	{
		if (!this->IsRunning())
		{
			try
			{
				asio::ip::tcp::resolver resolver(m_ioCtx);
				asio::ip::tcp::endpoint ep = resolver.resolve(host, port)->endpoint();

				m_acceptor.open(ep.protocol());
				m_acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
				m_acceptor.bind(ep);
				m_acceptor.listen(asio::socket_base::max_listen_connections);

				this->RunAcceptor();
			}
			catch (const std::exception&)
			{
				if (m_acceptor.is_open())
					m_acceptor.close();
			}
		}

		return std::nullopt;
	}

	void AsioTcpServer::Stop()
	{
		if (this->IsRunning())
		{
			std::lock_guard<std::mutex> lock(m_mutex);

			m_acceptor.close();
			for (Channel& ch : m_channels)
			{
				if (ch.socket.is_open())
				{
					asio::post(
						asio::bind_executor(ch.strand,
							[this, &ch]()
							{
								m_channelEventCallback(ch.id, ICommProtocol::ChannelEventType::ChannelRemoved);
								ch.socket.close();

								std::lock_guard<std::mutex> lock(m_mutex);
								(void)m_channels.erase(std::find_if(m_channels.begin(), m_channels.end(), [&ch](const Channel& rhs) { return &ch == &rhs; }));
							}));
					(void)m_ioThreads.emplace_back(&AsioTcpServer::RunIoContext, this);
				}
			}
		}
	}

	void AsioTcpServer::Stop(ICommProtocol::ChannelId channelId)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		Channel& ch = this->FindChannel(channelId);
		if (ch.socket.is_open())
		{
			asio::post(
				asio::bind_executor(m_strand,
					[this, &ch]()
					{
						m_channelEventCallback(ch.id, ICommProtocol::ChannelEventType::ChannelRemoved);
						ch.socket.close();


						std::lock_guard<std::mutex> lock(m_mutex);
						(void)m_channels.erase(std::find_if(m_channels.begin(), m_channels.end(), [&ch](const Channel& rhs) { return &ch == &rhs; }));
					}));
			(void)m_ioThreads.emplace_back(&AsioTcpServer::RunIoContext, this);
		}
	}

	size_t AsioTcpServer::Read(ICommProtocol::ChannelId channelId, std::span<uint8_t> buffer)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		Channel& ch = this->FindChannel(channelId);
		if (!ch.socket.is_open())
			return 0;
		return asio::read(ch.socket, asio::buffer(buffer));
	}

	void AsioTcpServer::ReadAsync(ICommProtocol::ChannelId channelId, ICommProtocol::ReadCallback callback)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		if (!callback)
			throw std::invalid_argument("callback cannot be null");

		Channel& ch = this->FindChannel(channelId);
		if (!ch.socket.is_open())
		{
			callback(std::make_error_code(std::errc::not_connected), ch.id, std::span<const uint8_t>{});
			return;
		}

		auto readBuffer = std::make_shared<std::vector<uint8_t>>(1024);
		ch.socket.async_read_some(asio::buffer(*readBuffer),
			asio::bind_executor(ch.strand,
				[channelId, callback, readBuffer](const std::error_code& ec, size_t size)
				{
					callback(ec, channelId, std::span<const uint8_t>(readBuffer->begin(), size));
				}));
		(void)m_ioThreads.emplace_back(&AsioTcpServer::RunIoContext, this);
	}

	void AsioTcpServer::Write(ICommProtocol::ChannelId channelId, std::span<const uint8_t> buffer)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		if (buffer.empty())
			return;

		Channel& ch = this->FindChannel(channelId);
		if (!ch.socket.is_open())
			return;
		(void)asio::write(ch.socket, asio::buffer(buffer));
	}

	void AsioTcpServer::WriteAsync(ICommProtocol::ChannelId channelId, std::span<const uint8_t> buffer, ICommProtocol::WriteCallback callback)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		if (!callback)
			throw std::invalid_argument("callback cannot be null");

		Channel& ch = this->FindChannel(channelId);
		if (!ch.socket.is_open())
		{
			callback(std::make_error_code(std::errc::not_connected), ch.id, 0);
			return;
		}

		asio::async_write(ch.socket, asio::buffer(buffer),
			asio::bind_executor(ch.strand,
				[channelId, callback](const asio::error_code& ec, size_t size)
				{
					callback(ec, channelId, size);
				}));
		(void)m_ioThreads.emplace_back(&AsioTcpServer::RunIoContext, this);
	}

	void AsioTcpServer::RunAcceptor()
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		auto socket = std::make_shared<asio::ip::tcp::socket>(m_ioCtx);
		(void)m_acceptor.async_accept(*socket,
			asio::bind_executor(m_strand,
				[this, socket](const std::error_code& ec)
				{
					if (!ec && socket->is_open())
					{
						std::lock_guard<std::mutex> lock(m_mutex);

						const auto ep = socket->remote_endpoint();
						auto& ch = m_channels.emplace_back(m_ioCtx, std::format("{}:{}", ep.address().to_string(), ep.port()));
						ch.socket = std::move(*socket);
						m_channelEventCallback(ch.id, ICommProtocol::ChannelEventType::ChannelAdded);
					}

					if (!m_disposing)
						this->RunAcceptor();
				}));
		(void)m_ioThreads.emplace_back(&AsioTcpServer::RunIoContext, this);
	}

	void AsioTcpServer::RunIoContext()
	{
		(void)m_ioCtx.run();

		std::lock_guard<std::mutex> lock(m_removeThreadMutex);
		m_threadIdsToRemove.push_back(std::this_thread::get_id());
	}

	void AsioTcpServer::CleanFinishedThreads()
	{
		while (!m_disposing)
		{
			std::lock_guard<std::mutex> l1(m_removeThreadMutex);
			if (!m_threadIdsToRemove.empty())
			{
				std::lock_guard<std::mutex> l2(m_mutex);
				for (const auto& tid : m_threadIdsToRemove)
				{
					auto it = std::find_if(m_ioThreads.begin(), m_ioThreads.end(), [tid](const std::jthread& t) { return t.get_id() == tid; });
					if (it != m_ioThreads.end())
						(void)m_ioThreads.erase(it);
				}
				m_threadIdsToRemove.clear();
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}

	AsioTcpServer::Channel& AsioTcpServer::FindChannel(ICommProtocol::ChannelId channelId)
	{
		auto it = std::find_if(m_channels.begin(), m_channels.end(), [channelId](const Channel& ch) { return ch.id == channelId; });
		if (it == m_channels.end())
			throw std::invalid_argument(std::format("channel with the id '{}' not found.", channelId));
		return *it;
	}

	const AsioTcpServer::Channel& AsioTcpServer::FindChannel(ICommProtocol::ChannelId channelId) const
	{
		auto it = std::find_if(m_channels.begin(), m_channels.end(), [channelId](const Channel& ch) { return ch.id == channelId; });
		if (it == m_channels.end())
			throw std::invalid_argument(std::format("channel with the id '{}' not found.", channelId));
		return *it;
	}

#pragma endregion TCP Server
}