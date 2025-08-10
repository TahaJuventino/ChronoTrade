#include <iostream>
#include <vector>
#include <cstring>
#include <chrono>
#include <thread>
#include <random>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "../SharedMemoryFeedSource.hpp"

using namespace feed;

int main(int argc, char* argv[]) {
    std::string shm_name = "/test_shm_live_writer";
    int rate_ms = 50;     // default: 50ms between packets
    int count = 100;      // total packets
    bool inject_malformed = false;
    bool burst_mode = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--rate" && i + 1 < argc) rate_ms = std::stoi(argv[++i]);
        else if (arg == "--count" && i + 1 < argc) count = std::stoi(argv[++i]);
        else if (arg == "--malformed") inject_malformed = true;
        else if (arg == "--burst") burst_mode = true;
    }

    int fd = shm_open("/test_shm_live_writer", O_CREAT | O_RDWR, 0666);

    if (fd < 0) {
        std::cerr << "Failed to open SHM: " << shm_name << std::endl;
        return 1;
    }

    void* ptr = mmap(nullptr, sizeof(RingBuffer), PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        std::cerr << "mmap failed\n";
        return 1;
    }

    RingBuffer* ring = static_cast<RingBuffer*>(ptr);
    uint32_t head = ring->head.load(std::memory_order_acquire);
    uint32_t tail = ring->tail.load(std::memory_order_acquire);
    std::mt19937 rng(std::random_device{}());

    for (int i = 0; i < count; ++i) {
        uint32_t next_head = (head + 1) % kMaxPackets;
        if (next_head == tail) {
            std::cerr << "[!] Ring buffer full\n";
            break;
        }

        std::string packet;
        if (inject_malformed && i % 5 == 0) {
            std::vector<std::string> malformed = {
                "", "malformed,packet", "1e308,NaN,XYZ", std::string(kPacketSize, 'X')
            };
            packet = malformed[rng() % malformed.size()];
        } else {
            packet = "100.0," + std::to_string(i + 1) + ",172500000" + std::to_string(i);
        }

        if (packet.size() >= kPacketSize) {
            packet = packet.substr(0, kPacketSize - 1); // truncate if oversized
        }

        Packet& pkt = ring->packets[head % kMaxPackets];
        std::memset(pkt.data, 0, kPacketSize);
        std::memcpy(pkt.data, packet.c_str(), packet.size());
        pkt.len = static_cast<uint16_t>(packet.size());

        head = next_head;
        ring->head.store(head, std::memory_order_release);

        std::cout << "[SHM Writer] Injected: " << packet << std::endl;
        if (!burst_mode) std::this_thread::sleep_for(std::chrono::milliseconds(rate_ms));
    }

    munmap(ptr, sizeof(RingBuffer));
    close(fd);
    return 0;
}