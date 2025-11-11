# Frame Handlers

The ``IFrameHandler`` is an object that defines the logic for validating (Rx) and sealing (Tx) the message frames.

## Example: Creating a Custom

```c++
class Crc16FrameHandler : public FrameHandler
{
public:
    uint16_t Calculate(std::span<const uint8_t> frame)
    {
        // calculate and return the crc
    }

    virtual bool Validate(const IRxMessage& msg, std::span<const uint8_t> frame) override
    {
        // validate header, footer, and size
        if (!FrameHandler::Validate(msg, frame))
            return false;

        // get the expected crc
        uint16_t expected;
        auto crcPosition = frame.end() - msg.FooterPattern().size() - sizeof(uint16_t);
        std::memcpy(&expected, &*crcPosition, sizeof(uint16_t));

        uint16_t calculated = this->Calculate(frame);

        return calculated == expected;
    }

    virtual void Seal(const ITxMessage& msg, std::span<uint8_t> frame) override
    {
        // write header and footer
        FrameHandler::Seal(msg, frame);
        
        uint16_t calculatedCrc = this->Calculate(frame);
        
        // write CRC into the frame
        auto crcPosition = frame.end() - msg.FooterPattern().size() - sizeof(uint16_t);
        std::memcpy(&*crcPosition, &calculatedCrc, sizeof(uint16_t));
    }

    // if the handler does not require an internal state,
    // you can just create a singleton and use it for all messages.
    static Crc16FrameHandler& Instance()
    {
        static Crc16FrameHandler instance;
        return instance;
    }
}
```