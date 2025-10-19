# Scarabnet 🪲
A **header-only C++ library** for easy and minimal **UDP communication** built on top of a for of [ENet](github.com/zpl-c/enet)

Features:
- Listening for incoming UDP packets
- Broadcasting packets
- Sending packets to specific clients

## Installation
To use it in your project, simply copy the following files:
- `common.hpp`
- `server.hpp`
- `client.hpp`
- `enet/` from the [ENet fork](github.com/zpl-c/enet)

You do **not** need to place the `enet` directory in the same folder. Just make sure your compiler can find it (e.g., using `-I` include path)

# Usage example
🖥️ **Server**
```cpp
int main() {
	Server server = Server(8095, 5, true); // Port, Max Clients, Enable Logging
	server.start(); // Start server thread

	// Application main loop
	while(server.isrunning()) {
		// Listen all events in this frame
		Event event;
		while(server.poll_event(event)) {
			switch (event.type) {
				case EventType::None: break;
				case EventType::Connect: break;
				case EventType::Disconnect: break;

				case EventType::Receive: {
					// We know that is packet with a string in this example
					// Try to extract string from received packet
					std::optional<std::string> msg = event.packet->unpack_string();
					if(!msg.has_value()) {
						std::cout << "Failed to unpack packet" << std::endl;
						break;
					}

					std::cout << "-> Received packet: " << *event.packet << std::endl;
					std::cout << "Packet message: " << msg.value() << std::endl;
					break;
				}
			}
		}
	}

	std::cout << "Server stopped!" << std::endl;
}
```

💻 **Client**
```cpp
int main() {
	Client client = Client(true);      // Enable logging
	client.connect("127.0.0.1", 8095); // Server Address, Port

	auto last_ping = std::chrono::steady_clock::now();

	// Application main loop
	while(client.isrunning()) {
		// Listen all events in this frame
		Event event;
		while(client.poll_event(event)) {
			switch (event.type) {
				case EventType::None:
				case EventType::Connect:
				case EventType::Disconnect:
				case EventType::Receive:
					break;
			}
		}

		// Send a message every 10 seconds
		if(client.isconnected()) {
			auto now = std::chrono::steady_clock::now();
			if(now - last_ping > std::chrono::seconds(10)) {
				last_ping = now;

				std::string msg = "Ping from client!";
				// Create a packet and send to server
				Packet packet_to_send;
				// Example of a header
				packet_to_send.header = {
					.id   = 1,
					.type = 123
				};
				// Filling data
				packet_to_send.putdata(msg.c_str(), msg.length());
				client.send(packet_to_send);
			}
		}
	}

	std::cout << "Client stopped!" << std::endl;
}
```
