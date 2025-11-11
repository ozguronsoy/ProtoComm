# Protocols

The ``ICommProtocol`` is the transport layer of ``ProtoComm``. It is an abstract interface that tells the ``CommStream`` how to actually send and receive raw bytes.

While ``ProtoComm`` provides optional, pre-built implementations for asio and Qt, its real power is that you can implement your own protocol to wrap any I/O mechanism.

## ICommProtocol

The interface is designed to handle multi-channel protocols, but one can use it for single-channel protocols as well. 

The ``ICommProtocol`` interface itself does not define a ``Start`` method. This is because the connection parameters are different for every protocol (e.g., a serial port needs a baud rate, while a TCP client needs an IP address and port).

Instead, ``CommStream`` requires that your protocol implementation satisfies the ``Startable`` concept. Your protocol class must:

- Provide a public ``Start`` method that takes any parameters you need.

- This ``Start`` method must return a optional channel id object which is:

    - The unique channel id on a successful start that creates or adds a new, usable channel.

    - ``std::nullopt`` on any failure or for "listener" protocols (like a server) that don't create a new channel at startup, but just begin listening.