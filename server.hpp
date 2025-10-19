#pragma once

#include "common.hpp"
#include <unordered_map>

using namespace scarabnet;

class Server {
	public:
		Server(const uint16 port, uint16 max_clients, bool show_log = false);
		~Server() noexcept;

		// Returns true if the sever has started
		inline bool isrunning() const noexcept {
			return this->running;
		}

		// Starts the network thread
		void start() noexcept;

		// Stops the network thread
		void stop() noexcept;

		// Returns true if an event was processed
		bool poll_event(Event& event) noexcept;

		// Send a packet to a specific client
		void send(const uint32 client_id, const Packet& packet, const PacketFlag flag = PacketFlag::RELIABLE) const;

		// Broadcast a packet to all clients
		void broadcast(const Packet& packet, const PacketFlag flag = PacketFlag::RELIABLE) const;
	private:
		// Listen to events and push to vector of events
		void network_thread_loop() noexcept; // Loop of thread

		ENetHost* host = nullptr;
		
		bool show_log; // Debug
		TSQueue<Event> events;

		// Thread
		std::thread thread;
		std::atomic<bool> running = false;
		// atomic avoids data races

		// Connected clients
		std::unordered_map<uint32, ENetPeer*> clients;
		// Current Peer id
		uint32 curid = 1;

		// Used to send packet to a specific client
		// So its possible to access this->clients safely
		// This is needed since this->clients is also called inside Server::send
		mutable std::mutex clients_mutex;
};


inline Server::Server(const uint16 port, uint16 max_clients, bool show_log)
	: show_log(show_log) {

	if(enet_initialize() != 0) {
		throw std::runtime_error("Failed to initialize ENet");
	}

	ENetAddress address = { 0 };
	address.host = ENET_HOST_ANY;
	address.port = port;
	this->host = enet_host_create(
		&address,
		max_clients, // Number of clients
		2, // Allow up to 2 channels to be used (0 and 1)
		0, // Assume any amount of incoming bandwidth
		0  // Assume any amount of outgoing bandwidth
	);

	if(this->host == NULL) {
		throw std::runtime_error("Failed to create ENet server host");
	}

	LOG_SERVER("Started server on port " << port);
}

inline Server::~Server() noexcept {
	if(this->running) {
		this->stop();
	}

	enet_host_destroy(this->host);
	enet_deinitialize();
}

inline void Server::start() noexcept {
	if(this->running) {
		return;
	}
	this->running = true;
	this->thread = std::thread(&Server::network_thread_loop, this);
}

inline void Server::stop() noexcept {
	this->running = false;
	if(this->thread.joinable()) {
		this->thread.join();
	}
}

inline bool Server::poll_event(Event& event) noexcept {
	if(this->events.empty()) {
		return false;
	}
	event = this->events.pop_front();
	return true;
}

inline void Server::send(const uint32 client_id, const Packet& packet, const PacketFlag flag) const {
	if(this->running) {
		return;
	}

	const std::vector<uint8> serialized = PacketHelper::serialize_packet(packet);
	ENetPacket* epacket = enet_packet_create(
		serialized.data(),
		serialized.size(),
		(ENetPacketFlag)flag
	);

	// Allocation failed
	if(epacket == NULL) {
		LOG_SERVER("Allocation failed while creating packet");
		return;
	}

	// Check if client is valid
	std::scoped_lock lock = std::scoped_lock(this->clients_mutex); // Lock before accessing the map
	auto it = this->clients.find(client_id);
	if(it == this->clients.end()) {
		LOG_SERVER("Client " << client_id << " not found");
		return;
	}

	// Send packet
	if(enet_peer_send((ENetPeer*)it->second, 0, epacket) < 0) {
		enet_packet_destroy(epacket); // Clean up on failure
		LOG_SERVER("Failed to send packet");
		return;
	}

	LOG_SERVER("Packet sent!");

	// immediately send out all queued packets
	// enet_host_flush((ENetHost*)this->host);
}

inline void Server::broadcast(const Packet& packet, const PacketFlag flag) const {
	if(this->running) {
		return;
	}

	const std::vector<uint8> buffer = PacketHelper::serialize_packet(packet);
	ENetPacket* epacket = enet_packet_create(
		buffer.data(),
		buffer.size(),
		(ENetPacketFlag)flag
	);

	// Allocation failed
	if(epacket == NULL) {
		LOG_SERVER("Allocation failed while creating packet");
		return;
	}

	// Broadcast packet
	enet_host_broadcast((ENetHost*)this->host, 0, epacket);

	LOG_SERVER("Broadcasted packet!");
}


inline void Server::network_thread_loop() noexcept {
	while(this->running) {
		ENetEvent event;

		// Wait 5ms for a new event
		while(enet_host_service(this->host, &event, 5) > 0) {
			switch (event.type) {
				case ENET_EVENT_TYPE_CONNECT: {
					// Lock before accessing this->clients
					std::scoped_lock lock = std::scoped_lock(this->clients_mutex);

					// New ID
					const uint32 newid = this->curid++;
					this->clients[newid] = event.peer;

					// Store the id on the peer itself for quick lookups
					event.peer->data = (void*)((uintptr_t)newid);
					// Push packet
					this->events.push_back({ .peer_id = newid, .type = EventType::Connect });

					LOG_SERVER("Client " << newid << " connected");
					break;
				}

				case ENET_EVENT_TYPE_RECEIVE: {
					const uint32 peerid = (uintptr_t)event.peer->data;
					LOG_SERVER("Packet received from peer " << peerid);

					// Create an event with data inside
					this->events.push_back({
						.peer_id = peerid,
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
					// Lock before accessing this->clients
					std::scoped_lock lock = std::scoped_lock(this->clients_mutex);

					const uint32 peerid = (uintptr_t)event.peer->data;
					// Remove from connected clients
					this->clients.erase(peerid);
					// Push event
					this->events.push_back({ .peer_id = peerid, .type = EventType::Disconnect });

					LOG_SERVER("Client " << peerid << " disconnected");
					break;
				}

				default:
					break;
			}
		}
	}
}

