#pragma once

#include "../core/EngineConfig.hpp"
#include "../utils/logger.h"
#include "../core/Candlestick.hpp"
#include "../engine/IndicatorRegistry.hpp"

#include <atomic>
#include <thread>
#include <chrono>
#include <sstream>

namespace engine {

    class LoopProcessor {
        public:
            LoopProcessor(IndicatorRegistry& registry, int interval_ms = 1000)
                : registry_(registry), interval(interval_ms), running(false) {}

            void start() {
                running = true;
                loopThread = std::thread([this] { run(); });
                SAFE_LOG(INFO) << "[LoopProcessor Started]";
            }

            void stop() {
                running = false;
                if (loopThread.joinable()) loopThread.join();
                SAFE_LOG(INFO) << "[LoopProcessor Stopped]";
            }

            ~LoopProcessor() {
                stop();
            }

            inline void run(const Candlestick& candle) {
                SAFE_LOG(INFO) << "[Manual Candle Injected] ts=" << candle.start_time;
                registry_.update_all(candle);
            }

        private:
            void run() {
                int tick = 0;
                while (running) {
                    Candlestick fake(
                        100.0 + tick, 101.0 + tick, 99.0 + tick, 100.5 + tick,
                        1.0, tick, tick + 1
                    );

                    SAFE_LOG(INFO) << "[Synthetic Tick] ts=" << tick;
                    registry_.update_all(fake);
                    std::this_thread::sleep_for(std::chrono::milliseconds(interval));
                    ++tick;
                }
            }

            IndicatorRegistry& registry_;
            int interval;
            std::atomic<bool> running;
            std::thread loopThread;
        };

} // namespace engine
