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


namespace ProtoComm
{
	class AsioSerialProtocol final
	{
	public:
		struct Channel
		{
			std::string portName;
			asio::serial_port port;
			asio::strand<asio::io_context::executor_type> strand;

			Channel(asio::io_context& ioCtx, const std::string& portName);
			Channel(const Channel&) = delete;
			Channel& operator=(const Channel&) = delete;
		};

	private:
		asio::io_context m_ioCtx;
		mutable std::list<Channel> m_channels; // AvailableReadSize requires native serial port handle, hence mutable

		std::vector<std::jthread> m_ioThreads;

	public:
		AsioSerialProtocol() = default;
		~AsioSerialProtocol();

		AsioSerialProtocol(const AsioSerialProtocol&) = delete;
		AsioSerialProtocol& operator=(const AsioSerialProtocol&) = delete;

		const Channel& GetChannel(size_t ch) const;
		std::optional<std::reference_wrapper<const Channel>> GetChannel(const std::string& portName) const;

		size_t ChannelCount() const;
		size_t AvailableReadSize(size_t ch) const;
		bool IsRunning() const;
		bool IsRunning(size_t ch) const;

		bool Start(
			const std::string& portName,
			asio::serial_port_base::baud_rate baudRate,
			asio::serial_port_base::character_size dataBits = asio::serial_port_base::character_size(),
			asio::serial_port_base::stop_bits stopBits = asio::serial_port_base::stop_bits(),
			asio::serial_port_base::parity parity = asio::serial_port_base::parity(),
			asio::serial_port_base::flow_control flowControl = asio::serial_port_base::flow_control());

		void Stop();
		void Stop(size_t ch);

		size_t Read(size_t ch, std::span<uint8_t> buffer);
		void ReadAsync(size_t ch, ProtocolReadCallback callback);

		void Write(size_t ch, std::span<const uint8_t> buffer);
		void WriteAsync(size_t ch, std::span<const uint8_t> buffer, ProtocolWriteCallback callback);

	private:
		void RunIoContext();
	};
}

#endif