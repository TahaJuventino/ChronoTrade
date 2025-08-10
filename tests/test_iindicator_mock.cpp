#include <gtest/gtest.h>
#include "../engine/IIndicator.hpp"
#include "../core/Candlestick.hpp"

class MockIndicator : public engine::IIndicator {
public:
    int updates = 0;

    void update(const engine::Candlestick&) override {
        ++updates;
    }

    std::string signal() const override {
        return "MOCK";
    }

    double value() const override {
        return 42.0;
    }

    ~MockIndicator() override = default;
};

TEST(IIndicatorTest, MockIndicatorBehavior) {
    MockIndicator mock;
    engine::Candlestick dummy(100, 105, 95, 102, 1000, 0, 60);

    engine::IIndicator* ptr = &mock;
    ptr->update(dummy);

    EXPECT_EQ(mock.updates, 1);
    EXPECT_EQ(ptr->signal(), "MOCK");
    EXPECT_DOUBLE_EQ(ptr->value(), 42.0);
}