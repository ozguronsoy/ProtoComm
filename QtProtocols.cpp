#include "QtProtocols.hpp"

namespace ProtoComm
{

#pragma region Serial

	QtSerialProtocol::Channel::Channel(const QString& portName)
		: id(0)
	{
		id = std::hash<QString>()(portName);
	}

	QtSerialProtocol::QtSerialProtocol()
	{
		m_ioThread.start();
	}

	QtSerialProtocol::~QtSerialProtocol()
	{
		if (this->IsRunning())
			this->Stop();

		m_ioThread.quit();
		(void)m_ioThread.wait();
	}

	size_t QtSerialProtocol::ChannelCount() const
	{
		return m_channels.size();
	}

	size_t QtSerialProtocol::AvailableReadSize(ICommProtocol::ChannelId channelId) const
	{
		const Channel& ch = this->FindChannel(channelId);
		return ch.port.bytesAvailable();
	}

	bool QtSerialProtocol::IsRunning() const
	{
		return std::find_if(m_channels.begin(), m_channels.end(), [](const Channel& channel) { return channel.port.isOpen(); }) != m_channels.end();
	}

	bool QtSerialProtocol::IsRunning(ICommProtocol::ChannelId channelId) const
	{
		const Channel& ch = this->FindChannel(channelId);
		return ch.port.isOpen();
	}

	void QtSerialProtocol::SetChannelEventCallback(ICommProtocol::ChannelEventCallback callback)
	{
		if (!callback)
			throw std::invalid_argument("channel event callback cannot be null");
		m_channelEventCallback = callback;
	}

	std::optional<ICommProtocol::ChannelId> QtSerialProtocol::Start(
		const QString& portName,
		qint32 baudRate,
		QSerialPort::DataBits dataBits,
		QSerialPort::StopBits stopBits,
		QSerialPort::Parity parity,
		QSerialPort::FlowControl flowControl)
	{
		if (!m_channelEventCallback)
			throw std::logic_error("channel event callback must be set before starting the protocol");

		if (std::find_if(m_channels.begin(), m_channels.end(), [&portName](const Channel& channel) { return channel.port.portName() == portName; }) != m_channels.end())
			return std::nullopt;

		const ICommProtocol::ChannelId channelId = std::hash<QString>()(portName);
		QObject obj; // we need this to make invokeMethod work
		obj.moveToThread(&m_ioThread);

		try
		{
			std::promise<bool> promise;
			(void)QMetaObject::invokeMethod(&obj,
				[&]()
				{
					try
					{
						Channel& ch = m_channels.emplace_back(portName);

						ch.port.setPortName(portName);

						if (!ch.port.setBaudRate(baudRate))
							throw std::runtime_error("failed to set baud rate");

						if (!ch.port.setDataBits(dataBits))
							throw std::runtime_error("failed to set data bits");

						if (!ch.port.setStopBits(stopBits))
							throw std::runtime_error("failed to set stop bits");

						if (!ch.port.setParity(parity))
							throw std::runtime_error("failed to set parity");

						if (!ch.port.setFlowControl(flowControl))
							throw std::runtime_error("failed to set flow control");

						if (!ch.port.open(QIODeviceBase::ReadWrite) || !ch.port.isOpen())
							throw std::runtime_error("port failed to open");

						promise.set_value(true);
					}
					catch (const std::exception& e)
					{
						try
						{
							promise.set_exception(std::current_exception());
						}
						catch (...)
						{
							promise.set_value(false);
						}
					}
				}, Qt::QueuedConnection);

			const bool res = promise.get_future().get();
			if (!res)
				throw std::runtime_error("something went wrong");

			m_channelEventCallback(channelId, ICommProtocol::ChannelEventType::ChannelAdded);
			return channelId;
		}
		catch (const std::exception&)
		{
			auto it = std::find_if(m_channels.begin(), m_channels.end(), [channelId](const Channel& ch) { return ch.id == channelId; });
			if (it != m_channels.end() && it->port.isOpen())
			{
				std::promise<void> promise;
				(void)QMetaObject::invokeMethod(&obj,
					[this, it, &promise]()
					{
						it->port.close();
						(void)m_channels.erase(it);
						promise.set_value();
					}, Qt::QueuedConnection);
				promise.get_future().get();
			}
			return std::nullopt;
		}
	}

	void QtSerialProtocol::Stop()
	{
		for (size_t i = m_channels.size(); (i--) > 0;)
		{
			auto it = std::next(m_channels.begin(), i);
			if (it->port.isOpen())
			{
				m_channelEventCallback(it->id, ICommProtocol::ChannelEventType::ChannelRemoved);

				std::promise<void> promise;
				(void)QMetaObject::invokeMethod(&it->port, [this, it, &promise]()
					{
						it->port.close();
						(void)m_channels.erase(it);
						promise.set_value();
					}, Qt::QueuedConnection);
				promise.get_future().get();
			}
		}
	}

	void QtSerialProtocol::Stop(ICommProtocol::ChannelId channelId)
	{
		auto it = std::find_if(m_channels.begin(), m_channels.end(), [channelId](const Channel& ch) { return ch.id == channelId; });
		if (it != m_channels.end() && it->port.isOpen())
		{
			m_channelEventCallback(it->id, ICommProtocol::ChannelEventType::ChannelRemoved);

			std::promise<void> promise;
			(void)QMetaObject::invokeMethod(&it->port, [this, it, &promise]()
				{
					it->port.close();
					(void)m_channels.erase(it);
					promise.set_value();
				}, Qt::QueuedConnection);
			promise.get_future().get();
		}
	}

	size_t QtSerialProtocol::Read(ICommProtocol::ChannelId channelId, std::span<uint8_t> buffer)
	{
		if (buffer.empty())
			return 0;

		Channel& ch = this->FindChannel(channelId);
		if (!ch.port.isOpen())
			return 0;
		return ch.port.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
	}

	void QtSerialProtocol::ReadAsync(ICommProtocol::ChannelId channelId, ICommProtocol::ReadCallback callback)
	{
		if (!callback)
			throw std::invalid_argument("callback cannot be null");

		Channel& ch = this->FindChannel(channelId);
		if (!ch.port.isOpen())
		{
			callback(std::make_error_code(std::errc::not_connected), ch.id, std::span<const uint8_t>{});
			return;
		}

		auto connection = std::make_shared<QMetaObject::Connection>();
		(*connection) = ch.port.connect(&ch.port, &QSerialPort::readyRead,
			[this, channelId, callback, connection]()
			{
				Channel& ch = this->FindChannel(channelId);
				QByteArray data = ch.port.readAll();
				callback(std::error_code(), channelId, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(data.data()), data.size()));
				(void)ch.port.disconnect(*connection);
			});
	}

	void QtSerialProtocol::Write(ICommProtocol::ChannelId channelId, std::span<const uint8_t> buffer)
	{
		if (buffer.empty())
			return;

		Channel& ch = this->FindChannel(channelId);
		if (ch.port.isOpen())
		{
			(void)ch.port.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
			(void)ch.port.waitForBytesWritten(-1);
		}
	}

	void QtSerialProtocol::WriteAsync(ICommProtocol::ChannelId channelId, std::span<const uint8_t> buffer, ICommProtocol::WriteCallback callback)
	{
		if (!callback)
			throw std::invalid_argument("callback cannot be null");

		if (buffer.empty())
		{
			callback(std::error_code(), channelId, 0);
			return;
		}

		Channel& ch = this->FindChannel(channelId);
		if (!ch.port.isOpen())
		{
			callback(std::make_error_code(std::errc::not_connected), ch.id, 0);
			return;
		}

		(void)QMetaObject::invokeMethod(&ch.port,
			[this, channelId, buffer, callback]()
			{
				Channel& ch = this->FindChannel(channelId);
				(void)ch.port.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
			}, Qt::QueuedConnection);

		auto connection = std::make_shared<QMetaObject::Connection>();
		(*connection) = ch.port.connect(&ch.port, &QSerialPort::bytesWritten,
			[this, channelId, callback, connection](quint64 size)
			{
				Channel& ch = this->FindChannel(channelId);
				QByteArray data = ch.port.readAll();
				callback(std::error_code(), channelId, size);
				(void)ch.port.disconnect(*connection);
			});
	}

	QtSerialProtocol::Channel& QtSerialProtocol::FindChannel(ICommProtocol::ChannelId channelId)
	{
		auto it = std::find_if(m_channels.begin(), m_channels.end(), [channelId](const Channel& ch) { return ch.id == channelId; });
		if (it == m_channels.end())
			throw std::invalid_argument(std::format("channel with the id '{}' not found.", channelId));
		return *it;
	}

	const QtSerialProtocol::Channel& QtSerialProtocol::FindChannel(ICommProtocol::ChannelId channelId) const
	{
		auto it = std::find_if(m_channels.begin(), m_channels.end(), [channelId](const Channel& ch) { return ch.id == channelId; });
		if (it == m_channels.end())
			throw std::invalid_argument(std::format("channel with the id '{}' not found.", channelId));
		return *it;
	}

#pragma endregion Serial

}