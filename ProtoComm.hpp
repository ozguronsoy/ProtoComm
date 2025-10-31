#ifndef PROTOCOMM_HPP
#define PROTOCOMM_HPP

#include <cstdint>
#include <array>
#include <vector>
#include <span>
#include <optional>
#include <numeric>
#include <type_traits>
#include <concepts>
#include <iostream>

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
     * @brief A basic frame validator that checks for header and footer patterns.
     *
     * This class provides a concrete implementation of IFrameValidator that
     * only validates the presence of correct header and footer byte patterns.
     */
    class FrameValidator : public IFrameValidator
    {
    public:
        virtual ~FrameValidator() = default;

        virtual bool operator()(const IMessage& msg, const IMessage::Frame& frame) override
        {
            // frame size checks should be done before calling this method
            auto headerPattern = msg.HeaderPattern();
            if (headerPattern.size() > 0 && !std::equal(headerPattern.begin(), headerPattern.end(), frame.begin()))
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
     * @tparam TResult The data type of the checksum (e.g., uint8_t, uint16_t).
     * @tparam TData The data type of the elements being summed (default: uint8_t).
     * @tparam init The initial value for the checksum calculation (default: 0).
     * @tparam BinaryOp The binary operation for checksum calculation (default: std::plus).
     */
    template<typename TResult, typename TData = uint8_t, TData init = 0, typename BinaryOp = std::plus<TData>>
        requires std::is_arithmetic_v<TResult>&& std::is_arithmetic_v<TData>
    class ChecksumFrameValidator final : public FrameValidator
    {
        bool operator()(const IMessage& msg, const IMessage::Frame& frame) override
        {
            if (!FrameValidator::operator()(msg, frame))
                return false;

            auto payloadBegin = frame.begin() + msg.HeaderPattern().size();
            auto payloadEnd = frame.end() - msg.FooterPattern().size() - sizeof(TResult);

            TResult expected;
            std::memcpy(&expected, &(*payloadEnd), sizeof(TResult));

            const TResult calculated = std::accumulate(payloadBegin, payloadEnd, init, BinaryOp());

            return calculated == expected;
        }
    };

#pragma endregion Frame Validators

#pragma region Comm

    template<typename T>
    concept IsCommProtocol = requires(T t, std::span<uint8_t> buf, std::span<const uint8_t> cbuf)
    {
        { t.IsConnected() } -> std::same_as<bool>;
        { t.Disconnect() };
        { t.Read(buf) }     -> std::same_as<size_t>;
        { t.Write(cbuf) };
    };

    // Specifies the type ``T`` has a ``Connect`` method that takes ``Args`` parameters.
    template<typename T, typename... Args>
    concept Connectable = requires(T t, Args... args)
    {
        { t.Connect(std::forward<Args>(args)...) } -> std::same_as<bool>;
    };

    // alternative naming to CommHandler
    template<typename Protocol, typename RxMessage, typename TxMessage, typename Validator = FrameValidator>
        requires IsCommProtocol<Protocol>&&
    std::derived_from<RxMessage, IRxMessage>&&
        std::derived_from<TxMessage, ITxMessage>&&
        std::derived_from<Validator, FrameValidator>

        class CommStream final
    {
    private:
        Protocol protocol;
        Validator frameValidator;

    public:
        CommStream& operator>>(RxMessage& msg)
        {
            auto rxMsg = this->Read();
            if (rxMsg.has_value())
                msg = std::move(*rxMsg);
            return *this;
        }

        CommStream& operator>>(std::vector<RxMessage>& messages)
        {
            messages = this->Read();
            return *this;
        }

        CommStream& operator<<(const TxMessage& msg)
        {
            this->Write(msg);
            return *this;
        }

        CommStream& operator<<(std::span<const TxMessage> messages)
        {
            this->Write(messages);
            return *this;
        }

        template<typename... Args>
            requires Connectable<Protocol, Args...>
        bool Connect(Args... args)
        {
            if (protocol.IsConnected()) return false;
            return protocol.Connect(std::forward<Args>(args)...);
        }

        void Disconnect()
        {
            if (protocol.IsConnected())
                protocol.Disconnect();
        }

        std::optional<RxMessage> Read()
        {
            auto rxMessages = this->Read(1);
            return (rxMessages.size() > 0) ? (rxMessages[0]) : (std::nullopt);
        }

        std::vector<RxMessage> Read(size_t n)
        {
            if (n == 0)
                return {};

            std::vector<RxMessage> rxMessages;

            // TODO

            return rxMessages;
        }

        void Write(const TxMessage& msg)
        {
            this->Write(std::span<const TxMessage>(&msg, 1));
        }

        void Write(std::span<const TxMessage> messages)
        {
            // TODO
        }
    };

#pragma endregion Comm

}

#endif