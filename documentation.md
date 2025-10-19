# Server Class
**Constructor**
- `port`: Port the server listens on
- `max_clients`: Maximum number of client connections
- `show_log`: If `true`, logs internal events and traffic
```cpp
Server(const uint16 port, const uint16 max_clients, bool show_log = false)
```

**Methods**:
Returns `true` if the server's internal thread is currently running
```cpp
bool isrunning();
```

Starts the server thread and begins accepting connections
```cpp
void start();
```

Stops the server's thread and disconnects all clients
```cpp
void stop();
```

Polls for an incoming event
- **Returns**: `true` if an event was successfully polled
- `event`: The event object that will be populated if an event is available
```cpp
bool poll_event(Event& event);
```

Sends a packet to a specific connected client
- `peer_id`: ID of the client to send the packet to
- `packet`*: The packet to send
- `flag`: Transmission method
```cpp
void send(const uint32 peer_id, const Packet& packet, const PacketFlag flag = PacketFlag::RELIABLE);
```

Sends a packet to all connected clients
- `packet`: The packet to send
- `flag`: Transmission method
```cpp
void broadcast(const Packet& packet, const PacketFlag flag = PacketFlag::RELIABLE);
```

# Client Class
**Constructor**
- **`show_log`**: If `true`, enables logging of internal events
```cpp
Client(bool show_log = false)
```

Returns `true` if the client is currently connected to a server
```cpp
bool isrunning();
```

Connects the client to a server
- `host`: Server IP address or hostname
- `port`: Port to connect to
```cpp
void connect(const std::string& host, const uint16 port);
```

Disconnects from the server and stops the client thread
```cpp
void disconnect();
```

Polls for incoming events from the server
- **Returns**: `true` if an event was polled
- `event`: The event object to populate
```cpp
bool poll_event(Event& event);
```

Sends a packet to the server
- `packet`: The packet to send
- `flag`: Transmission method
```cpp
void send(const Packet& packet, const PacketFlag flag = PacketFlag::RELIABLE);
```

---

# Globals
## Macros
### `CURRENT_TIME_STREAM`
Returns a `std::string` with the current date and time in the format: `Y-m-d H:M:S`

### `LOG_SERVER(x)`
Converts to:
```cpp
if(this->show_log) {
	std::cout << "[" << CURRENT_TIME_STREAM << "] " << message << std::endl;
}
```

## Namespace
### `PacketHelper`
Converts a `Packet` into a byte vector. Used internally
```cpp
std::vector<uint8> serialize_packet(Packet& packet)
```

Converts raw bytes back into a `Packet`. Used internally
```cpp
std::unique_ptr<Packet> deserialize_packet(const uint8* data, size_t size)
```

---

# Structs
## `Header`
Used inside `Packet` for data identification
```cpp
struct Header {
	uint32 id;
	uint32 type;
};
```
## `Packet`
Used to send data between server and clients

**Members**:
- `Header header`
- `std::vector<uint8> data`

**Methods**:
Returns the size of the packet
```cpp
size_t Packet::size() const
```

Appends raw data to the packet
```cpp
void Packet::putdata(const void* data, size_t size)
```

Converts internal data to a `std::string`
```cpp
std::string Packet::unpack_string()
```

Extracts data of type `T` from the packet
- `T` must be trivially copyable (e.g., simple structs, `int`, `float`, etc.)
```cpp
std::optional<T> Packet::unpack_data<T>()
```

Used for debug printing
```cpp
std::ostream& operator<<(std::ostream&, const Packet&)
```


## `Event`
Used internally to return polled events from the server and client

**Members**:
- `uint32 peer_id`
	+ Represents the owner of the event
	+ `0` represents the server
- `EventType type`
	+ Describes the event type.

---

# Enums
## `EventType`
Enum used internally to represent the type of an `Event`

## `PacketFlag`
Defines how a packet should be handled. Can be combined using bitwise operators. The default used is `PacketFlag::RELIABLE`

**Flags**:
- **`RELIABLE`**
	+ Guarantees delivery (like TCP); packets will be resent until acknowledged
	+ Use Case: Critical data (e.g., game state, chat messages)
	+ **Higher latency**
- **`UNSEQUENCED`**
	+ Packets may arrive out of order
	+ Use Case: Real-time data where stale data can be ignored (e.g., voice chat, player positions)
	+ **Lower latency**, but **may lose packets**
- **`UNRELIABLE_FRAGMENT`**
	+ Splits large packets into smaller fragments
	+ Use Case: Large, non-critical data (e.g., file transfers)
	+ Not suitable for small packets due to overhead

**Example**:
Reliable delivery of large, fragmented packets
```cpp
PacketFlag::RELIABLE | PacketFlag::UNRELIABLE_FRAGMENT
```

---

# Class: `TSQueue<T>`
A thread-safe queue used internally. Has the following methods:
- `T& front()`: Returns item at front of queue
- `T& back()`: Returns item at back of queue
- `void push_back(const T&)`: Adds an item to back of queue
- `void push_front(const T&)`: Adds an item to front of queue
- `void push_back(T&&)`: Adds an item to back of queue
- `void push_front(T&&)`: Adds an item to front of queue
- `bool empty()`: Returns true if queue has no items
- `size_t count()`: Returns number of items in queue
- `void clear()`: Clear queue
- `T pop_front()`: Removes and returns item from front of queue
- `T pop_back()`: Removes and returns item from back of queue

