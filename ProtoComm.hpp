#ifndef PROTOCOMM_HPP
#define PROTOCOMM_HPP

#include <cstdint>
#include <cstring>
#include <array>
#include <vector>
#include <span>
#include <optional>
#include <numeric>
#include <algorithm>
#include <type_traits>
#include <concepts>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <format>

namespace ProtoComm
{

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
	};

#pragma endregion Frame Handlers

#pragma region Comm

	/**
	 * @brief Specifies the requirements for a communication protocol.
	 *
	 */
	template<typename T>
	concept IsCommProtocol = requires(T t, const T ct, size_t ch, std::span<uint8_t> buf, std::span<const uint8_t> cbuf)
	{
		{ ct.ChannelCount() } -> std::same_as<size_t>;
		{ ct.AvailableReadSize(ch) } -> std::same_as<size_t>;

		{ ct.IsRunning() } -> std::same_as<bool>;
		{ ct.IsRunning(ch) } -> std::same_as<bool>;
		{ t.Stop() } -> std::same_as<void>;
		{ t.Stop(ch) } -> std::same_as<void>;

		{ t.Read(ch, buf) }     -> std::same_as<size_t>;
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
	 *
	 * @tparam RxMessage The concrete receivable message type.
	 * This type must be derived from `ProtoComm::IRxMessage`.
	 *
	 * @tparam TxMessage The concrete transmittable message type.
	 * This type must be derived from `ProtoComm::ITxMessage`.
	 *
	 * @tparam _FrameHandler The concrete frame handler type responsible
	 * for validating incoming (Rx) frames and sealing outgoing (Tx)
	 * frames. This type must be derived from `ProtoComm::IFrameHandler`.
	 * (e.g., `ChecksumFrameHandler`). Defaults to `FrameHandler`.
	 *
	 */
	template<typename Protocol, typename RxMessage, typename TxMessage, typename _FrameHandler = FrameHandler>
		requires
	IsCommProtocol<Protocol>&&
		std::derived_from<RxMessage, IRxMessage>&&
		std::derived_from<TxMessage, ITxMessage>&&
		std::derived_from<_FrameHandler, IFrameHandler>

		class CommStream final
	{
	private:
		Protocol m_protocol;
		_FrameHandler m_frameHandler;

		std::vector<std::vector<uint8_t>> m_rxBuffers;

		std::optional<size_t> m_rxFrameSize;
		std::span<const uint8_t> m_rxHeaderPattern;
		std::span<const uint8_t> m_rxFooterPattern;

		std::optional<size_t> m_txFrameSize;
		std::span<const uint8_t> m_txHeaderPattern;
		std::span<const uint8_t> m_txFooterPattern;

	public:
		CommStream()
		{
			{
				RxMessage msg{};
				m_rxFrameSize = msg.FrameSize();
				m_rxHeaderPattern = msg.HeaderPattern();
				m_rxFooterPattern = msg.FooterPattern();

				if (m_rxFrameSize.has_value() && m_rxFrameSize == 0)
					throw std::runtime_error("fixed-sized frame size cannot be 0");

				if (m_rxHeaderPattern.empty())
					throw std::runtime_error("all frames must have a header");

				if (!m_rxFrameSize.has_value() && m_rxFooterPattern.empty())
					throw std::runtime_error("variable-sized frames must have a footer");
			}

			{
				TxMessage msg{};
				m_txFrameSize = msg.FrameSize();
				m_txHeaderPattern = msg.HeaderPattern();
				m_txFooterPattern = msg.FooterPattern();

				if (m_txFrameSize.has_value() && m_txFrameSize == 0)
					throw std::runtime_error("fixed-sized frame size cannot be 0");

				if (m_txHeaderPattern.empty())
					throw std::runtime_error("all frames must have a header");

				if (!m_txFrameSize.has_value() && m_txFooterPattern.empty())
					throw std::runtime_error("variable-sized frames must have a footer");
			}
		}

		CommStream(const CommStream&) = delete;
		const CommStream& operator=(const CommStream&) = delete;

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
		 * @param ch The channel index from which to read messages.
		 * This must be a value in the range `[0, CommStream::ChannelCount() - 1]`.
		 *
		 * @param n The desired number of messages to read.
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
		std::vector<RxMessage> Read(size_t ch, size_t n = 1, std::chrono::milliseconds timeout = std::chrono::milliseconds::zero())
		{
			using clock = std::chrono::high_resolution_clock;
			using time_point = clock::time_point;

			constexpr std::chrono::milliseconds pollingPeriod = std::chrono::milliseconds(10);

			if (ch >= m_protocol.ChannelCount())
				throw std::out_of_range(std::format("invalid channel index: {}", ch));

			auto& m_rxBuffer = m_rxBuffers[ch];
			std::vector<RxMessage> messages;
			const time_point t1 = clock::now();

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

				const size_t rxBufferOldSize = m_rxBuffer.size();
				m_rxBuffer.resize(rxBufferOldSize + readSize);

				(void)m_protocol.Read(ch, std::span<uint8_t>(m_rxBuffer.begin() + rxBufferOldSize, readSize));
				if (m_rxFrameSize.has_value() && m_rxBuffer.size() < (*m_rxFrameSize))
				{
					std::this_thread::sleep_for(pollingPeriod);
					continue;
				}


				auto itFrameStart = m_rxBuffer.begin();
				auto itFrameEnd = m_rxBuffer.begin();
				do
				{
					itFrameStart = std::search(itFrameStart, m_rxBuffer.end(), m_rxHeaderPattern.begin(), m_rxHeaderPattern.end());
					if (itFrameStart == m_rxBuffer.end())
						break;

					if (!m_rxFooterPattern.empty())
					{
						itFrameEnd = std::search(
							(itFrameStart + m_rxHeaderPattern.size()),
							m_rxBuffer.end(),
							m_rxFooterPattern.begin(),
							m_rxFooterPattern.end());

						if (itFrameEnd == m_rxBuffer.end())
						{
							if (m_rxFrameSize.has_value())
							{
								if (std::distance(itFrameStart, itFrameEnd) >= (*m_rxFrameSize))
								{
									// fixed-size mismatch, drop frame
									itFrameStart += m_rxHeaderPattern.size();
									continue;
								}
								break; // wait for more data
							}
							else
							{
								auto itNextFrameStart = std::search(
									(itFrameStart + m_rxHeaderPattern.size()),
									m_rxBuffer.end(),
									m_rxHeaderPattern.begin(),
									m_rxHeaderPattern.end());

								if (itNextFrameStart != m_rxBuffer.end())
								{
									// no footer found before next header, drop frame
									itFrameStart = itNextFrameStart;
									continue;
								}
								break; // wait for more data
							}
						}

						itFrameEnd += m_rxFooterPattern.size();
						if (m_rxFrameSize.has_value() && std::distance(itFrameStart, itFrameEnd) != (*m_rxFrameSize))
						{
							// fixed-size mismatch, drop frame
							itFrameStart += m_rxHeaderPattern.size();
							continue;
						}
					}
					else
					{
						if ((*m_rxFrameSize) > std::distance(itFrameStart, m_rxBuffer.end()))
							break;

						itFrameEnd = itFrameStart + (*m_rxFrameSize);
					}

					const std::span<const uint8_t> frame(itFrameStart, itFrameEnd);
					if (m_frameHandler.Validate(RxMessage(), frame))
					{
						RxMessage& msg = messages.emplace_back();
						msg.Unpack(frame);

						itFrameStart = itFrameEnd;
					}
					else
					{
						itFrameStart += m_rxHeaderPattern.size();
					}

				} while (itFrameStart < m_rxBuffer.end() && messages.size() < n && checkTimeout());

				// remove all data except the last, possible incomplete message
				if (itFrameStart == m_rxBuffer.end() && !m_rxBuffer.empty())
				{
					// check for partial header
					const size_t searchLength = std::min(m_rxHeaderPattern.size(), m_rxBuffer.size());
					auto itLastHeaderStart = std::find(m_rxBuffer.rbegin(), (m_rxBuffer.rbegin() + searchLength), m_rxHeaderPattern[0]);
					const auto index = std::distance(m_rxBuffer.begin(), itLastHeaderStart.base()) - 1;
					itFrameStart = m_rxBuffer.begin() + index;

					if (itFrameStart != ((m_rxBuffer.rbegin() + searchLength).base() - 1) &&
						std::equal(itFrameStart, m_rxBuffer.end(), m_rxHeaderPattern.begin()))
					{
						// partial header found
						(void)m_rxBuffer.erase(m_rxBuffer.begin(), itFrameStart);
					}
					else
					{
						m_rxBuffer.clear();
					}
				}
				else if (itFrameStart != m_rxBuffer.begin())
				{
					(void)m_rxBuffer.erase(m_rxBuffer.begin(), itFrameStart);
				}

				std::this_thread::sleep_for(pollingPeriod);

			} while (messages.size() < n && checkTimeout());

			return messages;
		}

		/**
		 * @brief Writes a single message to a specific channel.
		 *
		 * @param ch The channel index which the message will be sent.
		 * This must be a value in the range `[0, CommStream::ChannelCount() - 1]`.
		 *
		 * @param msg The message to be sent.
		 */
		void Write(size_t ch, const TxMessage& msg)
		{
			this->Write(ch, std::span<const TxMessage>(&msg, 1));
		}

		/**
		 * @brief Writes a batch of messages to a specific channel.
		 *
		 * @param ch The channel index which the message will be sent.
		 * This must be a value in the range `[0, CommStream::ChannelCount() - 1]`.
		 *
		 * @param messages The messages to be sent.
		 */
		void Write(size_t ch, std::span<const TxMessage> messages)
		{
			std::vector<uint8_t> frame((m_txFrameSize.has_value()) ? (*m_txFrameSize) : (0));
			for (const TxMessage& msg : messages)
			{
				if (!m_txFrameSize.has_value())
					frame.clear();

				msg.Pack(frame);
				m_frameHandler.Seal(msg, std::span<uint8_t>(frame.begin(), frame.end()));
				m_protocol.Write(ch, std::span<const uint8_t>(frame.begin(), frame.end()));
			}
		}
	};

#pragma endregion Comm

}

#endif