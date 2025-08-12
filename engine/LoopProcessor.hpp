#pragma once

#include "../core/EngineConfig.hpp"
#include "../security/SecurityAwareLogger.hpp"
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
                security::SecurityAwareLogger::instance().log(
                    security::SecurityAwareLogger::Level::Info,
                    "[LoopProcessor Started]");
            }

            void stop() {
                running = false;
                if (loopThread.joinable()) loopThread.join();
                security::SecurityAwareLogger::instance().log(
                    security::SecurityAwareLogger::Level::Info,
                    "[LoopProcessor Stopped]");
            }

            ~LoopProcessor() {
                stop();
            }

            inline void run(const Candlestick& candle) {
                security::SecurityAwareLogger::instance().log(
                    security::SecurityAwareLogger::Level::Info,
                    "[Manual Candle Injected] ts={}",
                    candle.start_time);
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

                    security::SecurityAwareLogger::instance().log(
                        security::SecurityAwareLogger::Level::Info,
                        "[Synthetic Tick] ts={}",
                        tick);
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
