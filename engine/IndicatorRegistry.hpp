#pragma once

#include "core/EngineConfig.hpp"
#include "utils/logger.h"
#include "interfaces/IIndicator.hpp"
#include "core/Candlestick.hpp"

#include <memory>
#include <mutex>
#include <vector>
#include <string>
#include <unordered_map>

namespace engine {

    class IndicatorRegistry {
        public:
            inline void register_indicator(const std::string& name, std::shared_ptr<IIndicator> indicator) {
                std::lock_guard<std::mutex> lock(mutex);
                indicators[name] = std::move(indicator);
                SAFE_LOG(INFO) << "[Indicator Registered] " << name;
            }

            inline void update_all(const Candlestick& candle) {
                std::lock_guard<std::mutex> lock(mutex);
                for (auto& [name, indicator] : indicators) {
                    indicator->update(candle);
                    SAFE_LOG(INFO) << "[Indicator Updated] " << name;
                }
            }

            inline std::vector<std::string> current_signals() const {
                std::lock_guard<std::mutex> lock(mutex);
                std::vector<std::string> signals;
                for (const auto& [name, indicator] : indicators) {
                    auto sig = indicator->signal();
                    signals.push_back(sig);
                    SAFE_LOG(INFO) << "[Signal] " << name << ": " << sig;
                }
                return signals;
            }

            inline void reset() {
                std::lock_guard<std::mutex> lock(mutex);
                indicators.clear();
                SAFE_LOG(INFO) << "[Registry Reset]";
            }

            inline std::size_t count() const {
                std::lock_guard<std::mutex> lock(mutex);
                return indicators.size();
            }

        private:
            mutable std::mutex mutex;
            std::unordered_map<std::string, std::shared_ptr<IIndicator>> indicators;
        };

} // namespace engine
