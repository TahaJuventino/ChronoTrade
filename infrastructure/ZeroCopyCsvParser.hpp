#pragma once
#include <vector>
#include <string_view>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>

    namespace feed {

    enum class ParseResult {
        SUCCESS,
        BUFFER_OVERFLOW,
        MALFORMED_QUOTED_FIELD,
        UNTERMINATED_QUOTE,
        INVALID_ESCAPE,
        NULL_INPUT,
        MEMORY_ALLOCATION_FAILED
    };

    struct ParseError {
        ParseResult code;
        size_t position;
        const char* description;
    };

    class ZeroCopyCsvParser {
        public:
            // Public constants for testing
            static constexpr size_t MAX_LINE_LENGTH = 1048576; // 1MB
            static constexpr size_t MAX_FIELD_COUNT = 10000;

            struct ParseOptions {
                char delimiter = ',';
                char quote_char = '"';
                char escape_char = '"'; // CSV standard: double-quote escaping
                bool allow_embedded_newlines = false;
                bool strict_mode = true; // Fail on malformed input vs best-effort parsing
                size_t max_field_count = MAX_FIELD_COUNT;
            };

        private:
            // Memory pool for field vectors to avoid repeated allocations
            struct FieldVectorPool {
                std::vector<std::vector<std::string_view>> pool;
                size_t next_free = 0;
                
                std::vector<std::string_view>& acquire() {
                    if (next_free >= pool.size()) {
                        pool.emplace_back();
                        pool.back().reserve(64); // Reasonable field count estimate
                    }
                    auto& vec = pool[next_free++];
                    vec.clear();
                    return vec;
                }
                
                void reset() { next_free = 0; }
            };
            
            // Pool for processed strings (when escapes are needed)
            struct StringPool {
                std::vector<std::string> pool;
                size_t next_free = 0;
                
                std::string& acquire() {
                    if (next_free >= pool.size()) {
                        pool.emplace_back();
                    }
                    auto& str = pool[next_free++];
                    str.clear();
                    return str;
                }
                
                void reset() { next_free = 0; }
            };
            
            static thread_local FieldVectorPool field_pool;
            static thread_local StringPool string_pool;

        public:
            // Get default options (solves default parameter initialization issue)
            static const ParseOptions& get_default_options() {
                static const ParseOptions default_opts{};
                return default_opts;
            }

            // Enhanced single-line parser with proper CSV compliance
            [[nodiscard]] static ParseResult parse_line_safe(
                const char* line, 
                size_t line_len,
                std::vector<std::string_view>& fields,
                ParseError& error,
                const ParseOptions& opts = get_default_options()) {
                
                if (!line) {
                    error = {ParseResult::NULL_INPUT, 0, "Null input buffer"};
                    return ParseResult::NULL_INPUT;
                }

                if (line_len > MAX_LINE_LENGTH) {
                    error = {ParseResult::BUFFER_OVERFLOW, line_len, "Line exceeds maximum length"};
                    return ParseResult::BUFFER_OVERFLOW;
                }

                fields.clear();
                string_pool.reset(); // Reset string pool for escaped content
                
                // Handle empty line case
                if (line_len == 0) {
                    return ParseResult::SUCCESS;
                }
                
                fields.reserve(std::min(opts.max_field_count, line_len / 4)); // Heuristic

                const char* field_start = line;
                const char* ptr = line;
                const char* end = line + line_len;
                bool in_quotes = false;
                bool field_needs_processing = false;

                while (ptr <= end) {
                    // Check if we're at end of input or at a line terminator
                    bool at_end = (ptr == end) || (!opts.allow_embedded_newlines && (*ptr == '\n' || *ptr == '\r'));
                    char current = at_end ? '\0' : *ptr;

                    // Check field count limit
                    if (fields.size() >= opts.max_field_count) {
                        error = {ParseResult::BUFFER_OVERFLOW, static_cast<size_t>(ptr - line), "Field count exceeds maximum"};
                        return ParseResult::BUFFER_OVERFLOW;
                    }

                    if (current == opts.quote_char && !at_end) {
                        if (!in_quotes) {
                            // Start of quoted field
                            if (ptr != field_start && opts.strict_mode) {
                                error = {ParseResult::MALFORMED_QUOTED_FIELD, static_cast<size_t>(ptr - line), "Quote not at field start"};
                                return ParseResult::MALFORMED_QUOTED_FIELD;
                            }
                            in_quotes = true;
                            field_start = ptr + 1; // Skip opening quote
                            field_needs_processing = false; // Reset processing flag
                        } else {
                            // Inside quotes - check for escape or end quote
                            if (ptr + 1 < end && *(ptr + 1) == opts.quote_char) {
                                // Escaped quote ("") - mark field as needing processing
                                field_needs_processing = true;
                                ++ptr; // Skip first quote, will advance past second quote at end of loop
                            } else {
                                // End of quoted field
                                if (field_needs_processing) {
                                    // Process escaped quotes
                                    auto& processed = string_pool.acquire();
                                    const char* segment_start = field_start;
                                    const char* scan_ptr = field_start;
                                    
                                    while (scan_ptr < ptr) {
                                        if (*scan_ptr == opts.quote_char && scan_ptr + 1 < ptr && *(scan_ptr + 1) == opts.quote_char) {
                                            // Found escaped quote - add content up to first quote
                                            processed.append(segment_start, scan_ptr - segment_start);
                                            processed += opts.quote_char; // Add single quote
                                            scan_ptr += 2; // Skip both quotes
                                            segment_start = scan_ptr; // Continue from after escape
                                        } else {
                                            ++scan_ptr;
                                        }
                                    }
                                    // Add remaining content
                                    processed.append(segment_start, ptr - segment_start);
                                    fields.emplace_back(processed);
                                } else {
                                    // No escapes, use direct view
                                    fields.emplace_back(field_start, ptr - field_start);
                                }
                                
                                in_quotes = false;
                                field_needs_processing = false;
                                
                                // Move past closing quote
                                ++ptr;
                                
                                // Expect delimiter or end
                                if (ptr < end && *ptr == opts.delimiter) {
                                    // Skip delimiter, start new field
                                    ++ptr;
                                    field_start = ptr;
                                    continue;
                                } else if (ptr < end && *ptr != '\n' && *ptr != '\r' && opts.strict_mode) {
                                    error = {ParseResult::MALFORMED_QUOTED_FIELD, static_cast<size_t>(ptr - line), "Content after closing quote"};
                                    return ParseResult::MALFORMED_QUOTED_FIELD;
                                } else {
                                    // End of line after quoted field
                                    break;
                                }
                            }
                        }
                    } else if ((current == opts.delimiter && !in_quotes) || (at_end && !in_quotes)) {
                        // End of unquoted field
                        fields.emplace_back(field_start, ptr - field_start);
                        
                        if (at_end) {
                            break;
                        } else {
                            // Move to next field
                            field_start = ptr + 1;
                        }
                    } else if ((current == '\n' || current == '\r') && !opts.allow_embedded_newlines) {
                        if (in_quotes) {
                            error = {ParseResult::UNTERMINATED_QUOTE, static_cast<size_t>(ptr - line), "Unterminated quote at line end"};
                            return ParseResult::UNTERMINATED_QUOTE;
                        } else {
                            break; // End of line
                        }
                    }

                    ++ptr;
                }

                if (in_quotes) {
                    error = {ParseResult::UNTERMINATED_QUOTE, line_len, "Unterminated quote at end"};
                    return ParseResult::UNTERMINATED_QUOTE;
                }

                return ParseResult::SUCCESS;
            }

            // Destructive but fast parser for trusted input (original behavior preserved)
            [[deprecated("Use parse_line_safe for production code")]]
            static std::vector<std::string_view> parse_line_unsafe(char* line) {
                assert(line != nullptr);
                
                std::vector<std::string_view> fields;
                char* start = line;
                char* ptr = line;
                
                while (*ptr) {
                    if (*ptr == ',') {
                        *ptr = '\0';
                        fields.emplace_back(start, ptr - start);
                        start = ptr + 1;
                    }
                    ++ptr;
                }
                
                if (ptr != start) {
                    fields.emplace_back(start, ptr - start);
                }
                
                return fields;
            }

            // Enhanced buffer parser with memory safety
            [[nodiscard]] static ParseResult parse_buffer_safe(
                char* buffer,  // FIXED: Must be mutable for zero-copy parsing
                size_t len,
                std::vector<std::vector<std::string_view>>& result,
                ParseError& error,
                const ParseOptions& opts = get_default_options()
            ) {
                if (!buffer) {
                    error = {ParseResult::NULL_INPUT, 0, "Null input buffer"};
                    return ParseResult::NULL_INPUT;
                }
                
                result.clear();
                field_pool.reset();
                string_pool.reset();
                
                size_t line_start = 0;
                size_t line_count = 0;
                
                for (size_t i = 0; i <= len; ++i) { // <= to handle final line
                    bool is_line_end = (i == len) || (buffer[i] == '\n') || 
                                    (buffer[i] == '\r' && i + 1 < len && buffer[i + 1] == '\n');
                    
                    if (is_line_end) {
                        size_t line_len = i - line_start;
                        
                        if (line_len > 0) {
                            auto& fields = field_pool.acquire();
                            ParseResult line_result = parse_line_safe(
                                &buffer[line_start], line_len, fields, error, opts
                            );
                            
                            if (line_result != ParseResult::SUCCESS) {
                                error.position += line_start; // Adjust position to buffer offset
                                return line_result;
                            }
                            
                            result.emplace_back(std::move(fields));
                            ++line_count;
                        }
                        
                        // Handle CRLF
                        if (i < len && buffer[i] == '\r' && i + 1 < len && buffer[i + 1] == '\n') {
                            ++i;
                        }
                        
                        line_start = i + 1;
                    }
                }
                
                return ParseResult::SUCCESS;
            }

            // Original unsafe buffer parser (preserved for backward compatibility)
            [[deprecated("Use parse_buffer_safe for production code")]]
            static std::vector<std::vector<std::string_view>> parse_buffer_unsafe(char* buffer, size_t len) {
                assert(buffer != nullptr);
                
                std::vector<std::vector<std::string_view>> result;
                size_t line_start = 0;
                
                for (size_t i = 0; i < len; ++i) {
                    if (buffer[i] == '\n') {
                        buffer[i] = '\0';
                        
                        // Use safe parsing but preserve deprecated interface
                        std::vector<std::string_view> temp_fields;
                        ParseError temp_err;
                        auto parse_result = parse_line_safe(&buffer[line_start], i - line_start, temp_fields, temp_err, get_default_options());
                        if (parse_result == ParseResult::SUCCESS) {
                            result.emplace_back(std::move(temp_fields));
                        }
                        
                        line_start = i + 1;
                    }
                }
                
                // Handle last line if no trailing newline
                if (line_start < len) {
                    std::vector<std::string_view> temp_fields;
                    ParseError temp_err;
                    auto parse_result = parse_line_safe(&buffer[line_start], len - line_start, temp_fields, temp_err, get_default_options());
                    if (parse_result == ParseResult::SUCCESS) {
                        result.emplace_back(std::move(temp_fields));
                    }
                }
                
                return result;
            }

            // Utility: Quick helper to parse a single line safely with RAII
            // WARNING: Returned vector is pool-managed. For multiple calls, use parse_line_owned()
            static std::optional<std::vector<std::string_view>> parse_line_optional(
                const char* line, size_t len, ParseError& err, const ParseOptions& opts = get_default_options()) {
                
                field_pool.reset(); // Reset pool to avoid vector reuse corruption
                string_pool.reset(); // Reset string pool
                auto& vec = field_pool.acquire();
                auto res = parse_line_safe(line, len, vec, err, opts);
                if (res != ParseResult::SUCCESS) return std::nullopt;
                return std::move(vec);
            }

            // Utility: Parse single line with owned vector (safe for multiple calls)
            static std::optional<std::vector<std::string_view>> parse_line_owned(
                const char* line, size_t len, ParseError& err, const ParseOptions& opts = get_default_options()) {
                
                field_pool.reset(); // Reset to get clean state
                string_pool.reset(); // Reset string pool
                auto& vec = field_pool.acquire();
                auto res = parse_line_safe(line, len, vec, err, opts);
                if (res != ParseResult::SUCCESS) return std::nullopt;
                
                // Return owned copy to prevent pool reuse corruption
                return std::vector<std::string_view>(vec.begin(), vec.end());
            }

            // Debug variant for string input (fuzz testing)
            static ParseResult parse_line_debug(
                const std::string& s, std::vector<std::string_view>& fields, 
                ParseError& err, const ParseOptions& opts = get_default_options()) {
                return parse_line_safe(s.data(), s.size(), fields, err, opts);
            }

            // RFC-4180 compliance checker
            static constexpr bool is_rfc_compliant(const ParseOptions& opts) {
                return opts.delimiter == ',' && 
                    opts.quote_char == '"' && 
                    opts.escape_char == '"' &&
                    opts.strict_mode;
            }
            
            // Utility: Get human-readable error description
            static const char* get_error_description(ParseResult result) {
                switch (result) {
                    case ParseResult::SUCCESS: return "Success";
                    case ParseResult::BUFFER_OVERFLOW: return "Buffer overflow or size limit exceeded";
                    case ParseResult::MALFORMED_QUOTED_FIELD: return "Malformed quoted field";
                    case ParseResult::UNTERMINATED_QUOTE: return "Unterminated quote";
                    case ParseResult::INVALID_ESCAPE: return "Invalid escape sequence";
                    case ParseResult::NULL_INPUT: return "Null input provided";
                    case ParseResult::MEMORY_ALLOCATION_FAILED: return "Memory allocation failed";
                    default: return "Unknown error";
                }
            }
        };

    // Thread-local storage definition
    // WARNING: This parser is NOT safe for use across async/coroutine boundaries
    // where execution may resume on different threads. Thread-local storage
    // guarantees are only valid within the same OS thread context.
    // For async parsing, allocate parser per coroutine or use explicit pools.
    thread_local ZeroCopyCsvParser::FieldVectorPool ZeroCopyCsvParser::field_pool;
    thread_local ZeroCopyCsvParser::StringPool ZeroCopyCsvParser::string_pool;

} // namespace feed
