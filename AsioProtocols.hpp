#ifndef PROTOCOMM_ASIO_PROTOCOLS_HPP
#define PROTOCOMM_ASIO_PROTOCOLS_HPP

#include "ProtoComm.hpp"
#include <asio.hpp>
#include <list>
#include <optional>
#include <functional>
#include <string>
#include <span>
#include <mutex>
#include <atomic>


namespace ProtoComm
{
	/**
	 * @brief Implements multi-channel serial protocol using asio where each channel is a different port.
	 */
	class AsioSerialProtocol final : public ICommProtocol
	{
	private:
		struct Channel
		{
			ICommProtocol::ChannelId id;
			std::string portName;
			asio::serial_port port;
			asio::strand<asio::io_context::executor_type> strand;

			Channel(asio::io_context& ioCtx, const std::string& portName);
			Channel(const Channel&) = delete;
			Channel& operator=(const Channel&) = delete;
		};

	private:
		mutable std::mutex m_mutex;
		std::mutex m_removeThreadMutex;
		std::atomic<bool> m_disposing;

		ICommProtocol::ChannelEventCallback m_channelEventCallback;

		asio::io_context m_ioCtx;
		std::optional<asio::executor_work_guard<asio::io_context::executor_type>> m_workGuard;
		mutable std::list<Channel> m_channels; // AvailableReadSize requires native serial port handle, hence mutable

		std::vector<std::thread::id> m_threadIdsToRemove;
		std::vector<std::jthread> m_ioThreads;

	public:
		AsioSerialProtocol();
		~AsioSerialProtocol();

		AsioSerialProtocol(const AsioSerialProtocol&) = delete;
		AsioSerialProtocol& operator=(const AsioSerialProtocol&) = delete;

		/**
		 * @brief Gets the serial port of a specific channel.
		 * 
		 * @param channelId The unique id of the channel.
		 * @return The serial port.
		 */
		const asio::serial_port& Port(ICommProtocol::ChannelId channelId) const;

		/**
		 * @brief Gets the serial port of a specific channel.
		 * 
		 * @param name The name of the port.
		 * @return The serial port if found, `std::nullopt` otherwise.
		 */
		std::optional<std::reference_wrapper<const asio::serial_port>> Port(const std::string& name) const;

		size_t ChannelCount() const override;
		size_t AvailableReadSize(ICommProtocol::ChannelId channelId) const override;
		bool IsRunning() const override;
		bool IsRunning(ICommProtocol::ChannelId channelId) const override;
		void SetChannelEventCallback(ICommProtocol::ChannelEventCallback callback) override;

		/**
		 * @brief Opens a new serial port as a managable channel.
		 * 
		 * @param portName The name of the port to open.
		 * @param baudRate The baud rate.
		 * @param dataBits The number of data bits.
		 * @param stopBits The number of stop bits.
		 * @param parity The parity checking mode.
		 * @param flowControl The flow control mode.
		 * @return The unique id of the created channel on success, `std::nullopt` on fail.
		 */
		std::optional<ICommProtocol::ChannelId> Start(
			const std::string& portName,
			asio::serial_port_base::baud_rate baudRate,
			asio::serial_port_base::character_size dataBits = asio::serial_port_base::character_size(),
			asio::serial_port_base::stop_bits stopBits = asio::serial_port_base::stop_bits(),
			asio::serial_port_base::parity parity = asio::serial_port_base::parity(),
			asio::serial_port_base::flow_control flowControl = asio::serial_port_base::flow_control());

		void Stop() override;
		void Stop(ICommProtocol::ChannelId channelId) override;

		size_t Read(ICommProtocol::ChannelId channelId, std::span<uint8_t> buffer) override;
		void ReadAsync(ICommProtocol::ChannelId channelId, ICommProtocol::ReadCallback callback) override;

		void Write(ICommProtocol::ChannelId channelId, std::span<const uint8_t> buffer) override;
		void WriteAsync(ICommProtocol::ChannelId channelId, std::span<const uint8_t> buffer, ICommProtocol::WriteCallback callback) override;

	private:
		Channel& FindChannel(ICommProtocol::ChannelId channelId) const;
		void RunIoContext();
		void CleanFinishedThreads();
	};

	/**
	 * @brief Implements a single TCP client using asio.
	 */
	class AsioTcpClient final : public ICommProtocol
	{
	private:
		static constexpr ICommProtocol::ChannelId k_channelId = 0;

		ICommProtocol::ChannelEventCallback m_channelEventCallback;

		asio::io_context m_ioCtx;
		asio::ip::tcp::socket m_socket;
		std::optional<asio::executor_work_guard<asio::io_context::executor_type>> m_workGuard;

		std::atomic<bool> m_disposing;
		std::jthread m_ioThread;

	public:
		AsioTcpClient();
		~AsioTcpClient();

		AsioTcpClient(const AsioTcpClient&) = delete;
		AsioTcpClient& operator=(const AsioTcpClient&) = delete;

		/**
		 * @brief Gets the tcp socket.
		 * 
		 * @return The tcp socket.
		 */
		const asio::ip::tcp::socket& Socket() const;

		size_t ChannelCount() const override;
		size_t AvailableReadSize(ICommProtocol::ChannelId channelId) const override;
		bool IsRunning() const override;
		bool IsRunning(ICommProtocol::ChannelId channelId) const override;
		void SetChannelEventCallback(ICommProtocol::ChannelEventCallback callback) override;

		/**
		 * @brief Connects to a remote server.
		 * 
		 * @param host The hostname or IP address of the server.
		 * @param port The port number.
		 * @return The unique id of the channel on success, `std::nullopt` on fail.
		 */
		std::optional<ICommProtocol::ChannelId> Start(const std::string& host, const std::string& port);

		void Stop() override;
		void Stop(ICommProtocol::ChannelId channelId) override;

		size_t Read(ICommProtocol::ChannelId channelId, std::span<uint8_t> buffer) override;
		void ReadAsync(ICommProtocol::ChannelId channelId, ICommProtocol::ReadCallback callback) override;

		void Write(ICommProtocol::ChannelId channelId, std::span<const uint8_t> buffer) override;
		void WriteAsync(ICommProtocol::ChannelId channelId, std::span<const uint8_t> buffer, ICommProtocol::WriteCallback callback) override;

	private:
		void RunIoContext();
	};

	/**
	 * @brief Implements a TCP server using asio where each channel is a client.
	 */
	class AsioTcpServer final : public ICommProtocol
	{
	private:
		struct Channel
		{
			ICommProtocol::ChannelId id;
			asio::ip::tcp::socket socket;
			asio::strand<asio::io_context::executor_type> strand;

			Channel(asio::io_context& ioCtx, const std::string& fullAddr);
			Channel(const Channel&) = delete;
			Channel& operator=(const Channel&) = delete;
		};

	private:
		mutable std::mutex m_mutex;
		std::mutex m_removeThreadMutex;
		std::atomic<bool> m_disposing;

		ICommProtocol::ChannelEventCallback m_channelEventCallback;

		asio::io_context m_ioCtx;
		asio::ip::tcp::acceptor m_acceptor;
		asio::strand<asio::io_context::executor_type> m_strand;
		std::optional<asio::executor_work_guard<asio::io_context::executor_type>> m_workGuard;
		std::list<Channel> m_channels;

		std::vector<std::thread::id> m_threadIdsToRemove;
		std::vector<std::jthread> m_ioThreads;

	public:
		AsioTcpServer();
		~AsioTcpServer();

		AsioTcpServer(const AsioTcpServer&) = delete;
		AsioTcpServer& operator=(const AsioTcpServer&) = delete;

		size_t ChannelCount() const override;
		size_t AvailableReadSize(ICommProtocol::ChannelId channelId) const override;
		bool IsRunning() const override;
		bool IsRunning(ICommProtocol::ChannelId channelId) const override;
		void SetChannelEventCallback(ICommProtocol::ChannelEventCallback callback) override;

		/**
		 * @brief Starts the server and begins listening.
		 * 
		 * @note The channels represent clients in this protocol, 
		 * hence this method will always return `std::nullopt`.
		 * 
		 * @param host The local IP address to bind to.
		 * @param port The port number to listen on.
		 * @return Always `std::nullopt`.
		 */
		std::optional<ICommProtocol::ChannelId> Start(const std::string& host, const std::string& port);

		void Stop() override;
		void Stop(ICommProtocol::ChannelId channelId) override;

		size_t Read(ICommProtocol::ChannelId channelId, std::span<uint8_t> buffer) override;
		void ReadAsync(ICommProtocol::ChannelId channelId, ICommProtocol::ReadCallback callback) override;

		void Write(ICommProtocol::ChannelId channelId, std::span<const uint8_t> buffer) override;
		void WriteAsync(ICommProtocol::ChannelId channelId, std::span<const uint8_t> buffer, ICommProtocol::WriteCallback callback) override;

	private:
		void RunAcceptor();
		void RunIoContext();
		void CleanFinishedThreads();
		Channel& FindChannel(ICommProtocol::ChannelId channelId);
		const Channel& FindChannel(ICommProtocol::ChannelId channelId) const;
	};
}

#endif