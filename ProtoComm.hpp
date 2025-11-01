#ifndef PROTOCOMM_HPP
#define PROTOCOMM_HPP

#include <cstdint>
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
    class IRxMessage : public IMessage
    {
    public:
        using Frame = typename IMessage;

        virtual ~IRxMessage() = default;

        /**
         * @brief Unpacks (deserializes) data from a raw frame into this object.
         *
         * The implementation of this pure virtual function should parse the
         * provided 'frame' and populate the members of the derived class.
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
    class ITxMessage : public IMessage
    {
        virtual ~ITxMessage() = default;

        /**
         * @brief Packs (serializes) the data from this object into a raw frame.
         *
         * The implementation of this pure virtual function should fill the
         * provided 'frame' with the data held in the derived class members.
         * This method is 'const' as it should not modify the state of the
         * message object itself, only write its state into the frame.
         *
         * @param frame The raw data frame that will be filled and sent over the communication channel.
         */
        virtual void Pack(std::span<uint8_t> frame) const = 0;
    };

#pragma endregion Messages

#pragma region Frame Validators

    /**
     * @brief Interface for a frame validator functor.
     *
     * This class defines the abstract interface for a functor
     * responsible for validating the integrity of a raw data frame.
     */
    class IFrameValidator
    {
    public:
        virtual ~IFrameValidator() = default;

        /**
         * @brief Validates a raw data frame against a message's rules.
         *
         * @param msg The message definition, used to get validation rules
         * (e.g., header/footer patterns).
         * @param frame The raw data frame to be validated.
         * @return true if the frame is valid, false otherwise.
         */
        virtual bool operator()(const IMessage& msg, std::span<const uint8_t> frame) = 0;
    };

    /**
     * @brief A basic frame validator that checks for frame size, header and footer patterns.
     *
     * This class provides a concrete implementation of IFrameValidator that
     * only validates the frame size and the presence of correct header and footer byte patterns.
     */
    class FrameValidator : public IFrameValidator
    {
    public:
        virtual ~FrameValidator() = default;

        virtual bool operator()(const IMessage& msg, std::span<const uint8_t> frame) override
        {
            const auto frameSize = msg.FrameSize();
            if (frameSize.has_value() && frame.size() != (*frameSize))
                return false;

            auto headerPattern = msg.HeaderPattern();
            if (!std::equal(headerPattern.begin(), headerPattern.end(), frame.begin()))
                return false;

            auto footerPattern = msg.FooterPattern();
            if (footerPattern.size() > 0 && !std::equal(footerPattern.begin(), footerPattern.end(), (frame.end() - footerPattern.size())))
                return false;

            return true;
        }
    };

    /**
     * @brief A specialized validator that checks headers, footers, and a checksum.
     *
     * @tparam TChecksum The data type of the checksum (e.g., uint8_t, uint16_t).
     * @tparam TData The data type of the elements being summed (default: uint8_t).
     * @tparam init The initial value for the checksum calculation (default: 0).
     * @tparam BinaryOp The binary operation for checksum calculation (default: std::plus).
     */
    template<typename TChecksum = uint8_t, typename TData = uint8_t, TData init = 0, typename BinaryOp = std::plus<TData>>
        requires std::is_arithmetic_v<TChecksum>&& std::is_arithmetic_v<TData>
    class ChecksumValidator : public FrameValidator
    {
    public:
        virtual bool operator()(const IMessage& msg, std::span<const uint8_t>& frame) override
        {
            if (!FrameValidator::operator()(msg, frame))
                return false;

            auto payloadBegin = frame.begin() + msg.HeaderPattern().size();
            auto payloadEnd = frame.end() - msg.FooterPattern().size() - sizeof(TChecksum);

            TChecksum expected;
            std::memcpy(&expected, &(*payloadEnd), sizeof(TChecksum));

            const TChecksum calculated = static_cast<TChecksum>(std::accumulate(payloadBegin, payloadEnd, init, BinaryOp()));

            return calculated == expected;
        }
    };

#pragma endregion Frame Validators

#pragma region Comm

    /**
     * @brief Specifies the requirements for a communication protocol.
     *
     */
    template<typename T>
    concept IsCommProtocol = requires(T t, std::span<uint8_t> buf, std::span<const uint8_t> cbuf)
    {
        { t.IsConnected() } -> std::same_as<bool>;
        { t.Disconnect() };
        { t.AvailableReadSize() } -> std::same_as<size_t>;
        { t.Read(buf) }     -> std::same_as<size_t>;
        { t.Write(cbuf) };
    };

    /**
     * @brief Specifies the type ``T`` has a ``Connect`` method that takes ``Args`` parameters.
     *
     */
    template<typename T, typename... Args>
    concept Connectable = requires(T t, Args... args)
    {
        { t.Connect(std::forward<Args>(args)...) } -> std::same_as<bool>;
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
     * @tparam Validator The concrete frame validator type (e.g.,
     * `ChecksumFrameValidator`). This type must be derived from `ProtoComm::IFrameValidator`.
     *
     */
    template<typename Protocol, typename RxMessage, typename TxMessage, typename Validator = FrameValidator>
        requires
    IsCommProtocol<Protocol>&&
        std::derived_from<RxMessage, IRxMessage>&&
        std::derived_from<TxMessage, ITxMessage>&&
        std::derived_from<Validator, IFrameValidator>

        class CommStream final
    {
    private:
        Protocol m_protocol;
        Validator m_validator;

        std::vector<uint8_t> m_rxBuffer;

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
                RxMessage msg;
                m_rxFrameSize = msg.FrameSize();
                m_rxHeaderPattern = msg.HeaderPattern();
                m_rxFooterPattern = msg.FooterPattern();

                if (m_rxHeaderPattern.empty())
                {
                    throw std::runtime_error("all frames must have a header");
                }

                if (!m_rxFrameSize.has_value() && m_rxFooterPattern.empty())
                {
                    throw std::runtime_error("variable-sized frames must have a footer");
                }
            }

            {
                TxMessage msg;
                m_txFrameSize = msg.FrameSize();
                m_txHeaderPattern = msg.HeaderPattern();
                m_txFooterPattern = msg.FooterPattern();

                if (m_txHeaderPattern.empty())
                {
                    throw std::runtime_error("all frames must have a header");
                }

                if (!m_txFrameSize.has_value() && m_txFooterPattern.empty())
                {
                    throw std::runtime_error("variable-sized frames must have a footer");
                }
            }
        }

        /**
         * @brief Establishes a connection using the underlying protocol.
         *
         * @tparam Args The types of arguments required by the protocol's `Connect` method.
         * @param args The arguments required by the protocol's `Connect` method.
         * @return `true` if the connection was successful, `false` if it failed or if the stream was already connected.
         */
        template<typename... Args>
            requires Connectable<Protocol, Args...>
        bool Start(Args... args)
        {
            if (m_protocol.IsConnected())
                return false;
            return m_protocol.Connect(std::forward<Args>(args)...);
        }

        /**
         * @brief Disconnects the stream.
         *
         */
        void Stop()
        {
            if (m_protocol.IsConnected())
                m_protocol.Disconnect();
        }

        /**
         * @brief Blocks until 'n' messages are read or a timeout occurs.
         *
         * This function attempts to read a specified number of messages from
         * the stream, blocking the calling thread until the conditions are met.
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
         * @return A vector containing the messages that
         * were successfully parsed. This vector may contain fewer than 'n'
         * messages (or be empty) if the timeout was reached.
         */
        std::vector<RxMessage> Read(size_t n = 1, std::chrono::milliseconds timeout = std::chrono::milliseconds::zero())
        {
            using clock = std::chrono::high_resolution_clock;
            using time_point = clock::time_point;

            constexpr std::chrono::milliseconds pollingPeriod = 10;

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
                const size_t readSize = m_protocol.AvailableReadSize();
                if (readSize == 0)
                {
                    std::this_thread::sleep_for(pollingPeriod);
                    continue;
                }

                const size_t rxBufferOldSize = m_rxBuffer.size();
                m_rxBuffer.resize(rxBufferOldSize + readSize);

                (void)m_protocol.Read(std::span<uint8_t>(m_rxBuffer.begin() + rxBufferOldSize, readSize));
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

                    if (m_rxFooterPattern.size() != 0)
                    {
                        itFrameEnd = std::search((itFrameStart + m_rxHeaderPattern.size()), m_rxBuffer.end(), m_rxFooterPattern.begin(), m_rxFooterPattern.end());
                        itFrameEnd += m_rxFooterPattern.size();
                        if (m_rxFrameSize.has_value() && std::distance(itFrameStart, itFrameEnd) != (*m_rxFrameSize)) // fixed-size mismatch, drop frame
                        {
                            itFrameStart = itFrameEnd;
                            continue;
                        }
                    }
                    else
                    {
                        itFrameEnd = itFrameStart + (*m_rxFrameSize);
                    }

                    if (itFrameEnd > m_rxBuffer.end())
                        break;

                    const std::span<const uint8_t> frame(itFrameStart, itFrameEnd);
                    if (m_validator(RxMessage(), frame))
                    {
                        RxMessage& msg = messages.emplace_back();
                        msg.Unpack(frame);

                        itFrameStart = itFrameEnd;
                    }
                    else
                    {
                        itFrameStart++;
                    }


                } while (itFrameStart < m_rxBuffer.end() && messages.size() < n && checkTimeout());

                // remove all data except the last, possible incomplete message
                if (itFrameStart == m_rxBuffer.end() && !m_rxBuffer.empty())
                {
                    // check for partial header
                    const size_t searchLength = std::min((m_rxHeaderPattern.size() - 1), (m_rxBuffer.size() - 1));
                    auto itLastHeaderStart = std::find(m_rxBuffer.rbegin(), (m_rxBuffer.rbegin() + searchLength), m_rxHeaderPattern[0]);
                    const auto phc = std::distance(m_rxBuffer.rbegin(), itLastHeaderStart);
                    itFrameStart = m_rxBuffer.end() - phc - 1;

                    if (phc > 0 &&
                        phc < m_rxHeaderPattern.size() &&
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
                else
                {
                    (void)m_rxBuffer.erase(m_rxBuffer.begin(), itFrameStart);
                }

                std::this_thread::sleep_for(pollingPeriod);

            } while (messages.size() < n && checkTimeout());

            return messages;
        }

        /**
         * @brief Writes a single message to the stream.
         *
         * @param msg The message to be transmitted.
         *
         */
        void Write(const TxMessage& msg)
        {
            this->Write(std::span<const TxMessage>(&msg, 1));
        }

        /**
         * @brief Writes a batch of messages to the stream.
         *
         * @param messages The messages to be transmitted.
         *
         */
        void Write(std::span<const TxMessage> messages)
        {
            // TODO
        }
    };

#pragma endregion Comm

}

#endif