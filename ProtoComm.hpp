#ifndef PROTOCOMM_HPP
#define PROTOCOMM_HPP

#include <cstdint>
#include <cstring>
#include <array>
#include <vector>
#include <span>
#include <memory>
#include <optional>
#include <numeric>
#include <algorithm>
#include <type_traits>
#include <concepts>
#include <chrono>
#include <thread>
#include <functional>
#include <future>
#include <format>
#include <stdexcept>
#include <typeinfo>
#include <typeindex>

namespace ProtoComm
{

	class IFrameHandler;

#pragma region Messages

	/**
	 * @brief Base interface for a communication message.
	 *
	 * This class defines the common structural properties of a message's
	 * raw frame, such as its expected size and header/footer patterns.
	 */
	class IMessage
	{
	public:
		virtual ~IMessage() = default;

		/**
		 * @brief Gets the fixed size of the message frame, if one exists.
		 *
		 * @return An `std::optional<size_t>` containing the fixed frame size,
		 * or `std::nullopt` if the frame is variable-sized.
		 */
		virtual std::optional<size_t> FrameSize() const = 0;

		/**
		 * @brief Gets the fixed byte pattern that identifies the start of a frame.
		 *
		 * @return A `std::span` viewing the constant header bytes.
		 * An empty span (e.g., `return {};`) indicates that this message
		 * does not use a header pattern.
		 */
		virtual std::span<const uint8_t> HeaderPattern() const = 0;

		/**
		 * @brief Gets the fixed byte pattern that identifies the end of a frame.
		 *
		 * @return A `std::span` viewing the constant footer bytes.
		 * An empty span (e.g., `return {};`) indicates that this message
		 * does not use a footer pattern.
		 */
		virtual std::span<const uint8_t> FooterPattern() const = 0;

		/**
		 * @brief Gets the frame handler responsible for this message type.
		 *
		 * @return A reference to the associated frame handler instance.
		 */
		virtual IFrameHandler& FrameHandler() const = 0;

		/**
		 * @brief Creates a deep copy of this message object.
		 *
		 * @return A unique pointer pointing to the cloned object.
		 */
		virtual std::unique_ptr<IMessage> Clone() const = 0;
	};

	/**
	 * @brief Interface for a receivable (Rx) message.
	 *
	 * A class deriving from IRxMessage represents a message that can be
	 * deserialized or "unpacked" from a raw data frame received from the
	 * transport layer.
	 */
	class IRxMessage : public virtual IMessage
	{
	public:
		virtual ~IRxMessage() = default;

		/**
		 * @brief Unpacks (deserializes) data from a raw frame into this object.
		 *
		 * @param frame The raw data frame received from the communication channel.
		 */
		virtual void Unpack(std::span<const uint8_t> frame) = 0;
	};

	/**
	 * @brief Interface for a transmittable (Tx) message.
	 *
	 * A class deriving from ITxMessage represents a message that can be
	 * serialized or "packed" into a raw data frame to be sent over the
	 * transport layer.
	 */
	class ITxMessage : public virtual IMessage
	{
	public:
		virtual ~ITxMessage() = default;

		/**
		 * @brief Packs (serializes) the data from this object into a raw frame.
		 *
		 * @param frame The raw data frame that will be filled and sent over the communication channel.
		 */
		virtual void Pack(std::vector<uint8_t>& frame) const = 0;
	};

#pragma endregion Messages

#pragma region Frame Handlers

	/**
	 * @brief Provides methods for frame validation and sealing.
	 *
	 */
	class IFrameHandler
	{
	public:
		virtual ~IFrameHandler() = default;

		/**
		 * @brief Validates an incoming raw data frame.
		 *
		 * @param msg A prototyep message that will be used for retrieving validation rules.
		 * @param frame Raw frame to be validated.
		 * @return true if the frame's integrity is valid, false otherwise.
		 */
		virtual bool Validate(const IRxMessage& msg, std::span<const uint8_t> frame) = 0;

		/**
		 * @brief Finalizes the outgoing raw data frame by adding required structural data (e.g., header, footer, checksum).
		 *
		 * @param msg A prototyep message that will be used for retrieving validation rules.
		 * @param frame Raw frame that will be modified in-place.
		 */
		virtual void Seal(const ITxMessage& msg, std::span<uint8_t> frame) = 0;
	};

	/**
	 * @brief Validates and seals the frame's header and footer.
	 *
	 */
	class FrameHandler : public IFrameHandler
	{
	public:
		virtual ~FrameHandler() = default;

		virtual bool Validate(const IRxMessage& msg, std::span<const uint8_t> frame) override
		{
			const auto frameSize = msg.FrameSize();
			if (frameSize.has_value() && frame.size() != (*frameSize))
				return false;

			auto headerPattern = msg.HeaderPattern();
			if (!std::equal(headerPattern.begin(), headerPattern.end(), frame.begin()))
				return false;

			auto footerPattern = msg.FooterPattern();
			if (!footerPattern.empty() && !std::equal(footerPattern.begin(), footerPattern.end(), (frame.end() - footerPattern.size())))
				return false;

			return true;
		}

		virtual void Seal(const ITxMessage& msg, std::span<uint8_t> frame) override
		{
			auto headerPattern = msg.HeaderPattern();
			(void)std::copy(headerPattern.begin(), headerPattern.end(), frame.begin());

			auto footerPattern = msg.FooterPattern();
			if (!footerPattern.empty())
			{
				(void)std::copy(footerPattern.rbegin(), footerPattern.rend(), frame.rbegin());
			}
		}

		static FrameHandler& Instance()
		{
			static FrameHandler instance{};
			return instance;
		}
	};

	/**
	 * @brief Validates and computes the checksum for a frame.
	 *
	 * @tparam TChecksum The data type of the checksum itself (e.g., `uint8_t`, `uint16_t`).
	 * @tparam TData The data type of the payload elements being summed (default: `uint8_t`).
	 * @tparam init The initial value for the binary operation (default: 0).
	 * @tparam BinaryOp The binary operation to use (default: `std::plus<TData>`).
	 */
	template<typename TChecksum = uint8_t, typename TData = uint8_t, TData init = 0, typename BinaryOp = std::plus<TData>>
		requires std::is_arithmetic_v<TChecksum>&& std::is_arithmetic_v<TData>
	class ChecksumFrameHandler : public FrameHandler
	{
	public:
		virtual ~ChecksumFrameHandler() = default;

		TChecksum CalculateChecksum(const IMessage& msg, std::span<const uint8_t> frame) const
		{
			auto payloadBegin = frame.begin() + msg.HeaderPattern().size();
			auto payloadEnd = frame.end() - msg.FooterPattern().size() - sizeof(TChecksum);
			return static_cast<TChecksum>(std::accumulate(payloadBegin, payloadEnd, init, BinaryOp()));
		}

		virtual bool Validate(const IRxMessage& msg, std::span<const uint8_t> frame) override
		{
			if (!FrameHandler::Validate(msg, frame))
				return false;

			auto checksumPosition = frame.end() - msg.FooterPattern().size() - sizeof(TChecksum);
			TChecksum expected;
			(void)std::memcpy(&expected, &(*checksumPosition), sizeof(TChecksum));

			const TChecksum calculated = this->CalculateChecksum(msg, frame);

			return calculated == expected;
		}

		virtual void Seal(const ITxMessage& msg, std::span<uint8_t> frame) override
		{
			FrameHandler::Seal(msg, frame);

			const TChecksum checksum = this->CalculateChecksum(msg, frame);
			auto checksumPosition = frame.end() - msg.FooterPattern().size() - sizeof(TChecksum);
			(void)std::memcpy(&(*checksumPosition), &checksum, sizeof(TChecksum));
		}

		static ChecksumFrameHandler& Instance()
		{
			static ChecksumFrameHandler instance{};
			return instance;
		}
	};

#pragma endregion Frame Handlers

#pragma region Comm

	/**
	 * @brief Interface for communication protocols.
	 */
	class ICommProtocol
	{
	public:
		/**
		 * @brief Specifies the type of the event that occurred on a protocol channel.
		 */
		enum class ChannelEventType
		{
			/** @biref Indicates that a new channel has been added. */
			ChannelAdded,
			/** @brief Indicates that an existing channel has been removed. */
			ChannelRemoved
		};

		/**
		 * @brief Type of the unique channel identifier.
		 */
		using ChannelId = size_t;

		/**
		 * @brief Callback function type for channel events.
		 */
		using ChannelEventCallback = std::function<void(ChannelId, ChannelEventType)>;

		/**
		 * @brief Callback function type for asynchronous reads.
		 */
		using ReadCallback = std::function<void(const std::error_code&, ChannelId, std::span<const uint8_t>)>;

		/**
		 * @brief Callback function type for asynchronous writes.
		 */
		using WriteCallback = std::function<void(const std::error_code&, ChannelId, size_t)>;

		/**
		 * @brief Gets the number of active channels.
		 *
		 * @return The number of active channels.
		 */
		virtual size_t ChannelCount() const = 0;

		/**
		 * @brief Gets the number of bytes available to read from the specified channel.

		 * @param channelId The unique id of the channel.
		 * @return The number of bytes available to read.
		 */
		virtual size_t AvailableReadSize(ChannelId channelId) const = 0;

		/**
		 * @brief Checks whether the protocol is active.
		 *
		 * @return `true` if the protocol is running.
		 */
		virtual bool IsRunning() const = 0;

		/**
		 * @brief Checks whether the specified channel is active.

		 * @param channelId The unique id of the channel.
		 * @return `true` if the channel is active.
		 */
		virtual bool IsRunning(ChannelId channelId) const = 0;

		/**
		 * @brief Sets the channel event callback.
		 *
		 * @param callback The method that will be invoked when a channel event occurs.
		 */
		virtual void SetChannelEventCallback(ChannelEventCallback callback) = 0;

		/**
		 * @brief Stops all channels and the protocol.
		 */
		virtual void Stop() = 0;

		/**
		 * @brief Stops the specified channel.

		 * @param channelId The unique id of the channel.
		 */
		virtual void Stop(ChannelId channelId) = 0;

		/**
		 * @brief Reads from the specified channel.
		 *
		 * @param channelId The unique id of the channel.
		 * @param buffer The buffer that will be filled with the data read from the channel.
		 * @return The number of bytes read.
		 */
		virtual size_t Read(ChannelId channelId, std::span<uint8_t> buffer) = 0;

		/**
		 * @brief Reads from the specified channel asynchronously.
		 *
		 * @param channelId The unique id of the channel.
		 * @param callback The method which will be invoked when the read operation completes or fails.
		 */
		virtual void ReadAsync(ChannelId channelId, ReadCallback callback) = 0;

		/**
		 * @brief Writes to the specified channel.
		 *
		 * @param channelId The unique id of the channel.
		 * @param buffer The buffer that contains the data to be written.
		 */
		virtual void Write(ChannelId channelId, std::span<const uint8_t> buffer) = 0;

		/**
		 * @brief Writes to the specified channel.
		 *
		 * @param channelId The unique id of the channel.
		 * @param buffer The buffer that contains the data to be written.
		 * @param callback The method which will be invoked when the write operation completes or fails.
		 */
		virtual void WriteAsync(ChannelId channelId, std::span<const uint8_t> buffer, WriteCallback callback) = 0;
	};

	/**
	 * @brief Specifies the type ``T`` has a ``Start`` method that takes ``Args`` parameters.
	 *
	 */
	template<typename T, typename... Args>
	concept Startable = requires(T t, Args... args)
	{
		{ t.Start(std::forward<Args>(args)...) } -> std::same_as<std::optional<ICommProtocol::ChannelId>>;
	};

	/**
	 * @brief Manages message-based communication over a protocol.
	 *
	 * @tparam Protocol The concrete type of the communication protocol (e.g., `TcpClient`, `SerialPort`).
	 */
	template<typename Protocol>
		requires std::derived_from<Protocol, ICommProtocol>
	class CommStream final
	{
	private:
		struct FrameInfo
		{
			std::optional<size_t> size;
			std::span<const uint8_t> headerPattern;
			std::span<const uint8_t> footerPattern;
			std::reference_wrapper<IFrameHandler> handler;

			FrameInfo(std::optional<size_t> fs, std::span<const uint8_t> hp, std::span<const uint8_t> fp, IFrameHandler& fh)
				: size(fs),
				headerPattern(hp),
				footerPattern(fp),
				handler(fh)
			{
			}
		};

	public:
		class Channel
		{
			friend class CommStream;

		private:
			ICommProtocol::ChannelId id;
			std::vector<uint8_t> rxBuffer;

		public:
			Channel(ICommProtocol::ChannelId id)
				: id(id)
			{
			}

			Channel(const Channel&) = delete;
			Channel& operator=(const Channel&) = delete;

			ICommProtocol::ChannelId Id() const
			{
				return id;
			}
		};

	private:
		mutable std::mutex m_mutex;
		std::vector<std::shared_ptr<Channel>> m_channels;
		Protocol m_protocol;

	public:
		/**
		 * @brief Callback function type for async read operations.
		 * @return `true` to continue reading, `false` to stop.
		 */
		using ReadCallback = std::function<bool(const std::error_code&, std::shared_ptr<Channel>, std::span<std::unique_ptr<IRxMessage>>)>;
		/**
		 * @brief Callback function type for async write operations.
		 */
		using WriteCallback = std::function<void(const std::error_code&, std::shared_ptr<Channel>, size_t)>;
		/**
		 * @brief Type of the rx message prototypes.
		 */
		using RxMessagePrototype = std::shared_ptr<const IRxMessage>;

	public:
		CommStream()
		{
			m_protocol.SetChannelEventCallback(std::bind(&CommStream::ChannelEventHandler, this, std::placeholders::_1, std::placeholders::_2));
		}

		CommStream(const CommStream&) = delete;
		CommStream& operator=(const CommStream&) = delete;

		/**
		 * @brief Gets the underlying communication protocol.
		 *
		 * @return A reference to the internal `Protocol` instance.
		 */
		const Protocol& GetProtocol() const
		{
			return m_protocol;
		}

		/**
		 * @brief Gets the number of active channels.
		 *
		 * @return The number of active channels.
		 */
		size_t ChannelCount() const
		{
			std::lock_guard<std::mutex> lock(m_mutex);

			const size_t protocolChannelCount = m_protocol.ChannelCount();
			const size_t streamChannelCount = m_channels.size();

			if (protocolChannelCount != streamChannelCount)
				throw std::logic_error(std::format(
					"Internal state synchronization error: Protocol channel count ({}) "
					"does not match CommStream's internal channel count ({}).",
					protocolChannelCount, streamChannelCount
				));

			return protocolChannelCount;
		}

		/**
		 * @brief Gets the channel with a specific id.
		 *
		 * @param channelId The unique id of the channel.
		 * @return A shared pointer if the requested channel if found, otherwise `nullptr`.
		 */
		std::shared_ptr<Channel> GetChannelById(ICommProtocol::ChannelId channelId)
		{
			std::lock_guard<std::mutex> lock(m_mutex);

			auto it = std::find_if(m_channels.begin(), m_channels.end(), [channelId](const auto& ch) { return channelId == ch->id; });
			return (it != m_channels.end()) ? (*it) : (std::shared_ptr<Channel>());
		}

		/**
		 * @brief Gets the channel at the provided index.
		 *
		 * @param index The zero-based index of the channel.
		 * @return A shared pointer if the requested channel if found, otherwise `nullptr`.
		 */
		std::shared_ptr<Channel> GetChannel(size_t index)
		{
			std::lock_guard<std::mutex> lock(m_mutex);

			if (index >= m_channels.size())
				throw std::out_of_range("index out of bounds");
			return m_channels[index];
		}

		/**
		 * @brief Checks if the underlying protocol is currently running.
		 *
		 * @return true if the protocol is running, false otherwise.
		 */
		bool IsRunning() const
		{
			return m_protocol.IsRunning();
		}

		/**
		 * @brief Checks if a specific communication channel is running.
		 * @param ch The channel to check.
		 * @return `true` if the specified channel is running, `false` otherwise.
		 */
		bool IsRunning(std::shared_ptr<Channel> ch) const
		{
			return m_protocol.IsRunning(ch->id);
		}

		/**
		 * @brief Starts the underlying communication protocol.
		 *
		 * @tparam Args The types of arguments required by the protocol's `Start` method.
		 * @param args The arguments required by the protocol's `Start` method.
		 *
		 * @return A shared pointer to the channel if the protocol was started successfully,
		 * `nullptr` if it failed or if the communication protocol does not create a channel on start.
		 */
		template<typename... Args>
			requires Startable<Protocol, Args...>
		std::shared_ptr<Channel> Start(Args... args)
		{
			std::optional<ICommProtocol::ChannelId> channelId = m_protocol.Start(std::forward<Args>(args)...);
			if (channelId.has_value())
				return this->GetChannelById(*channelId);
			return nullptr;
		}

		/**
		 * @brief Stops all channels and the underlying communication protocol.
		 *
		 */
		void Stop()
		{
			m_protocol.Stop();
		}

		/**
		 * @brief Stops a single communication channel.
		 *
		 * @param ch The channel to stop.
		 */
		void Stop(std::shared_ptr<Channel> ch)
		{
			m_protocol.Stop(ch->id);
		}

		/**
		 * @brief Removes all inactive or disconnected channels.
		 *
		 * @return The number of channels that were removed.
		 */
		size_t Prune()
		{
			std::lock_guard<std::mutex> lock(m_mutex);

			size_t channelsRemoved = 0;
			for (const auto& ch : m_channels)
			{
				if (!this->IsRunning(ch))
				{
					this->Stop(ch);
					channelsRemoved++;
				}
			}
			return channelsRemoved;
		}

		/**
		 * @brief Blocks until 'n' messages are read from a specific channel or a timeout occurs.
		 *
		 * @tparam RxMessages Message types to listen for.
		 *
		 * @param ch The channel from which to read messages.
		 * @param n The number of messages to read.
		 * @param timeout The maximum duration to wait for the messages where `0` means wait indefinitely.
		 *
		 * @return A vector containing the messages that were successfully parsed.
		 */
		template<typename... RxMessages>
			requires (std::derived_from<RxMessages, IRxMessage>, ...)
		std::vector<std::unique_ptr<IRxMessage>> Read(
			std::shared_ptr<Channel> ch,
			size_t n = 1,
			std::chrono::milliseconds timeout = std::chrono::milliseconds::zero())
		{
			const std::array<RxMessagePrototype, sizeof...(RxMessages)> prototypes = { std::make_shared<const RxMessages>()... };
			return this->Read(ch, prototypes, n, timeout);
		}

		/**
		 * @brief Blocks until 'n' messages are read from a specific channel
		 * or a timeout occurs.
		 *
		 * @param ch The channel from which to read messages.
		 * @param prototypes A span of prototype instances that specifiy the types of messages to attempt to parse from the incoming data.
		 * @param n The desired number of messages to read.
		 * @param timeout The maximum duration to wait for the messages where `0` means wait indefinitely.
		 *
		 * @return A vector containing the messages that were successfully parsed.
		 */
		std::vector<std::unique_ptr<IRxMessage>> Read(
			std::shared_ptr<Channel> ch,
			std::span<const RxMessagePrototype> prototypes,
			size_t n = 1,
			std::chrono::milliseconds timeout = std::chrono::milliseconds::zero())
		{
			using clock = std::chrono::high_resolution_clock;

			constexpr std::chrono::milliseconds pollingPeriod = std::chrono::milliseconds(10);

			if (!this->ChannelExists(ch))
				throw std::invalid_argument("channel not found");

			this->ValidatePrototypes(prototypes);

			auto& rxBuffer = ch->rxBuffer;
			std::vector<std::unique_ptr<IRxMessage>> messages;
			const clock::time_point t1 = clock::now();
			const ICommProtocol::ChannelId channelId = ch->id;

			if (n == 0)
				return messages;

			// false if timeout reached
			auto checkTimeout = [&timeout, &t1]() -> bool
				{
					return (timeout == std::chrono::milliseconds::zero())
						|| (std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t1) < timeout);
				};

			do
			{
				const size_t readSize = m_protocol.AvailableReadSize(channelId);
				if (readSize == 0)
				{
					std::this_thread::sleep_for(pollingPeriod);
					continue;
				}

				const size_t rxBufferOldSize = rxBuffer.size();
				rxBuffer.resize(rxBufferOldSize + readSize);
				(void)m_protocol.Read(channelId, std::span<uint8_t>(rxBuffer.begin() + rxBufferOldSize, readSize));
				this->ParseRxMessages(prototypes, rxBuffer, messages, n, checkTimeout);

				std::this_thread::sleep_for(pollingPeriod);

			} while (messages.size() < n && checkTimeout());

			return messages;
		}

		/**
		 * @brief Reads messages from a specific channel asynchronously.
		 *
		 * @tparam RxMessages Message types to listen for.
		 *
		 * @param ch The channel from which to read messages.
		 * @param callback The function that will be called when at least one message is parsed, or when an error occurs.
		 */
		template<typename... RxMessages>
			requires (std::derived_from<RxMessages, IRxMessage>, ...)
		void ReadAsync(std::shared_ptr<Channel> ch, ReadCallback callback)
		{
			const std::array<RxMessagePrototype, sizeof...(RxMessages)> prototypes = { std::make_shared<const RxMessages>()... };
			this->ReadAsync(ch, prototypes, callback);
		}

		/**
		 * @brief Reads at least 'n' messages from a specific channel asynchronously.
		 *
		 * @tparam RxMessages Message types to listen for.
		 *
		 * @param ch The channel from which to read messages.
		 * @param n The desired number of messages to read.
		 * @return A future which will eventually contain the vector of messages that were successfully parsed.
		 */
		template<typename... RxMessages>
			requires (std::derived_from<RxMessages, IRxMessage>, ...)
		std::future<std::vector<std::unique_ptr<IRxMessage>>> ReadAsync(std::shared_ptr<Channel> ch, size_t n)
		{
			const std::array<RxMessagePrototype, sizeof...(RxMessages)> prototypes = { std::make_shared<const RxMessages>()... };
			return this->ReadAsync(ch, prototypes, n);
		}

		/**
		 * @brief Reads messages from a specific channel asynchronously.
		 *
		 * @param ch The channel from which to read messages.
		 * @param prototypes A span of prototype instances that specifiy the types of messages to attempt to parse from the incoming data.
		 * @param callback The function that will be called when at least one message is parsed, or when an error occurs.
		 */
		void ReadAsync(
			std::shared_ptr<Channel> ch,
			std::span<const RxMessagePrototype> prototypes,
			ReadCallback callback)
		{
			if (!this->ChannelExists(ch))
				throw std::invalid_argument("channel not found");

			if (!callback)
				throw std::invalid_argument("callback cannot be null");

			this->ValidatePrototypes(prototypes);

			auto pPrototypes = std::make_shared<std::vector<RxMessagePrototype>>(prototypes.begin(), prototypes.end());
			auto protocolCallback = [this, ch, callback, pPrototypes](const std::error_code& ec, ICommProtocol::ChannelId channelId, std::span<const uint8_t> data)
				{
					std::vector<std::unique_ptr<IRxMessage>> messages;

					if (!ec && data.size() > 0)
					{
						auto& rxBuffer = ch->rxBuffer;
						const size_t rxBufferOldSize = rxBuffer.size();
						rxBuffer.resize(rxBufferOldSize + data.size());
						(void)std::copy(data.begin(), data.end(), rxBuffer.begin() + rxBufferOldSize);
						this->ParseRxMessages(*pPrototypes, rxBuffer, messages, std::numeric_limits<size_t>::max(), nullptr);
					}

					bool continueReading = true;

					if (ec || !messages.empty())
						continueReading = callback(ec, ch, messages);

					if (continueReading)
						this->ReadAsync(ch, *pPrototypes, callback);
				};

			m_protocol.ReadAsync(ch->id, protocolCallback);
		}

		/**
		 * @brief Reads at least 'n' messages from a specific channel asynchronously.
		 *
		 * @param ch The channel from which to read messages.
		 * @param prototypes A span of prototype instances that specifiy the types of messages to attempt to parse from the incoming data.
		 * @param n The desired number of messages to read.
		 * @return A future which will eventually contain the vector of messages that were successfully parsed.
		 */
		std::future<std::vector<std::unique_ptr<IRxMessage>>> ReadAsync(
			std::shared_ptr<Channel> ch,
			std::span<const RxMessagePrototype> prototypes,
			size_t n)
		{
			if (n == 0)
				return std::async(std::launch::deferred, []() { return std::vector<std::unique_ptr<IRxMessage>>{}; });

			auto messages = std::make_shared<std::vector<std::unique_ptr<IRxMessage>>>();
			auto promise = std::make_shared<std::promise<std::vector<std::unique_ptr<IRxMessage>>>>();
			this->ReadAsync(ch, prototypes,
				[n, messages, promise](const std::error_code& ec, std::shared_ptr<Channel> ch, std::span<std::unique_ptr<IRxMessage>> newMessages) -> bool
				{
					if (ec)
					{
						promise->set_exception(std::make_exception_ptr(std::runtime_error(std::format("ReadAsync error: {}", ec.message()))));
						return false;
					}

					messages->insert(messages->end(),
						std::make_move_iterator(newMessages.begin()),
						std::make_move_iterator(newMessages.end()));

					if (messages->size() >= n)
					{
						promise->set_value(std::move(*messages));
						return false;
					}

					return true;
				});

			return promise->get_future();
		}

		/**
		 * @brief Writes a single message to a specific channel.
		 *
		 * @param ch The channel which the message will be sent.
		 * @param msg The message to be sent.
		 */
		void Write(std::shared_ptr<Channel> ch, const ITxMessage& msg)
		{
			const std::array<std::reference_wrapper<const ITxMessage>, 1> messages = { msg };
			this->Write(ch, messages);
		}

		/**
		 * @brief Writes a batch of messages to a specific channel.
		 *
		 * @param ch The channel which the message will be sent.
		 * @param messages A span of messages to send.
		 */
		void Write(std::shared_ptr<Channel> ch, std::span<const std::reference_wrapper<const ITxMessage>> messages)
		{
			if (!this->ChannelExists(ch))
				throw std::invalid_argument("channel not found");

			std::vector<uint8_t> frame;
			for (auto& msg : messages)
			{
				FrameInfo info = this->GetFrameInfo(msg);
				IFrameHandler& frameHandler = info.handler.get();

				if (info.size.has_value())
					frame.resize(*info.size);
				else
					frame.clear();

				msg.get().Pack(frame);
				frameHandler.Seal(msg, frame);
				m_protocol.Write(ch->id, frame);
			}
		}

		/**
		 * @brief Asynchronously writes a batch of messages to a specific channel.
		 *
		 * @param ch The channel to write to.
		 * @param messages A span of messages to send.
		 * @param callback The function that will be called when at least one message is written, or when an error occurs.
		 */
		void WriteAsync(std::shared_ptr<Channel> ch, std::span<const std::reference_wrapper<const ITxMessage>> messages, WriteCallback callback)
		{
			if (!this->ChannelExists(ch))
				throw std::invalid_argument("channel not found");

			if (!callback)
				throw std::invalid_argument("callback cannot be null");

			std::vector<uint8_t> buffer;
			std::vector<uint8_t> messagePositions(messages.size());
			for (size_t i = 0; i < messages.size(); ++i)
			{
				const ITxMessage& msg = messages[i];
				FrameInfo info = this->GetFrameInfo(msg);
				IFrameHandler& frameHandler = info.handler.get();
				std::vector<uint8_t> frame((info.size.has_value()) ? (*info.size) : (0));

				msg.Pack(frame);
				frameHandler.Seal(msg, frame);
				buffer.insert(buffer.end(), frame.begin(), frame.end());
				if ((i + 1) < messages.size())
					messagePositions[i + 1] = messagePositions[i] + frame.size();
			}

			m_protocol.WriteAsync(ch->id, buffer,
				[this, ch, callback, messagePositions](const std::error_code& ec, ICommProtocol::ChannelId channelId, size_t size)
				{
					auto it = std::find_if(messagePositions.begin(), messagePositions.end(), [size](size_t p) { return p >= size; });
					const size_t writtenMessageCount = static_cast<size_t>(std::distance(messagePositions.begin(), it));
					callback(ec, ch, writtenMessageCount);

				});
		}

		/**
		 * @brief Asynchronously writes a batch of messages to a specific channel.
		 *
		 * @param ch The channel to write to.
		 * @param messages A span of messages to send.
		 * @return A future which will eventually contain the number of messages that were successfully written.
		 */
		std::future<size_t> WriteAsync(std::shared_ptr<Channel> ch, std::span<const std::reference_wrapper<const ITxMessage>> messages)
		{
			auto promise = std::make_shared<std::promise<size_t>>();
			this->WriteAsync(ch, messages,
				[this, promise](const std::error_code& ec, std::shared_ptr<Channel> ch, size_t size)
				{
					if (ec)
						promise->set_exception(std::make_exception_ptr(std::runtime_error(std::format("WriteAsync error: {}", ec.message()))));
					else
						promise->set_value(size);
				});
			return promise->get_future();
		}

	private:
		void ChannelEventHandler(ICommProtocol::ChannelId channelId, ICommProtocol::ChannelEventType eventType)
		{

			if (eventType == ICommProtocol::ChannelEventType::ChannelAdded)
			{
				if (this->GetChannelById(channelId))
					throw std::logic_error(std::format("a channel with the id {} already exists", channelId));

				std::lock_guard<std::mutex> lock(m_mutex);
				m_channels.push_back(std::make_shared<Channel>(channelId));
			}
			else if (eventType == ICommProtocol::ChannelEventType::ChannelRemoved)
			{
				std::lock_guard<std::mutex> lock(m_mutex);

				auto it = std::find_if(m_channels.begin(), m_channels.end(), [channelId](const auto& ch) { return ch->id == channelId; });
				if (it == m_channels.end())
					std::invalid_argument("channel not found");
				(void)m_channels.erase(it);
			}
			else
			{
				throw std::logic_error(std::format(
					"Unhandled ICommProtocol::ChannelEventType in ChannelEventHandler: The event type with value {} is not implemented.",
					static_cast<int>(eventType)
				));
			}
		}

		bool ChannelExists(std::shared_ptr<Channel> ch) const
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			return std::find(m_channels.begin(), m_channels.end(), ch) != m_channels.end();
		}

		void ValidatePrototypes(std::span<const RxMessagePrototype> prototypes) const
		{
			if (prototypes.empty())
				throw std::invalid_argument("prototypes cannot be empty");

			for (auto it = prototypes.begin(); it != prototypes.end(); ++it)
			{
				const std::type_index typeIndex(typeid(it->get()));
				const size_t count = std::count_if(it, prototypes.end(),
					[&typeIndex](const auto& prototype)
					{
						return std::type_index(typeid(*prototype)) == typeIndex;
					});

				if (count > 1)
					throw std::invalid_argument("duplicate message types found in 'prototypes' list, all prototypes must be unique");
			}
		}

		FrameInfo GetFrameInfo(const IMessage& msg)
		{
			auto frameSize = msg.FrameSize();
			auto headerPattern = msg.HeaderPattern();
			auto footerPattern = msg.FooterPattern();
			auto& frameHandler = msg.FrameHandler();

			if (frameSize.has_value() && frameSize == 0)
				throw std::runtime_error("fixed-sized frame size cannot be 0");

			if (headerPattern.empty())
				throw std::runtime_error("all frames must have a header");

			if (!frameSize.has_value() && footerPattern.empty())
				throw std::runtime_error("variable-sized frames must have a footer");

			return FrameInfo(frameSize, headerPattern, footerPattern, frameHandler);
		}

		void ParseRxMessages(
			std::span<const RxMessagePrototype> prototypes,
			std::vector<uint8_t>& rxBuffer,
			std::vector<std::unique_ptr<IRxMessage>>& messages,
			size_t n,
			std::function<bool()> checkTimeout)
		{
			if (rxBuffer.empty() || n == 0)
				return;

			do
			{
				// most outer do-while loop and frame matching are for
				// unpacking the messages in order of arrival
				struct FrameMatch
				{
					std::reference_wrapper<const IRxMessage> prototype;
					size_t startIndex;
					size_t endIndex;
					FrameMatch(const IRxMessage& rmi, size_t s, size_t e)
						: prototype(rmi),
						startIndex(s),
						endIndex(e)
					{
					}
				};

				std::vector<FrameMatch> frameMatches;

				for (size_t i = 0;
					i < prototypes.size() && messages.size() < n && (!checkTimeout || checkTimeout());
					++i)
				{
					const IRxMessage& prototype = *prototypes[i];
					FrameInfo frameInfo = this->GetFrameInfo(prototype);
					IFrameHandler& frameHandler = frameInfo.handler.get();

					if (frameInfo.size.has_value() && rxBuffer.size() < (*frameInfo.size))
						continue;

					auto itFrameStart = rxBuffer.begin();
					auto itFrameEnd = rxBuffer.begin();
					do
					{
						itFrameStart = std::search(itFrameStart, rxBuffer.end(), frameInfo.headerPattern.begin(), frameInfo.headerPattern.end());
						if (itFrameStart == rxBuffer.end())
							break;

						if (!frameInfo.footerPattern.empty())
						{
							itFrameEnd = std::search(
								(itFrameStart + frameInfo.headerPattern.size()),
								rxBuffer.end(),
								frameInfo.footerPattern.begin(),
								frameInfo.footerPattern.end());

							if (itFrameEnd == rxBuffer.end())
							{
								if (frameInfo.size.has_value())
								{
									if (std::distance(itFrameStart, itFrameEnd) >= (*frameInfo.size))
									{
										// fixed-size mismatch, drop frame
										itFrameStart += frameInfo.headerPattern.size();
										continue;
									}
									break; // wait for more data
								}
								else
								{
									auto itNextFrameStart = std::search(
										(itFrameStart + frameInfo.headerPattern.size()),
										rxBuffer.end(),
										frameInfo.headerPattern.begin(),
										frameInfo.headerPattern.end());

									if (itNextFrameStart != rxBuffer.end())
									{
										// no footer found before next header, drop frame
										itFrameStart = itNextFrameStart;
										continue;
									}
									break; // wait for more data
								}
							}

							itFrameEnd += frameInfo.footerPattern.size();
							if (frameInfo.size.has_value() && std::distance(itFrameStart, itFrameEnd) != (*frameInfo.size))
							{
								// fixed-size mismatch, drop frame
								itFrameStart += frameInfo.headerPattern.size();
								continue;
							}
						}
						else
						{
							if ((*frameInfo.size) > std::distance(itFrameStart, rxBuffer.end()))
								break;

							itFrameEnd = itFrameStart + (*frameInfo.size);
						}

						const std::span<uint8_t> frame(itFrameStart, itFrameEnd);
						if (frameHandler.Validate(prototype, frame))
						{
							(void)frameMatches.emplace_back(
								prototype,
								static_cast<size_t>(std::distance(rxBuffer.begin(), itFrameStart)),
								static_cast<size_t>(std::distance(rxBuffer.begin(), itFrameEnd)));

							itFrameStart += frame.size();
						}
						else
						{
							// adjust previously found frame positions
							for (auto& fm : frameMatches)
							{
								if (fm.startIndex > static_cast<size_t>(std::distance(rxBuffer.begin(), itFrameStart)))
								{
									fm.startIndex -= frameInfo.headerPattern.size();
									fm.endIndex -= frameInfo.headerPattern.size();
								}
							}

							(void)rxBuffer.erase(itFrameStart, itFrameStart + frameInfo.headerPattern.size());

							itFrameStart = rxBuffer.begin();
						}

					} while (itFrameStart != rxBuffer.end() && messages.size() < n && (!checkTimeout || checkTimeout()));
				}

				// no more frames in rx buffer
				// break the loop to read more bytes
				if (frameMatches.empty())
					break;

				// unpack frames in order of arrival

				std::sort(frameMatches.begin(),
					frameMatches.end(),
					[](const auto& a, const auto& b)
					{
						return a.startIndex < b.startIndex;
					});

				size_t offset = 0;
				for (auto& fm : frameMatches)
				{
					const IRxMessage& prototype = fm.prototype.get();
					auto itFrameStart = rxBuffer.begin() + (fm.startIndex - offset);
					auto itFrameEnd = rxBuffer.begin() + (fm.endIndex - offset);
					const std::span<uint8_t> frame(itFrameStart, itFrameEnd);

					std::unique_ptr<IRxMessage> msg(dynamic_cast<IRxMessage*>(prototype.Clone().release()));
					msg->Unpack(frame);
					messages.push_back(std::move(msg));
					(void)rxBuffer.erase(itFrameStart, itFrameEnd);
					offset += frame.size();
				}

			} while (!rxBuffer.empty() && messages.size() < n && (!checkTimeout || checkTimeout()));

			if (!rxBuffer.empty())
			{
				auto itEraseEnd = rxBuffer.end();

				// check for full headers
				for (auto& prototype : prototypes)
				{
					FrameInfo frameInfo = this->GetFrameInfo(*prototype);
					itEraseEnd = std::min(
						itEraseEnd,
						std::search(rxBuffer.begin(), rxBuffer.end(), frameInfo.headerPattern.begin(), frameInfo.headerPattern.end()));
				}

				// erase all data before the earliest full header
				if (itEraseEnd != rxBuffer.end())
				{
					(void)rxBuffer.erase(rxBuffer.begin(), itEraseEnd);
					return;
				}

				// no full headers found, check for partial headers at the end of the buffer
				for (auto& prototype : prototypes)
				{
					FrameInfo frameInfo = this->GetFrameInfo(*prototype);
					const size_t searchLength = std::min(frameInfo.headerPattern.size(), rxBuffer.size());
					auto itLastHeaderStart = std::find(rxBuffer.rbegin(), (rxBuffer.rbegin() + searchLength), frameInfo.headerPattern[0]);
					const auto index = std::distance(rxBuffer.begin(), itLastHeaderStart.base()) - 1;
					auto itFrameStart = rxBuffer.begin() + index;

					if (itFrameStart != ((rxBuffer.rbegin() + searchLength).base() - 1) &&
						std::equal(itFrameStart, rxBuffer.end(), frameInfo.headerPattern.begin()))
					{
						// partial header found, erase all data before it
						(void)rxBuffer.erase(rxBuffer.begin(), itFrameStart);
						return;
					}
				}

				// no partial header found, clear entire buffer
				rxBuffer.clear();
			}
		}
	};

#pragma endregion Comm

}

#endif