#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <new>
#include <cassert>
#include <cstring>
#include <type_traits>
#include <utility> // for std::forward

namespace utils {

    class ArenaAllocator {
        public:
            explicit ArenaAllocator(std::size_t size)
                : buffer(new std::uint8_t[size]), capacity(size), offset(0) {}

            // Disable copy semantics to avoid double-free and ownership issues
            ArenaAllocator(const ArenaAllocator&) = delete;
            ArenaAllocator& operator=(const ArenaAllocator&) = delete;

            // Enable move semantics for safe ownership transfer
            ArenaAllocator(ArenaAllocator&& other) noexcept
                : buffer(other.buffer), capacity(other.capacity), offset(other.offset) {
                other.buffer = nullptr;
                other.capacity = 0;
                other.offset = 0;
            }

            ArenaAllocator& operator=(ArenaAllocator&& other) noexcept {
                if (this != &other) {
                    delete[] buffer;
                    buffer = other.buffer;
                    capacity = other.capacity;
                    offset = other.offset;
                    other.buffer = nullptr;
                    other.capacity = 0;
                    other.offset = 0;
                }
                return *this;
            }

            ~ArenaAllocator() {
                delete[] buffer;
            }

            void* allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) {
                std::size_t current = reinterpret_cast<std::uintptr_t>(buffer + offset);
                std::size_t aligned = (current + alignment - 1) & ~(alignment - 1);
                std::size_t next_offset = aligned - reinterpret_cast<std::uintptr_t>(buffer) + size;

                if (next_offset > capacity)
                    throw std::bad_alloc();

                offset = next_offset;
                return reinterpret_cast<void*>(buffer + offset - size);
            }

            template<typename T, typename... Args>
            T* construct(Args&&... args) {
                void* ptr = allocate(sizeof(T), alignof(T));
                return new(ptr) T(std::forward<Args>(args)...);
            }

            void reset() {
                offset = 0;
            }

            std::size_t used() const {
                return offset;
            }

            std::size_t available() const {
                return capacity - offset;
            }

        private:
            std::uint8_t* buffer;
            std::size_t capacity;
            std::size_t offset;
        };

} // namespace utils
