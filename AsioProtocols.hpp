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
			
			Channel(asio::io_context& ioCtx, const std::string& portName);
			Channel(const Channel&) = delete;
			Channel& operator=(const Channel&) = delete;
		};

	private:
		asio::io_context m_ioCtx;
		mutable std::list<Channel> m_channels; // AvailableReadSize requires native serial port handle

	public:
		AsioSerialProtocol() = default;

		AsioSerialProtocol(const AsioSerialProtocol&) = delete;
		AsioSerialProtocol& operator=(const AsioSerialProtocol&) = delete;

		~AsioSerialProtocol();

		const Channel& GetChannel(size_t ch) const;
		std::optional<std::reference_wrapper<const Channel>> GetChannel(const std::string& portName) const;

		size_t ChannelCount() const;
		size_t AvailableReadSize(size_t ch) const;
		bool IsRunning() const;

		bool Start(
			const std::string& portName,
			asio::serial_port_base::baud_rate baudRate,
			asio::serial_port_base::character_size dataBits = asio::serial_port_base::character_size(),
			asio::serial_port_base::stop_bits stopBits = asio::serial_port_base::stop_bits(),
			asio::serial_port_base::parity parity = asio::serial_port_base::parity(),
			asio::serial_port_base::flow_control flowControl = asio::serial_port_base::flow_control());

		void Stop();

		size_t Read(size_t ch, std::span<uint8_t> buffer);
		void Write(size_t ch, std::span<const uint8_t> buffer);
	};
}

#endif