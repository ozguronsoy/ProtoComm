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
#include <stdexcept>

namespace ProtoComm
{

#pragma region Messages

    /**
     * @brief Provides the core type definition 'Frame', which represents the underlying data buffer for a message.
     *
     */
    class IMessage
    {
    public:
        /**
         * The underlying data buffer for the message.
         *
         */
        using Frame = std::span<uint8_t>;

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
        virtual void Unpack(const Frame& frame) = 0;
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
    public:
        using Frame = typename IMessage::Frame;

        virtual ~ITxMessage() = default;

        /**
         * @brief Packs (serializes) the data from this object into a raw frame.
         *
         * The implementation of this pure virtual function should fill the
         * provided 'frame' with the data held in the derived class members.
         * This method is 'const' as it should not modify the state of the
         * message object itself, only write its state into the frame.
         *
         * @param frame A reference to the raw data frame that will be filled and sent over the communication channel.
         */
        virtual void Pack(Frame& frame) const = 0;
    };

#pragma endregion Messages

#pragma region Frame Validators

    /**
     * @brief Interface for a frame validator functor.
     *
     * This class defines the abstract interface for a callable object (functor)
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
        virtual bool operator()(const IMessage& msg, const IMessage::Frame& frame) = 0;
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

        virtual bool operator()(const IMessage& msg, const IMessage::Frame& frame) override
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
    class ChecksumValidator final : public FrameValidator
    {
        bool operator()(const IMessage& msg, const IMessage::Frame& frame) override
        {
            if (!FrameValidator::operator()(msg, frame))
                return false;

            auto payloadBegin = frame.begin() + msg.HeaderPattern().size();
            auto payloadEnd = frame.end() - msg.FooterPattern().size() - sizeof(TChecksum);

            TChecksum expected;
            std::memcpy(&expected, &(*payloadEnd), sizeof(TChecksum));

            const TChecksum calculated = std::accumulate(payloadBegin, payloadEnd, init, BinaryOp());

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

                if (m_rxHeaderPattern.size() == 0)
                {
                    throw std::runtime_error("all frames must have a header");
                }

                if (!m_rxFrameSize.has_value() && m_rxFooterPattern.size() == 0)
                {
                    throw std::runtime_error("variable-sized frames must have a footer");
                }
            }

            {
                TxMessage msg;
                m_txFrameSize = msg.FrameSize();
                m_txHeaderPattern = msg.HeaderPattern();
                m_txFooterPattern = msg.FooterPattern();

                if (m_txHeaderPattern.size() == 0)
                {
                    throw std::runtime_error("all frames must have a header");
                }

                if (!m_txFrameSize.has_value() && m_txFooterPattern.size() == 0)
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
         * @brief Reads available data, parses messages, and returns them.
         *
         * @return A vector containing all messages that were successfully parsed in this cycle.
         */
        std::vector<RxMessage> Read()
        {
            std::vector<RxMessage> messages;

            const size_t readSize = m_protocol.AvailableReadSize();
            const size_t rxBufferOldSize = m_rxBuffer.size();
            m_rxBuffer.resize(rxBufferOldSize + readSize);

            (void)m_protocol.Read(std::span<uint8_t>(m_rxBuffer.begin() + rxBufferOldSize, readSize));

            auto itHeader = m_rxBuffer.begin();
            auto itFooter = m_rxBuffer.begin();
            do
            {
                itHeader = std::search(itHeader, m_rxBuffer.end(), m_rxHeaderPattern.begin(), m_rxHeaderPattern.end());
                if (itHeader == m_rxBuffer.end())
                    break;

                if (m_rxFooterPattern.size() != 0)
                {
                    itFooter = std::search(itHeader, m_rxBuffer.end(), m_rxFooterPattern.begin(), m_rxFooterPattern.end());
                    if (m_rxFrameSize.has_value() && std::distance(itHeader, itFooter) != (*m_rxFrameSize)) // fixed-size mismatch, drop frame
                    {
                        itHeader++;
                        continue;
                    }
                }
                else
                {
                    itFooter = itHeader + (*m_rxFrameSize);
                }

                if (itFooter >= m_rxBuffer.end())
                    break;

                if (m_validator(RxMessage(), std::span<const uint8_t>(itHeader, itFooter)))
                {
                    RxMessage& msg = messages.emplace_back();
                    msg.Unpack(RxMessage::Frame(itHeader, itFooter));
                }

                itHeader++;

            } while (itHeader != m_rxBuffer.end());

            // move the header to the begining of the rx buffer
            if (itHeader == m_rxBuffer.end())
            {
                // TODO do not remove the header bytes 
                // if only a part of the header is arrived. 
                // (e.g., hadder pattern is ``AA BB CC DD``, and only ``AA BB`` part is arrived and ``CC DD`` will arrive in next read)
                m_rxBuffer.clear();
            }
            else if (itHeader != m_rxBuffer.begin())
            {
                auto it = std::move(itHeader, m_rxBuffer.end(), m_rxBuffer.begin());
                (void)m_rxBuffer.erase(it, m_rxBuffer.end());
            }

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