#ifndef PROTOCOMM_ASIO_PROTOCOLS_HPP
#define PROTOCOMM_ASIO_PROTOCOLS_HPP

#include "ProtoComm.hpp"
#include <list>
#include <asio.hpp>
#include <asio/serial_port.hpp>
#include <optional>
#include <functional>
#include <string>
#include <span>
#include <cstdint>
#include <mutex>


namespace ProtoComm
{
	class AsioSerialProtocol final : public ICommProtocol
	{
	public:
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
		ICommProtocol::ChannelEventCallback m_channelEventCallback;

		asio::io_context m_ioCtx;
		mutable std::list<Channel> m_channels; // AvailableReadSize requires native serial port handle, hence mutable

		std::vector<std::jthread> m_ioThreads;

	public:
		AsioSerialProtocol() = default;
		~AsioSerialProtocol();

		AsioSerialProtocol(const AsioSerialProtocol&) = delete;
		AsioSerialProtocol& operator=(const AsioSerialProtocol&) = delete;

		const Channel& GetChannel(ICommProtocol::ChannelId channelId) const;
		std::optional<std::reference_wrapper<const Channel>> GetChannel(const std::string& portName) const;

		size_t ChannelCount() const override;
		size_t AvailableReadSize(ICommProtocol::ChannelId channelId) const override;
		bool IsRunning() const override;
		bool IsRunning(ICommProtocol::ChannelId channelId) const override;
		void SetChannelEventCallback(ICommProtocol::ChannelEventCallback callback) override;

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
	};
}

#endif