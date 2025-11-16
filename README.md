# ProtoComm

[![Test](https://github.com/ozguronsoy/ProtoComm/actions/workflows/test.yaml/badge.svg)](https://github.com/ozguronsoy/ProtoComm/actions/workflows/test.yaml)

A header-only modern C++ library designed for message-based communication with hardware or services. Intended for use in applications running on Embedded Linux devices as well as high-performance platforms like PCs, Macs, and smartphones.

``ProtoComm`` can be used in a variety of scenarios, such as a ground station communicating with a rocket, UAV, or drone for real-time data exchange and control.

- [Features](#features)
- [Dependencies](#dependencies)
- [Usage](#usage)
- [Docs](https://ozguronsoy.github.io/ProtoComm/)
- [Examples](docs/examples)
- [Roadmap](#roadmap)
- [Contributing](CONTRIBUTING.md)

## Features

- **Multi-Message Architecture:** Parse multiple, different message types from a single, interleaved data stream. The parser is order-preserving, ensuring messages are unpacked in the exact sequence they are received.

- **Multi-Channel I/O:** Natively supports 1-to-N (server/hub) and 1-to-1 (client) protocols. Can manage multiple, independent channels (like 5 serial ports, or 50 connected TCP clients) from a single object.

- **Hybrid API:** Provides a complete set of both synchronous and asynchronous methods. The async API supports both ``std::future`` and callback-based patterns, offering maximum flexibility for both simple, await-style requests and high-performance, continuous, event-driven data streams.

- **Extensible Interface-Based Design:** The library is built on simple interfaces which allows you to:

    - Implement custom communication protocols to add any new transport layer (e.g., CAN bus, SPI, I2C).

    - Define custom message payloads.

    - Define custom, per-message validation and sealing (like checksums or CRCs).

- **Modern & High Portability:** The core library is header-only and has no external dependencies, relying exclusively on the C++20 Standard Library for maximum portability.

- **Optional Implementations:** Get started immediately with optional protocol implementations for ``asio`` and ``Qt``.


## Dependencies

### Core Library (Header-Only)
The core ``ProtoComm`` library is header-only and has no external dependencies other than a C++20 compliant compiler.

### Optional Implementations
The protocol implementations are optional and require the respective libraries.

- asio (standalone or boost)
- Qt6 Core and SerialPort modules



## Usage

A collection of examples demonstrating how to implement your own messages, protocols, and frame handlers can be found in the [docs/examples](docs/examples) directory.




## Roadmap

``CommStream`` is optimized for platforms with a full operating system where resources like memory and threads are readily available. A primary goal is to introduce a parallel implementation, ``CommStreamES``, designed for resource-constrained MCUs.

This embedded-friendly version will be built for bare-metal and RTOS environments by adhering to strict embedded C++ guidelines:

- **No Dynamic Memory:** All buffers and objects will be statically-allocated.

- **No Exceptions:** All error handling will be done through return values with ``std::error_code``, to be compatible with compilers using.

- **No RTTI:** Multi-message parsing will be handled at compile-time using ``std::variant`` instead of prototypes and cloning.

- **Synchronous-Only API:** The initial version will be single-threaded and synchronous-only.

## Contributing

Contributions are welcome and greatly appreciated! Please see the [Contributing Guidelines](CONTRIBUTING.md) for details on how to submit pull requests, report issues, and follow the project's code style.