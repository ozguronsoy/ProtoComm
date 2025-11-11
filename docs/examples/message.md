# Messages

All communication is built around ``IMessage`` objects. These are your custom classes that define the specific data payloads you want to send and receive.

To create a new message, you must inherit from one or both of the core interfaces:

- **IRxMessage:** Implement this to create a message that can be received (unpacked).

- **ITxMessage:** Implement this to create a message that can be sent (packed).

This guide explains the methods you must implement and shows examples for all three common use cases.

## Example 1: Receive-Only (IRxMessage)

Implement ``IRxMessage`` when you only need to receive and parse the  message. 

```c++
class MyRxMessage : public IRxMessage
{
public:
    std::optional<size_t> FrameSize() const override
    {
        // payload + header + footer + checksum
        return 9 + HeaderPattern().size() + FooterPattern().size() + 1;
    }

    std::span<const uint8_t> HeaderPattern() const override
    {
        static constexpr const std::array<uint8_t, 2> header = { 0xF7, 0xA5 };
        return header;
    }

    std::span<const uint8_t> FooterPattern() const override
    {
        static constexpr const std::array<uint8_t, 2> footer = { 0x2C, 0x73 };
        return footer;
    }

    IFrameHandler& FrameHandler() const override
    {
        return ChecksumFrameHandler<>::Instance();
    }

    std::unique_ptr<IMessage> Clone() const override
    {
        return std::make_unique<MyRxMessage>(*this);
    }

    void Unpack(std::span<const uint8_t> frame) override
    {
        auto it = frame.begin() + this->HeaderPattern().size();
        // your unpacking logic
    }

    float val1 = 0;
    int val2 = 0;
    char val3 = 0;
};
```

## Example 2: Transmit-Only (ITxMessage)

Implement ``ITxMessage`` when you only need to create and send a specific message. Your Pack implementation should only write the payload data (e.g., ``val1``, ``val2``). The ``IFrameHandler::Seal`` method is called after ``Pack`` and is responsible for filling in all remaining frame bytes, such as headers, footers, checksum, etc.

```c++
class MyTxMessage : public ITxMessage
{
public:
    std::optional<size_t> FrameSize() const override
    {
        // payload + header + footer + checksum
        return 9 + HeaderPattern().size() + FooterPattern().size() + 1;
    }

    std::span<const uint8_t> HeaderPattern() const override
    {
        static constexpr const std::array<uint8_t, 2> header = { 0xA2, 0xC7 };
        return header;
    }

    std::span<const uint8_t> FooterPattern() const override
    {
        static constexpr const std::array<uint8_t, 2> footer = { 0x2C, 0x73 };
        return footer;
    }

    IFrameHandler& FrameHandler() const override
    {
        return ChecksumFrameHandler<>::Instance();
    }

    std::unique_ptr<IMessage> Clone() const override
    {
        return std::make_unique<MyTxMessage>(*this);
    }

    void Pack(std::vector<uint8_t>& frame) const override
    {
        auto it = frame.begin() + this->HeaderPattern().size();
        // your packing logic
    }

    float val1 = 0;
    int val2 = 0;
    char val3 = 0;
};
```

## Example 3: Bidirectional

A message can be bidirectional.

```c++
class MyMessage : public IRxMessage, public ITxMessage
{
public:
    std::optional<size_t> FrameSize() const override
    {
        // payload + header + footer + checksum
        return 9 + HeaderPattern().size() + FooterPattern().size() + 1;
    }

    std::span<const uint8_t> HeaderPattern() const override
    {
        static constexpr const std::array<uint8_t, 2> header = { 0xFF, 0x37 };
        return header;
    }

    std::span<const uint8_t> FooterPattern() const override
    {
        static constexpr const std::array<uint8_t, 2> footer = { 0x2C, 0x73 };
        return footer;
    }

    IFrameHandler& FrameHandler() const override
    {
        return ChecksumFrameHandler<>::Instance();
    }

    std::unique_ptr<IMessage> Clone() const override
    {
        return std::make_unique<MyMessage>(*this);
    }

    void Unpack(std::span<const uint8_t> frame) override
    {
        auto it = frame.begin() + this->HeaderPattern().size();
        // your unpacking logic
    }

    void Pack(std::vector<uint8_t>& frame) const override
    {
        auto it = frame.begin() + this->HeaderPattern().size();
        // your packing logic
    }

    float val1 = 0;
    int val2 = 0;
    char val3 = 0;
};
```