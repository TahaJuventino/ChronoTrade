#pragma once

#include <vector>
#include <mutex>
#include <cstddef>
#include <stdexcept>

namespace engine {

    template<typename T>
    class FixedWindow {
        public:
            explicit FixedWindow(std::size_t capacity);

            void push(const T& value);
            std::size_t size() const;
            const T& at(std::size_t index) const;
            std::size_t capacity() const;

        private:
            std::vector<T> buffer;            // Circular buffer storage
            std::size_t head = 0;             // Index where the next element will be written
            std::size_t count = 0;            // Current number of valid elements in the window
            std::size_t max_capacity;         // Fixed capacity of the window
            mutable std::mutex mtx;           // Mutex for thread-safe access, mutable to allow locking in const methods
        };

        template<typename T>
        FixedWindow<T>::FixedWindow(std::size_t capacity)
            : buffer(capacity), max_capacity(capacity) {}

        template<typename T>
        void FixedWindow<T>::push(const T& value) {
            std::lock_guard<std::mutex> lock(mtx);
            buffer[head] = value;
            head = (head + 1) % max_capacity;        // Wrap around if we reach the end of the buffer
            if (count < max_capacity) ++count;       // Increase count only if not already at capacity
        }

        template<typename T>
        std::size_t FixedWindow<T>::size() const {
            std::lock_guard<std::mutex> lock(mtx);
            return count;
        }

        template<typename T>
        std::size_t FixedWindow<T>::capacity() const {
            return max_capacity;
        }

        template<typename T>
        const T& FixedWindow<T>::at(std::size_t index) const {
            std::lock_guard<std::mutex> lock(mtx);
            if (index >= count)
                throw std::out_of_range("FixedWindow: index out of bounds");

            // Translate logical index (0 to count-1) to the correct physical index in the circular buffer
            std::size_t logical_index = (head + max_capacity - count + index) % max_capacity;
            return buffer[logical_index];
        }

} // namespace engine