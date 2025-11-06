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
		 * @param msg A const reference to the `IRxMessage` type, used
		 * to retrieve validation rules (e.g., `HeaderPattern()`).
		 *
		 * @param frame Raw frame to be validated.
		 * @return true if the frame's integrity is valid, false otherwise.
		 */
		virtual bool Validate(const IRxMessage& msg, std::span<const uint8_t> frame) const = 0;

		/**
		 * @brief Finalizes ("seals") an outgoing raw data frame.
		 *
		 * @param msg A const reference to the `ITxMessage` type, used
		 * to retrieve frame rules (e.g., `HeaderPattern()`).
		 *
		 * @param frame Raw frame that will be modified in-place.
		 */
		virtual void Seal(const ITxMessage& msg, std::span<uint8_t> frame) const = 0;
	};

	/**
	 * @brief A base implementation of IFrameHandler that validates and seals
	 * frame headers and footers.
	 *
	 * This class provides the fundamental logic for checking
	 * header/footer patterns on `Validate` and writing them on `Seal`.
	 *
	 * It is intended to be used as a base class for more specialized
	 * handlers (like `ChecksumFrameHandler`), which can call these
	 * methods before adding their own logic (e.g., checksum validation).
	 */
	class FrameHandler : public IFrameHandler
	{
	public:
		virtual ~FrameHandler() = default;

		virtual bool Validate(const IRxMessage& msg, std::span<const uint8_t> frame) const override
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

		virtual void Seal(const ITxMessage& msg, std::span<uint8_t> frame) const override
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
	 * @brief A specialized `FrameHandler` that adds checksum validation.
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

		virtual bool Validate(const IRxMessage& msg, std::span<const uint8_t> frame) const override
		{
			if (!FrameHandler::Validate(msg, frame))
				return false;

			auto checksumPosition = frame.end() - msg.FooterPattern().size() - sizeof(TChecksum);
			TChecksum expected;
			(void)std::memcpy(&expected, &(*checksumPosition), sizeof(TChecksum));

			const TChecksum calculated = this->CalculateChecksum(msg, frame);

			return calculated == expected;
		}

		virtual void Seal(const ITxMessage& msg, std::span<uint8_t> frame) const override
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
	 * @brief Callback function type for asynchronous protocol reads.
	 */
	using ProtocolReadCallback = std::function<void(const std::error_code&, size_t, std::span<const uint8_t>)>;

	/**
	 * @brief Specifies the requirements for a communication protocol.
	 *
	 */
	template<typename T>
	concept IsCommProtocol = requires(T t, const T ct, size_t ch, std::span<uint8_t> buf, std::span<const uint8_t> cbuf, ProtocolReadCallback readCallback)
	{
		{ ct.ChannelCount() } -> std::same_as<size_t>;
		{ ct.AvailableReadSize(ch) } -> std::same_as<size_t>;

		{ ct.IsRunning() } -> std::same_as<bool>;
		{ ct.IsRunning(ch) } -> std::same_as<bool>;
		{ t.Stop() } -> std::same_as<void>;
		{ t.Stop(ch) } -> std::same_as<void>;

		{ t.Read(ch, buf) }     -> std::same_as<size_t>;
		{ t.ReadAsync(ch, readCallback) } -> std::same_as<void>;

		{ t.Write(ch, cbuf) } -> std::same_as<void>;
	};

	/**
	 * @brief Specifies the type ``T`` has a ``Start`` method that takes ``Args`` parameters.
	 *
	 */
	template<typename T, typename... Args>
	concept Startable = requires(T t, Args... args)
	{
		{ t.Start(std::forward<Args>(args)...) } -> std::same_as<bool>;
	};

	/**
	 * @brief Manages a high-level, message-based communication channel.
	 *
	 * This class is the primary user-facing component of the ProtoComm library.
	 * It handles connection management, frame parsing, and validation,
	 * allowing the user to work directly with their defined message objects.
	 *
	 * @tparam Protocol The concrete transport protocol type (e.g., `TcpClient`, `SerialPort`).
	 * This type must satisfy the `IsCommProtocol` concept.
	 */
	template<typename Protocol>
		requires IsCommProtocol<Protocol>
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

	private:
		Protocol m_protocol;
		std::vector<std::vector<uint8_t>> m_rxBuffers;

	public:
		/**
		 * @brief Callback function type for async read operations.
		 * @return `true` to continue reading, `false` to stop.
		 */
		using ReadCallback = std::function<bool(const std::error_code&, size_t, std::span<std::unique_ptr<IRxMessage>>)>;

	public:
		CommStream() = default;
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
		 * @brief Gets the current number of active channels from the protocol.
		 *
		 * @return The number of active channels.
		 */
		size_t ChannelCount() const
		{
			return m_protocol.ChannelCount();
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
		 *
		 * @return true if the specified channel is running, false otherwise.
		 */
		bool IsRunning(size_t ch) const
		{
			return m_protocol.IsRunning(ch);
		}

		/**
		 * @brief Starts the underlying communication protocol.
		 *
		 * @tparam Args The types of arguments required by the protocol's `Start` method.
		 * @param args The arguments required by the protocol's `Start` method.
		 *
		 * @return `true` if the protocol was started successfully, `false` if it failed.
		 */
		template<typename... Args>
			requires Startable<Protocol, Args...>
		bool Start(Args... args)
		{
			const bool result = m_protocol.Start(std::forward<Args>(args)...);

			if (result)
				m_rxBuffers.resize(m_protocol.ChannelCount());

			return result;
		}

		/**
		 * @brief Stops the entire underlying communication protocol.
		 *
		 */
		void Stop()
		{
			m_protocol.Stop();
			m_rxBuffers.clear();
		}

		/**
		 * @brief Stops a single, specific communication channel.
		 *
		 * @param ch The channel index to stop. This must be a value
		 * in the range `[0, ChannelCount() - 1]`.
		 */
		void Stop(size_t ch)
		{
			const size_t oldChannelCount = m_protocol.ChannelCount();
			if (ch >= oldChannelCount)
				throw std::out_of_range(std::format("invalid channel index: {}", ch));

			m_protocol.Stop(ch);
			if (oldChannelCount != (m_protocol.ChannelCount() + 1))
				throw std::logic_error(
					std::format(
						"Protocol contract violation: After Stop({}), ChannelCount() was {} but expected {}. Protocol must decrease count by exactly 1.",
						ch, m_protocol.ChannelCount(), (oldChannelCount - 1)
					));

			(void)m_rxBuffers.erase(m_rxBuffers.begin() + ch);
		}

		/**
		 * @brief Removes all inactive or disconnected channels.
		 *
		 * @return The number of channels that were removed.
		 */
		size_t Prune()
		{
			size_t channelsRemoved = 0;
			for (size_t ch = m_protocol.ChannelCount(); (ch--) > 0; )
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
		 * @brief Blocks until 'n' messages are read from a specific channel
		 * or a timeout occurs.
		 *
		 * This function attempts to read a specified number of messages from
		 * a specific channel, blocking the calling thread until the
		 * conditions are met.
		 *
		 * @tparam RxMessages A variadic template pack of the concrete `IRxMessage` types to listen for.
		 *
		 * @param ch The channel index from which to read messages.
		 * This must be a value in the range `[0, CommStream::ChannelCount() - 1]`.
		 *
		 * @param n The desired number of messages to read.
		 *
		 * @param timeout The maximum duration to wait for the messages.
		 * - If `std::chrono::milliseconds::zero()` (the default), this
		 * function will block indefinitely until exactly 'n'
		 * messages have been received.
		 * - If greater than zero, the function will return after the
		 * timeout expires, even if fewer than 'n' messages were
		 * received.
		 *
		 * @return A vector containing the messages that were successfully parsed.
		 */
		template<typename... RxMessages>
			requires (std::derived_from<RxMessages, IRxMessage>, ...)
		std::vector<std::unique_ptr<IRxMessage>> Read(
			size_t ch,
			size_t n = 1,
			std::chrono::milliseconds timeout = std::chrono::milliseconds::zero())
		{
			const std::array<std::unique_ptr<const IRxMessage>, sizeof...(RxMessages)> instances = { std::make_unique<const RxMessages>()... };

			std::vector<std::reference_wrapper<const IRxMessage>> prototypes;
			prototypes.reserve(sizeof...(RxMessages));

			for (auto& prototype : instances)
				prototypes.push_back(std::move(*prototype));

			return this->Read(ch, prototypes, n, timeout);
		}

		/**
		 * @brief Blocks until 'n' messages are read from a specific channel
		 * or a timeout occurs.
		 *
		 * This function attempts to read a specified number of messages from
		 * a specific channel, blocking the calling thread until the
		 * conditions are met.
		 *
		 * @param ch The channel index from which to read messages.
		 * This must be a value in the range `[0, CommStream::ChannelCount() - 1]`.
		 *
		 * @param prototypes A span of prototype instances that specifiy the types of messages to attempt to parse from the incoming data.
		 * @param n The desired number of messages to read.
		 *
		 * @param timeout The maximum duration to wait for the messages.
		 * - If `std::chrono::milliseconds::zero()` (the default), this
		 * function will block indefinitely until exactly 'n'
		 * messages have been received.
		 * - If greater than zero, the function will return after the
		 * timeout expires, even if fewer than 'n' messages were
		 * received.
		 *
		 * @return A vector containing the messages that were successfully parsed.
		 */
		std::vector<std::unique_ptr<IRxMessage>> Read(
			size_t ch,
			std::span<const std::reference_wrapper<const IRxMessage>> prototypes,
			size_t n = 1,
			std::chrono::milliseconds timeout = std::chrono::milliseconds::zero())
		{
			using clock = std::chrono::high_resolution_clock;

			constexpr std::chrono::milliseconds pollingPeriod = std::chrono::milliseconds(10);

			if (ch >= m_protocol.ChannelCount())
				throw std::out_of_range(std::format("invalid channel index: {}", ch));

			this->ValidatePrototypes(prototypes);

			auto& rxBuffer = m_rxBuffers[ch];
			std::vector<std::unique_ptr<IRxMessage>> messages;
			const clock::time_point t1 = clock::now();

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
				const size_t readSize = m_protocol.AvailableReadSize(ch);
				if (readSize == 0)
				{
					std::this_thread::sleep_for(pollingPeriod);
					continue;
				}

				const size_t rxBufferOldSize = rxBuffer.size();
				rxBuffer.resize(rxBufferOldSize + readSize);
				(void)m_protocol.Read(ch, std::span<uint8_t>(rxBuffer.begin() + rxBufferOldSize, readSize));
				this->ParseRxMessages(prototypes, rxBuffer, messages, n, checkTimeout);

				std::this_thread::sleep_for(pollingPeriod);

			} while (messages.size() < n && checkTimeout());

			return messages;
		}

		/**
		 * @brief Reads messages from a specific channel asynchronously.
		 *
		 * @param ch The channel index from which to read messages.
		 * This must be a value in the range `[0, CommStream::ChannelCount() - 1]`.
		 *
		 * @param prototypes A span of prototype instances that specifiy the types of messages to attempt to parse from the incoming data.
		 * @param callback The function that will be called when at least one message is parsed, or when an error occurs.
		 */
		void ReadAsync(
			size_t ch,
			std::span<const std::reference_wrapper<const IRxMessage>> prototypes,
			ReadCallback callback)
		{
			if (ch >= m_protocol.ChannelCount())
				throw std::out_of_range(std::format("invalid channel index: {}", ch));

			if (!callback)
				throw std::invalid_argument("callback cannot be null");

			this->ValidatePrototypes(prototypes);

			auto rxBuffer = std::make_shared<std::vector<uint8_t>>(m_rxBuffers[ch].begin(), m_rxBuffers[ch].end());
			auto protocolCallback = std::make_shared<ProtocolReadCallback>();

			m_rxBuffers[ch].clear();

			(*protocolCallback) = [this, prototypes, callback, rxBuffer, protocolCallback](const std::error_code& ec, size_t ch, std::span<const uint8_t> data)
				{
					std::vector<std::unique_ptr<IRxMessage>> messages;

					if (!ec && data.size() > 0)
					{
						const size_t rxBufferOldSize = rxBuffer->size();
						rxBuffer->resize(rxBufferOldSize + data.size());
						(void)std::copy(data.begin(), data.end(), rxBuffer->begin() + rxBufferOldSize);
						this->ParseRxMessages(prototypes, *rxBuffer, messages, std::numeric_limits<size_t>::max(), nullptr); // read all available messages
					}

					if (ec || !messages.empty())
					{
						if (callback(ec, ch, messages))
							m_protocol.ReadAsync(ch, *protocolCallback);
					}
					else
					{
						m_protocol.ReadAsync(ch, *protocolCallback);
					}
				};

			m_protocol.ReadAsync(ch, *protocolCallback);
		}

		/**
		 * @brief Reads at least 'n' messages from a specific channel asynchronously.
		 *
		 * @param ch The channel index from which to read messages.
		 * This must be a value in the range `[0, CommStream::ChannelCount() - 1]`.
		 *
		 * @param prototypes A span of prototype instances that specifiy the types of messages to attempt to parse from the incoming data.
		 * @param n The desired number of messages to read.
		 *
		 * @return A future which will return a vector containing the parsed messages on success.
		 */
		std::future<std::vector<std::unique_ptr<IRxMessage>>> ReadAsync(
			size_t ch,
			std::span<const std::reference_wrapper<const IRxMessage>> prototypes,
			size_t n)
		{
			if (n == 0)
				return std::async(std::launch::deferred, []() { return std::vector<std::unique_ptr<IRxMessage>>{}; });

			auto messages = std::make_shared<std::vector<std::unique_ptr<IRxMessage>>>();
			auto promise = std::make_shared<std::promise<std::vector<std::unique_ptr<IRxMessage>>>>();
			this->ReadAsync(ch, prototypes,
				[n, messages, promise](const std::error_code& ec, size_t ch, std::span<std::unique_ptr<IRxMessage>> newMessages) -> bool
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
		 * @brief Writes a message to a specific channel.
		 *
		 * @param ch The channel index which the message will be sent.
		 * This must be a value in the range `[0, CommStream::ChannelCount() - 1]`.
		 *
		 * @param msg The message to be sent.
		 */
		void Write(size_t ch, const ITxMessage& msg)
		{
			FrameInfo info = this->GetFrameInfo(msg);
			IFrameHandler& frameHandler = info.handler.get();

			std::vector<uint8_t> frame((info.size.has_value()) ? (*info.size) : (0));

			msg.Pack(frame);
			frameHandler.Seal(msg, frame);
			m_protocol.Write(ch, frame);
		}

	private:
		void ValidatePrototypes(std::span<const std::reference_wrapper<const IRxMessage>> prototypes) const
		{
			if (prototypes.empty())
				throw std::invalid_argument("prototypes cannot be empty");

			for (auto it = prototypes.begin(); it != prototypes.end(); ++it)
			{
				const std::type_index typeIndex(typeid(it->get()));
				const size_t count = std::count_if(it, prototypes.end(),
					[&typeIndex](const auto& mt)
					{
						return std::type_index(typeid(mt.get())) == typeIndex;
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
			std::span<const std::reference_wrapper<const IRxMessage>> prototypes,
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
					const IRxMessage& prototype = prototypes[i].get();
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
					FrameInfo frameInfo = this->GetFrameInfo(prototype.get());
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
					FrameInfo frameInfo = this->GetFrameInfo(prototype.get());
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