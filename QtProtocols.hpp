#ifndef PROTOCOMM_QT_PROTOCOLS_HPP
#define PROTOCOMM_QT_PROTOCOLS_HPP

#include "ProtoComm.hpp"
#include <QSerialPort>
#include <QThread>

namespace ProtoComm
{
	/**
	 * @brief Implements multi-channel serial protocol using Qt where each channel is a different port.
	 */
	class QtSerialProtocol final : public ICommProtocol
	{
	public:
		QtSerialProtocol();
		~QtSerialProtocol();

		QtSerialProtocol(const QtSerialProtocol&) = delete;
		QtSerialProtocol& operator=(const QtSerialProtocol&) = delete;

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
			const QString& portName,
			qint32 baudRate,
			QSerialPort::DataBits dataBits = QSerialPort::Data8,
			QSerialPort::StopBits stopBits = QSerialPort::OneStop,
			QSerialPort::Parity parity = QSerialPort::NoParity,
			QSerialPort::FlowControl flowControl = QSerialPort::NoFlowControl);

		void Stop() override;
		void Stop(ICommProtocol::ChannelId channelId) override;

		size_t Read(ICommProtocol::ChannelId channelId, std::span<uint8_t> buffer) override;
		void ReadAsync(ICommProtocol::ChannelId channelId, ICommProtocol::ReadCallback callback) override;

		void Write(ICommProtocol::ChannelId channelId, std::span<const uint8_t> buffer) override;
		void WriteAsync(ICommProtocol::ChannelId channelId, std::span<const uint8_t> buffer, ICommProtocol::WriteCallback callback) override;

	private:
		struct Channel
		{
			ICommProtocol::ChannelId id;
			QSerialPort port;

			Channel(const QString& portName);
			Channel(const Channel&) = delete;
			Channel& operator=(const Channel&) = delete;
		};

		Channel& FindChannel(ICommProtocol::ChannelId channelId);
		const Channel& FindChannel(ICommProtocol::ChannelId channelId) const;

		ICommProtocol::ChannelEventCallback m_channelEventCallback;
		std::list<Channel> m_channels;

		QThread m_ioThread;
	};
}

#endif