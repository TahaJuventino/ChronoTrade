#include <gtest/gtest.h>
#include <vector>
#include <string_view>
#include <optional>
#include <random>
#include <thread>
#include <future>
#include <chrono>
#include <algorithm>
#include <memory>
#include <limits>
#include "infrastructure/ZeroCopyCsvParser.hpp"

using namespace feed;
using namespace std::chrono_literals;

class ZeroCopyCsvParserTest : public ::testing::Test {
    protected:
        ZeroCopyCsvParser::ParseOptions default_opts;
        ZeroCopyCsvParser::ParseOptions strict_opts{',', '"', '"', false, true, 1000};
        ZeroCopyCsvParser::ParseOptions lenient_opts{',', '"', '"', true, false, 10000};
        ParseError err;
        
        void SetUp() override {
            // Reset error state
            err = {ParseResult::SUCCESS, 0, ""};
        }
        
        // Helper: Generate random CSV data for stress testing
        std::string generate_random_csv_line(size_t field_count, size_t max_field_len, bool include_quotes = true) {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> len_dist(1, max_field_len);
            std::uniform_int_distribution<> char_dist('A', 'Z');
            std::uniform_real_distribution<> quote_prob(0.0, 1.0);
            
            std::string result;
            for (size_t i = 0; i < field_count; ++i) {
                if (i > 0) result += ',';
                
                size_t field_len = len_dist(gen);
                bool should_quote = include_quotes && quote_prob(gen) < 0.3;
                
                if (should_quote) result += '"';
                
                for (size_t j = 0; j < field_len; ++j) {
                    char c = static_cast<char>(char_dist(gen));
                    if (should_quote && c == '"') {
                        result += "\"\""; // RFC-4180 escape
                    } else {
                        result += c;
                    }
                }
                
                if (should_quote) result += '"';
            }
            return result;
        }
        
        // Helper: Create pathological input for boundary testing
        std::vector<char> create_malicious_buffer(size_t size, char fill_char = 'A') {
            std::vector<char> buffer(size, fill_char);
            // Insert some delimiters to create massive field count
            for (size_t i = 1; i < size; i += 2) {
                buffer[i] = ',';
            }
            buffer[size - 1] = '\0';
            return buffer;
        }
};

// BASIC FUNCTIONALITY TESTS
TEST_F(ZeroCopyCsvParserTest, SingleUnquotedLine) {
    const char* line = "A,B,C";
    std::vector<std::string_view> fields;
    auto result = ZeroCopyCsvParser::parse_line_safe(line, std::strlen(line), fields, err, default_opts);
    
    ASSERT_EQ(result, ParseResult::SUCCESS);
    ASSERT_EQ(fields.size(), 3);
    EXPECT_EQ(fields[0], "A");
    EXPECT_EQ(fields[1], "B");
    EXPECT_EQ(fields[2], "C");
}

TEST_F(ZeroCopyCsvParserTest, SingleQuotedField) {
    const char* line = "\"hello,world\",X,Y";
    std::vector<std::string_view> fields;
    auto result = ZeroCopyCsvParser::parse_line_safe(line, std::strlen(line), fields, err, default_opts);
    
    ASSERT_EQ(result, ParseResult::SUCCESS);
    ASSERT_EQ(fields.size(), 3);
    EXPECT_EQ(fields[0], "hello,world");
    EXPECT_EQ(fields[1], "X");
    EXPECT_EQ(fields[2], "Y");
}

// RFC-4180 COMPLIANCE TESTS
TEST_F(ZeroCopyCsvParserTest, RFC4180_DoubleQuoteEscaping) {
    const char* line = "\"She said \"\"Hello\"\" to me\",normal,\"end\"";
    std::vector<std::string_view> fields;
    auto result = ZeroCopyCsvParser::parse_line_safe(line, std::strlen(line), fields, err, default_opts);
    
    ASSERT_EQ(result, ParseResult::SUCCESS);
    ASSERT_EQ(fields.size(), 3);
    EXPECT_EQ(fields[0], "She said \"Hello\" to me");
    EXPECT_EQ(fields[1], "normal");
    EXPECT_EQ(fields[2], "end");
}

TEST_F(ZeroCopyCsvParserTest, RFC4180_EmptyFields) {
    const char* line = ",,empty,,last";
    std::vector<std::string_view> fields;
    auto result = ZeroCopyCsvParser::parse_line_safe(line, std::strlen(line), fields, err, default_opts);
    
    ASSERT_EQ(result, ParseResult::SUCCESS);
    ASSERT_EQ(fields.size(), 5);
    EXPECT_EQ(fields[0], "");
    EXPECT_EQ(fields[1], "");
    EXPECT_EQ(fields[2], "empty");
    EXPECT_EQ(fields[3], "");
    EXPECT_EQ(fields[4], "last");
}

TEST_F(ZeroCopyCsvParserTest, RFC4180_QuotedEmptyFields) {
    const char* line = "\"\",\"non-empty\",\"\"";
    std::vector<std::string_view> fields;
    auto result = ZeroCopyCsvParser::parse_line_safe(line, std::strlen(line), fields, err, default_opts);
    
    ASSERT_EQ(result, ParseResult::SUCCESS);
    ASSERT_EQ(fields.size(), 3);
    EXPECT_EQ(fields[0], "");
    EXPECT_EQ(fields[1], "non-empty");
    EXPECT_EQ(fields[2], "");
}

TEST_F(ZeroCopyCsvParserTest, RFC4180_NewlinesInQuotedFields) {
    const char* line = "\"Line1\nLine2\",normal,\"Line3\r\nLine4\"";
    std::vector<std::string_view> fields;
    auto result = ZeroCopyCsvParser::parse_line_safe(line, std::strlen(line), fields, err, lenient_opts);
    
    ASSERT_EQ(result, ParseResult::SUCCESS);
    ASSERT_EQ(fields.size(), 3);
    EXPECT_EQ(fields[0], "Line1\nLine2");
    EXPECT_EQ(fields[1], "normal");
    EXPECT_EQ(fields[2], "Line3\r\nLine4");
}

// ERROR HANDLING TESTS
TEST_F(ZeroCopyCsvParserTest, ErrorHandling_NullInput) {
    std::vector<std::string_view> fields;
    auto result = ZeroCopyCsvParser::parse_line_safe(nullptr, 0, fields, err, default_opts);
    
    EXPECT_EQ(result, ParseResult::NULL_INPUT);
    EXPECT_EQ(err.code, ParseResult::NULL_INPUT);
    EXPECT_EQ(err.position, 0);
}

TEST_F(ZeroCopyCsvParserTest, ErrorHandling_UnterminatedQuote) {
    const char* line = "normal,\"unterminated quote";
    std::vector<std::string_view> fields;
    auto result = ZeroCopyCsvParser::parse_line_safe(line, std::strlen(line), fields, err, strict_opts);
    
    EXPECT_EQ(result, ParseResult::UNTERMINATED_QUOTE);
    EXPECT_EQ(err.code, ParseResult::UNTERMINATED_QUOTE);
    EXPECT_GT(err.position, 0);
}

TEST_F(ZeroCopyCsvParserTest, ErrorHandling_MalformedQuotedField) {
    const char* line = "normal,x\"invalid quote placement\",end";
    std::vector<std::string_view> fields;
    auto result = ZeroCopyCsvParser::parse_line_safe(line, std::strlen(line), fields, err, strict_opts);
    
    EXPECT_EQ(result, ParseResult::MALFORMED_QUOTED_FIELD);
    EXPECT_EQ(err.code, ParseResult::MALFORMED_QUOTED_FIELD);
}

TEST_F(ZeroCopyCsvParserTest, ErrorHandling_ExcessiveFieldCount) {
    ZeroCopyCsvParser::ParseOptions limited_opts{',', '"', '"', false, true, 5}; // Max 5 fields
    const char* line = "1,2,3,4,5,6,7,8,9,10"; // 10 fields
    std::vector<std::string_view> fields;
    auto result = ZeroCopyCsvParser::parse_line_safe(line, std::strlen(line), fields, err, limited_opts);
    
    EXPECT_EQ(result, ParseResult::BUFFER_OVERFLOW);
    EXPECT_EQ(err.code, ParseResult::BUFFER_OVERFLOW);
}

TEST_F(ZeroCopyCsvParserTest, ErrorHandling_ExcessiveLineLength) {
    std::string long_line(ZeroCopyCsvParser::MAX_LINE_LENGTH + 1000, 'X');
    std::vector<std::string_view> fields;
    auto result = ZeroCopyCsvParser::parse_line_safe(long_line.c_str(), long_line.size(), fields, err, default_opts);
    
    EXPECT_EQ(result, ParseResult::BUFFER_OVERFLOW);
    EXPECT_EQ(err.code, ParseResult::BUFFER_OVERFLOW);
}

// BOUNDARY AND EDGE CASE TESTS
TEST_F(ZeroCopyCsvParserTest, Boundary_SingleCharField) {
    const char* line = "A";
    std::vector<std::string_view> fields;
    auto result = ZeroCopyCsvParser::parse_line_safe(line, 1, fields, err, default_opts);
    
    ASSERT_EQ(result, ParseResult::SUCCESS);
    ASSERT_EQ(fields.size(), 1);
    EXPECT_EQ(fields[0], "A");
}

TEST_F(ZeroCopyCsvParserTest, Boundary_EmptyLine) {
    const char* line = "";
    std::vector<std::string_view> fields;
    auto result = ZeroCopyCsvParser::parse_line_safe(line, 0, fields, err, default_opts);
    
    ASSERT_EQ(result, ParseResult::SUCCESS);
    EXPECT_EQ(fields.size(), 0);
}

TEST_F(ZeroCopyCsvParserTest, Boundary_OnlyDelimiters) {
    const char* line = ",,,";
    std::vector<std::string_view> fields;
    auto result = ZeroCopyCsvParser::parse_line_safe(line, std::strlen(line), fields, err, default_opts);
    
    ASSERT_EQ(result, ParseResult::SUCCESS);
    ASSERT_EQ(fields.size(), 4); // Four empty fields
    for (const auto& field : fields) {
        EXPECT_EQ(field, "");
    }
}

TEST_F(ZeroCopyCsvParserTest, Boundary_TrailingDelimiter) {
    const char* line = "A,B,C,";
    std::vector<std::string_view> fields;
    auto result = ZeroCopyCsvParser::parse_line_safe(line, std::strlen(line), fields, err, default_opts);
    
    ASSERT_EQ(result, ParseResult::SUCCESS);
    ASSERT_EQ(fields.size(), 4);
    EXPECT_EQ(fields[0], "A");
    EXPECT_EQ(fields[1], "B");
    EXPECT_EQ(fields[2], "C");
    EXPECT_EQ(fields[3], ""); // Empty trailing field
}

TEST_F(ZeroCopyCsvParserTest, Boundary_MaxFieldsExactly) {
    ZeroCopyCsvParser::ParseOptions exact_opts{',', '"', '"', false, true, 3};
    const char* line = "A,B,C";
    std::vector<std::string_view> fields;
    auto result = ZeroCopyCsvParser::parse_line_safe(line, std::strlen(line), fields, err, exact_opts);
    
    ASSERT_EQ(result, ParseResult::SUCCESS);
    EXPECT_EQ(fields.size(), 3);
}

// STRESS AND PERFORMANCE TESTS
TEST_F(ZeroCopyCsvParserTest, Stress_LargeFieldCount) {
    const size_t FIELD_COUNT = 5000;
    std::string line;
    
    for (size_t i = 0; i < FIELD_COUNT; ++i) {
        if (i > 0) line += ',';
        line += "field" + std::to_string(i);
    }
    
    std::vector<std::string_view> fields;
    ZeroCopyCsvParser::ParseOptions high_limit_opts{',', '"', '"', false, true, FIELD_COUNT + 100};
    
    auto start = std::chrono::high_resolution_clock::now();
    auto result = ZeroCopyCsvParser::parse_line_safe(line.c_str(), line.size(), fields, err, high_limit_opts);
    auto end = std::chrono::high_resolution_clock::now();
    
    ASSERT_EQ(result, ParseResult::SUCCESS);
    EXPECT_EQ(fields.size(), FIELD_COUNT);
    
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    EXPECT_LT(duration.count(), 10000); // Should parse in under 10ms
    
    // Verify field contents
    for (size_t i = 0; i < std::min(fields.size(), FIELD_COUNT); ++i) {
        EXPECT_EQ(fields[i], "field" + std::to_string(i));
    }
}

TEST_F(ZeroCopyCsvParserTest, Stress_RandomDataGeneration) {
    const size_t NUM_ITERATIONS = 1000;
    const size_t MAX_FIELDS = 100;
    const size_t MAX_FIELD_LEN = 50;
    
    for (size_t i = 0; i < NUM_ITERATIONS; ++i) {
        std::string csv_line = generate_random_csv_line(
            std::rand() % MAX_FIELDS + 1, 
            std::rand() % MAX_FIELD_LEN + 1
        );
        
        std::vector<std::string_view> fields;
        ZeroCopyCsvParser::ParseOptions liberal_opts{',', '"', '"', true, false, MAX_FIELDS + 10};
        
        auto result = ZeroCopyCsvParser::parse_line_safe(
            csv_line.c_str(), csv_line.size(), fields, err, liberal_opts
        );
        
        // Should succeed in lenient mode
        EXPECT_EQ(result, ParseResult::SUCCESS) 
            << "Failed on iteration " << i << " with line: " << csv_line;
        EXPECT_GT(fields.size(), 0) << "No fields parsed from: " << csv_line;
    }
}

TEST_F(ZeroCopyCsvParserTest, Stress_PathologicalInput) {
    // Create input designed to trigger worst-case behavior
    auto malicious_buffer = create_malicious_buffer(10000); // 5000 single-char fields
    
    std::vector<std::string_view> fields;
    ZeroCopyCsvParser::ParseOptions high_limit_opts{',', '"', '"', false, false, 6000};
    
    auto result = ZeroCopyCsvParser::parse_line_safe(
        malicious_buffer.data(), malicious_buffer.size() - 1, fields, err, high_limit_opts
    );
    
    // Should handle gracefully
    if (result == ParseResult::SUCCESS) {
        EXPECT_GT(fields.size(), 1000);
    } else {
        EXPECT_EQ(result, ParseResult::BUFFER_OVERFLOW);
    }
}

// BUFFER PARSING TESTS
TEST_F(ZeroCopyCsvParserTest, Buffer_MultipleLines) {
    std::string buffer = "A,B,C\nX,Y,Z\n1,2,3";
    std::vector<char> mutable_buffer(buffer.begin(), buffer.end());
    mutable_buffer.push_back('\0');
    
    std::vector<std::vector<std::string_view>> result;
    auto parse_result = ZeroCopyCsvParser::parse_buffer_safe(
        mutable_buffer.data(), mutable_buffer.size() - 1, result, err, default_opts
    );
    
    ASSERT_EQ(parse_result, ParseResult::SUCCESS);
    ASSERT_EQ(result.size(), 3);
    
    EXPECT_EQ(result[0].size(), 3);
    EXPECT_EQ(result[0][0], "A");
    EXPECT_EQ(result[0][1], "B");
    EXPECT_EQ(result[0][2], "C");
    
    EXPECT_EQ(result[2].size(), 3);
    EXPECT_EQ(result[2][0], "1");
    EXPECT_EQ(result[2][1], "2");
    EXPECT_EQ(result[2][2], "3");
}

TEST_F(ZeroCopyCsvParserTest, Buffer_CRLF_LineEndings) {
    std::string buffer = "A,B,C\r\nX,Y,Z\r\n";
    std::vector<char> mutable_buffer(buffer.begin(), buffer.end());
    
    std::vector<std::vector<std::string_view>> result;
    auto parse_result = ZeroCopyCsvParser::parse_buffer_safe(
        mutable_buffer.data(), mutable_buffer.size(), result, err, default_opts
    );
    
    ASSERT_EQ(parse_result, ParseResult::SUCCESS);
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0][0], "A");
    EXPECT_EQ(result[1][0], "X");
}

// THREAD SAFETY TESTS
TEST_F(ZeroCopyCsvParserTest, ThreadSafety_ConcurrentParsing) {
    const size_t NUM_THREADS = 8;
    const size_t ITERATIONS_PER_THREAD = 100;
    
    std::vector<std::future<bool>> futures;
    std::atomic<bool> all_success{true};
    
    for (size_t t = 0; t < NUM_THREADS; ++t) {
        futures.emplace_back(std::async(std::launch::async, [&, t]() {
            bool thread_success = true;
            
            for (size_t i = 0; i < ITERATIONS_PER_THREAD; ++i) {
                std::string csv_line = "thread" + std::to_string(t) + "_" + std::to_string(i) + ",data,end";
                
                ParseError thread_err;
                auto result = ZeroCopyCsvParser::parse_line_owned(
                    csv_line.c_str(), csv_line.size(), thread_err, default_opts
                );
                
                if (!result || result->size() != 3) {
                    thread_success = false;
                    break;
                }
            }
            
            if (!thread_success) {
                all_success = false;
            }
            return thread_success;
        }));
    }
    
    // Wait for all threads
    for (auto& future : futures) {
        EXPECT_TRUE(future.get()) << "Thread reported parsing failures";
    }
    
    EXPECT_TRUE(all_success.load()) << "Concurrent parsing failed";
}

// POOL MANAGEMENT TESTS
TEST_F(ZeroCopyCsvParserTest, PoolManagement_MultipleOptionalCalls) {
    const char* line1 = "A,B,C";
    const char* line2 = "X,Y,Z";
    
    auto result1 = ZeroCopyCsvParser::parse_line_optional(line1, std::strlen(line1), err, default_opts);
    auto result2 = ZeroCopyCsvParser::parse_line_optional(line2, std::strlen(line2), err, default_opts);
    
    ASSERT_TRUE(result1.has_value());
    ASSERT_TRUE(result2.has_value());
    
    // Verify both results are valid and independent
    EXPECT_EQ((*result1)[0], "A");
    EXPECT_EQ((*result2)[0], "X");
    
    // Results should not reference the same vector
    EXPECT_NE(result1->data(), result2->data());
}

TEST_F(ZeroCopyCsvParserTest, PoolManagement_OwnedVsOptional) {
    const char* line = "test,data,end";
    
    auto owned_result = ZeroCopyCsvParser::parse_line_owned(line, std::strlen(line), err, default_opts);
    auto optional_result = ZeroCopyCsvParser::parse_line_optional(line, std::strlen(line), err, default_opts);
    
    ASSERT_TRUE(owned_result.has_value());
    ASSERT_TRUE(optional_result.has_value());
    
    // Both should have identical content
    EXPECT_EQ(owned_result->size(), optional_result->size());
    for (size_t i = 0; i < owned_result->size(); ++i) {
        EXPECT_EQ((*owned_result)[i], (*optional_result)[i]);
    }
}

// UTILITY AND CONFIGURATION TESTS
TEST_F(ZeroCopyCsvParserTest, Utility_RFC_ComplianceChecker) {
    ZeroCopyCsvParser::ParseOptions rfc_compliant{',', '"', '"', false, true};
    ZeroCopyCsvParser::ParseOptions non_compliant{'|', '\'', '\'', false, true};
    
    EXPECT_TRUE(ZeroCopyCsvParser::is_rfc_compliant(rfc_compliant));
    EXPECT_FALSE(ZeroCopyCsvParser::is_rfc_compliant(non_compliant));
}

TEST_F(ZeroCopyCsvParserTest, Utility_ErrorDescriptions) {
    EXPECT_STRNE(ZeroCopyCsvParser::get_error_description(ParseResult::SUCCESS), "");
    EXPECT_STRNE(ZeroCopyCsvParser::get_error_description(ParseResult::NULL_INPUT), "");
    EXPECT_STRNE(ZeroCopyCsvParser::get_error_description(ParseResult::BUFFER_OVERFLOW), "");
}

TEST_F(ZeroCopyCsvParserTest, Configuration_CustomDelimiters) {
    ZeroCopyCsvParser::ParseOptions tab_opts{'\t', '"', '"', false, true};
    const char* line = "A\tB\tC";
    std::vector<std::string_view> fields;
    
    auto result = ZeroCopyCsvParser::parse_line_safe(line, std::strlen(line), fields, err, tab_opts);
    
    ASSERT_EQ(result, ParseResult::SUCCESS);
    ASSERT_EQ(fields.size(), 3);
    EXPECT_EQ(fields[0], "A");
    EXPECT_EQ(fields[1], "B");
    EXPECT_EQ(fields[2], "C");
}

// BENCHMARK TESTS
TEST_F(ZeroCopyCsvParserTest, Benchmark_ParsingSpeed) {
    // Create large CSV data
    const size_t NUM_LINES = 1000;
    const size_t FIELDS_PER_LINE = 20;
    
    std::string large_csv;
    for (size_t line = 0; line < NUM_LINES; ++line) {
        if (line > 0) large_csv += '\n';
        for (size_t field = 0; field < FIELDS_PER_LINE; ++field) {
            if (field > 0) large_csv += ',';
            large_csv += "data_" + std::to_string(line) + "_" + std::to_string(field);
        }
    }
    
    std::vector<char> mutable_buffer(large_csv.begin(), large_csv.end());
    std::vector<std::vector<std::string_view>> result;
    
    auto start = std::chrono::high_resolution_clock::now();
    auto parse_result = ZeroCopyCsvParser::parse_buffer_safe(
        mutable_buffer.data(), mutable_buffer.size(), result, err, default_opts
    );
    auto end = std::chrono::high_resolution_clock::now();
    
    ASSERT_EQ(parse_result, ParseResult::SUCCESS);
    EXPECT_EQ(result.size(), NUM_LINES);
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    double mb_per_sec = (large_csv.size() / (1024.0 * 1024.0)) / (duration.count() / 1000.0);
    
    // Should achieve reasonable parsing speed (>1 MB/s on modern hardware)
    EXPECT_GT(mb_per_sec, 1.0) << "Parsing speed too slow: " << mb_per_sec << " MB/s";
    
    std::cout << "Parsing performance: " << mb_per_sec << " MB/s" << std::endl;
}