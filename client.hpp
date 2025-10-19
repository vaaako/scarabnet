#pragma once

#include "common.hpp"
#include "enet/enet.h"
#include <memory>

class Client {
	public:
		Client(const bool show_log = false);
		~Client() noexcept;

		// Returns true if client is running
		inline bool isrunning() const noexcept {
			return this->running;
		}

		// Returns true if client is connected to a server
		inline bool isconnected() const noexcept {
			return this->connected && this->peer != nullptr;
		}

		// Initiates a non-blocking connection attempt to a server.
		// The result (success/failure) will arrive as an event in poll_packet
		void connect(const std::string& ipaddress, const uint16 port);

		// Disconnects from the server
		void disconnect() noexcept;

		// Sends a packet to the server
		void send(const Packet& packet, const PacketFlag flag = PacketFlag::RELIABLE) const noexcept;

		// Returns true if an event was processed, false otherwhise
		bool poll_event(Event& packet) noexcept;

	private:
		// Listen to events and push to vector of events
		void network_thread_loop(); // Loop of thread
		// Stop network thread
		void stop_network() noexcept;

		ENetHost* host = nullptr;
		ENetPeer* peer = nullptr;

		bool show_log; // Debug
		TSQueue<Event> events;

		// Thread
		std::thread thread;
		std::atomic<bool> running   = false;
		std::atomic<bool> connected = false;
		// atomic avoids data races
};


inline Client::Client(const bool show_log) : show_log(show_log) {
	if(enet_initialize() != 0) {
		throw std::runtime_error("Failed to initialize ENet");
	}

	this->host = enet_host_create(
		NULL, // Client host
		1, // Allow 1 outgoing connection
		2, // Allow up to 2 channels to be used (0 and 1)
		0, // Assume any amount of incoming bandwidth
		0  // Assume any amount of outgoing bandwidth
	);

	if(this->host == NULL) {
		throw std::runtime_error("Failed to create ENet client host");
	}
}

inline Client::~Client() noexcept {
	// Runs disconnect too
	if(this->connected) {
		this->disconnect();
	}
	this->stop_network(); // Stop thread

	// Reset peer
	if(this->peer != nullptr) {
		enet_peer_reset(peer);
		this->peer = nullptr;
	}

	// Destroy host
	if(this->host) {
		enet_host_destroy(this->host);
	}

	enet_deinitialize();

	// Peer was stopped inside Disconnect event
}

inline void Client::connect(const std::string& ipaddress, const uint16 port) {
	if(this->connected || this->running) {
		LOG_SERVER("Already connected or connection in pogress");
		return;
	}

	ENetAddress address = { 0 };
	address.port = port;
	enet_address_set_host(&address, ipaddress.c_str());
	// Allocating the two channels 0 and 1
	this->peer = enet_host_connect(this->host, &address, 2, 0);

	if(this->peer == NULL) {
		throw std::runtime_error("Failed to create ENet peer for connection");
	}

	LOG_SERVER("Connection attempt started to " << ipaddress << ":" << port);

	// Start the network thread to handle the connection result and future events
	this->running = true;
	this->thread  = std::thread(&Client::network_thread_loop, this);
}

inline void Client::disconnect() noexcept {
	if(!this->isconnected()) {
		return;
	}
	enet_peer_disconnect(this->peer, 0); // Trigger disconnect event
	LOG_SERVER("Disconnecting from server");

	// Thread disconnect will be processed in the network thread
}

inline void Client::send(const Packet& packet, const PacketFlag flag) const noexcept {
	if(!this->connected) {
		LOG_SERVER("Not connected to send packet");
		return;
	}

	const std::vector<uint8> buffer = PacketHelper::serialize_packet(packet);
	ENetPacket* epacket = enet_packet_create(
		buffer.data(),
		buffer.size(),
		(ENetPacketFlag)flag
	);
	enet_peer_send(this->peer, 0, epacket);

	LOG_SERVER("Sending packet of size " << packet.data.size() << "...");
}

inline bool Client::poll_event(Event& packet) noexcept {
	if(this->events.empty()) {
		return false;
	}
	packet = this->events.pop_front();
	return true;
}




inline void Client::stop_network() noexcept {
	// This will only be triggered if disconnected
	// if(this->isconnected()) {
	// 	this->disconnect();
	// }

	this->running = false;
	if(this->thread.joinable()) {
		this->thread.join();
	}
}

inline void Client::network_thread_loop() {
	while(this->running) {
		ENetEvent event;
		// Wait 5ms for event
		while(enet_host_service(this->host, &event, 5) > 0) {
			// Peer ID of 0 represent the server connection
			const uint32 serverid = 0;

			switch (event.type) {
				case ENET_EVENT_TYPE_CONNECT: {
					this->connected = true;
					this->events.push_back({ .peer_id = serverid, .type = EventType::Connect });
					LOG_SERVER("Connection successful!");
					break;
				}
					
				case ENET_EVENT_TYPE_RECEIVE: {
					LOG_SERVER("Packet received from server");

					// Create an event with data inside
					this->events.push_back({
						.peer_id = serverid,
						.type    = EventType::Receive,
						.packet  = PacketHelper::deserialize_packet(event.packet->data, event.packet->dataLength)
					});

					// This data was copied to the packet
					// which was moved to the queue
					enet_packet_destroy(event.packet);
					break;
				}
				case ENET_EVENT_TYPE_DISCONNECT:
				case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT: {
					// Since is disconnected, the network thread's job can stop
					this->running = false;
					this->connected = false;
					// Push event
					this->events.push_back({ .peer_id = serverid, .type = EventType::Disconnect });

					LOG_SERVER("Disconnected from server");
					break;
				}

				default:
					break;
			}
		}
	}
}
