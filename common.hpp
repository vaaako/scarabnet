#pragma once

#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include <iostream>
#include <iomanip>
#include <thread>
#include <atomic>
#include <stdexcept>

#define ENET_IMPLEMENTATION
#include "enet/enet.h"

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;

/*
-- NOTE --
This whole file is based on javidx implementation of asio
Repository: https://github.com/OneLoneCoder/Javidx9/tree/master/PixelGameEngine/BiggerProjects/Networking
Youtube video: https://www.youtube.com/watch?v=2hNdkYInj4g
*/


// Types of events that can be polled from the server/client
enum class EventType : uint8 {
	// This enum is only used to mark an uninitialized event
	None  = 0,
	Connect,
	Disconnect,
	Receive
};


struct Packet {
	struct Header {
		uint32 id   = 0;
		uint32 type = 0;
	};

	Packet::Header header;
	std::vector<uint8> data;

	// The whole size of the packet
	inline size_t size() const noexcept {
		return sizeof(Packet::Header) + this->data.size();
	}

	// Put data into the packet
	inline void putdata(const void* data, const size_t size) noexcept {
		// Cast the generic void pointer to a pointer of the type stored in the vector
		const uint8* start = static_cast<const uint8*>(data);
		// Copy memory to vector
		// From data's start to data's end
		this->data.assign(start, start + size);
	}

	// Unpack data into a std::string
	inline std::string unpack_string() const noexcept {
		if(this->data.empty()) {
			return "";
		}
		return std::string(this->data.begin(), this->data.end());
	}

	// Unpack data into a type.
	// T must be a trivially copyable type (simple structs, int, float etc)
	template <typename T>
	inline std::optional<T> unpack_data() const {
		static_assert(std::is_trivially_copyable_v<T>,
			"get_data can only be used with trivially copyable types (simple structs, int, float, etc.)");

		// Check if is the same type that is trying to convert to
		if(data.empty() || data.size() != sizeof(T)) {
			return std::nullopt;
		}

		// Copy bytes to T object
		T result_object;
		std::memcpy(&result_object, data.data(), sizeof(T));
		return result_object;
	}

	friend std::ostream& operator<<(std::ostream& os, const Packet& packet) {
		os << "ID: " << packet.header.id << " Type: " << packet.header.type << " Size: " << packet.size();
		return os;
	}
	// Friend because is accessed by anywhere
};


// Events sent/received by server and client
struct Event {
	// Peer owner of the event
	uint32 peer_id = 0;
	// Type of the event
	EventType type = EventType::None;

	std::unique_ptr<Packet> packet = nullptr;
};


// Flags to tell how sending packets should be handled.
// Flags can be combined
// for example: RELIABLE | UNRELIABLE_FRAGMENT
// (reliable, but fraagmented, for large critical data)
enum PacketFlag : uint8 {
	// Guarantees delivery (like TCP), packets will resent until is received.
	// Use case: Critical data (e.g. game state, chat messages).
	// Higher latency
	RELIABLE            = (1 << 0),
	// Packets may arrive out of order.
	// Use case: Real-time dataa where old packets can be ignored (e.g. voice chat, real time position).
	// Faster but may drop packets
	UNSEQUENCED         = (1 << 1),
	// Splits large packets into fragments
	// Use case: Large, non-critical data (e.g. file transfers).
	// Avoid using for small packets (overhead)
	UNRELIABLE_FRAGMENT = (1 << 3)
};


#define CURRENT_TIME_STREAM \
	([]() -> std::string { \
		auto now = std::chrono::system_clock::now(); \
		std::time_t time = std::chrono::system_clock::to_time_t(now); \
		std::tm tm = *std::localtime(&time); \
		std::ostringstream oss; \
		oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S"); \
		return oss.str(); \
	})()

#define LOG_SERVER(message) \
	if(this->show_log) \
		std::cout << "[" << CURRENT_TIME_STREAM << "] " << message << std::endl;



namespace PacketHelper {
	inline std::vector<uint8> serialize_packet(const Packet& packet) noexcept{
		std::vector<uint8> buffer;
		buffer.resize(packet.size());

		// Copy the header into the beginning of the buffer
		std::memcpy(buffer.data(), &packet.header, sizeof(Packet::Header));
		// Copy the packet data into the buffer, after the header
		std::memcpy(buffer.data() + sizeof(Packet::Header), packet.data.data(), packet.data.size());

		return buffer;
	}

	inline std::unique_ptr<Packet> deserialize_packet(const uint8* data, const size_t size) noexcept {
		// Should containg at least a Packet::Header
		if(size < sizeof(Packet::Header)) {
			return nullptr;
		}

		std::unique_ptr<Packet> packet = std::make_unique<Packet>();

		// Unpack header from the beginning
		std::memcpy(&packet->header, data, sizeof(Packet::Header));

		// Unpack the rest of the payload, which could be 0 bytes
		const uint8* payload_start = data + sizeof(Packet::Header);
		const size_t paylaod_size  = size - sizeof(Packet::Header);
		packet->putdata(payload_start, paylaod_size);

		return packet;
	}
};


template <typename T>
class TSQueue {
	public:
		TSQueue() = default;
		TSQueue(const TSQueue<T>&) = delete;
		virtual ~TSQueue() {
			this->clear();
		}

		// Returns item at front of queue
		inline const T& front() const noexcept {
			std::scoped_lock lock = std::scoped_lock(this->mux);
			return this->queue.front();
		}

		// Returns item at back of queue
		inline const T& back() const noexcept {
			std::scoped_lock lock = std::scoped_lock(this->mux);
			return this->queue.back();
		}

		// Adds an item to back of queue
		inline void push_back(const T& item) noexcept {
			std::scoped_lock lock = std::scoped_lock(this->mux);
			this->queue.push_back(std::move(item));
		}

		// Adds an item to back of queue
		inline void push_back(T&& item) noexcept {
			std::scoped_lock lock = std::scoped_lock(this->mux);
			this->queue.push_back(std::move(item));
		}

		// Adds an item to back of queue
		inline void push_front(const T& item) noexcept {
			std::scoped_lock lock = std::scoped_lock(this->mux);
			this->queue.push_front(std::move(item));
		}

		// Adds an item to back of queue
		inline void push_front(T&& item) noexcept {
			std::scoped_lock lock = std::scoped_lock(this->mux);
			this->queue.push_front(std::move(item));
		}

		// Returns true if queue has no items
		inline bool empty() const noexcept {
			std::scoped_lock lock = std::scoped_lock(this->mux);
			return this->queue.empty();
		}

		// Returns number of items in queue
		inline size_t count() const noexcept {
			std::scoped_lock lock = std::scoped_lock(this->mux);
			return this->queue.count();
		}

		// Clear queue
		inline void clear() noexcept {
			std::scoped_lock lock = std::scoped_lock(this->mux);
			return this->queue.clear();
		}

		// Removes and returns item from front of queue
		inline T pop_front() noexcept {
			std::scoped_lock lock = std::scoped_lock(this->mux);
			T t = std::move(this->queue.front());
			this->queue.pop_front();
			return t;
		}

		// Removes and returns item from back of queue
		inline T pop_back() noexcept {
			std::scoped_lock lock = std::scoped_lock(this->mux);
			T t = std::move(this->queue.back());
			this->queue.pop_front();
			return t;
		}

	private:
		mutable std::mutex mux;
		std::deque<T> queue;
};
