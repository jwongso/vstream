// -------------------------------------------------------------------------------------------------
//
// Copyright (C) all of the contributors. All rights reserved.
//
// This software, including documentation, is protected by copyright controlled by
// contributors. All rights are reserved. Copying, including reproducing, storing,
// adapting or translating, any or all of this material requires the prior written
// consent of all contributors.
//
// -------------------------------------------------------------------------------------------------

#ifndef LOGGING_H
#define LOGGING_H

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <mutex>

/**
 * @brief Thread-safe singleton logger class designed for embedded systems
 *
 * This logger provides synchronous logging capabilities with the following features:
 * - Thread-safe operations using mutex protection
 * - Exception-safe design with noexcept guarantees
 * - Dual output support (console and file)
 * - Configurable log levels with filtering
 * - Automatic log file generation with timestamps
 * - Section-based logging for structured output
 * - Utility functions for text processing and JSON handling
 *
 * The logger is designed as a singleton to ensure consistent logging across
 * the entire application while maintaining thread safety and resource efficiency.
 */
class logger {
public:
    /**
     * @brief Log severity levels in ascending order of importance
     *
     * DEBUG: Detailed information for debugging purposes
     * INFO: General informational messages
     * WARNING: Warning messages for potential issues
     * ERROR: Error messages for serious problems
     */
    enum class Level {
        DEBUG,
        INFO,
        WARNING,
        ERROR
    };

    // Delete copy/move operations to enforce singleton pattern
    logger(const logger&) = delete;
    logger& operator=(const logger&) = delete;
    logger(logger&&) = delete;
    logger& operator=(logger&&) = delete;

    /**
     * @brief Get the singleton instance of the logger
     * @return Reference to the singleton logger instance
     * @note Thread-safe singleton implementation
     */
    static logger& instance() noexcept;

    /**
     * @brief Initialize the logging system
     * @param enable_file_logging Enable logging to file (default: true)
     * @param enable_console_logging Enable logging to console/stderr (default: true)
     * @note Must be called before using other logging functions
     * @note Safe to call multiple times - will reinitialize if needed
     */
    void init(bool enable_file_logging = true, bool enable_console_logging = true) noexcept;

    /**
     * @brief Check if logging system is properly initialized and enabled
     * @return true if at least one output method (file or console) is enabled
     */
    bool is_enabled() const noexcept;

    /**
     * @brief Core logging function - thread-safe and exception-safe
     * @param level Log level for this message
     * @param message The message to log
     * @param file Source file name (optional, used by macros)
     * @param line Source line number (optional, used by macros)
     * @note Messages below the minimum log level are filtered out
     * @note All exceptions are caught internally to prevent crashes
     */
    void log(Level level, const std::string& message,
             const std::string& file = "", int line = -1) noexcept;

    /**
     * @brief Log a titled section with multiple messages
     * @param title Section title (will be surrounded by decorative borders)
     * @param messages Vector of messages to log in the section
     * @param level Log level for the entire section (default: INFO)
     * @note Useful for grouping related log entries together
     */
    void log_section(const std::string& title,
                     const std::vector<std::string>& messages,
                     Level level = Level::INFO) noexcept;

    /**
     * @brief Set the minimum log level to output
     * @param level Minimum level - messages below this level are ignored
     * @note Default minimum level is DEBUG (all messages shown)
     */
    void set_min_level(Level level) noexcept;

    /**
     * @brief Get the current log file name
     * @return Log file name, or empty string if file logging is disabled
     */
    std::string get_log_file_name() const noexcept;

    /**
     * @brief Force flush of log file buffers to disk
     * @note Useful for ensuring logs are written before critical operations
     */
    void flush() noexcept;

    /**
     * @brief Shutdown the logging system and close files
     * @note Safe to call multiple times
     * @note Automatically called by destructor
     */
    void shutdown() noexcept;

    /**
     * @brief Utility function to truncate long text strings
     * @param text Input text to potentially truncate
     * @param max_length Maximum allowed length (default: 100)
     * @return Truncated text with "..." suffix if needed
     * @note Static function - can be used without logger instance
     */
    static std::string truncate_text(const std::string& text, size_t max_length = 100) noexcept;

    /**
     * @brief Utility function to extract JSON object keys
     * @param j JSON object to analyze
     * @return Comma-separated string of keys, or "(none)" if empty
     * @note Static function - can be used without logger instance
     * @note Exception-safe - returns error message if JSON parsing fails
     */
    static std::string get_json_keys(const nlohmann::json& j) noexcept;

private:
    /**
     * @brief Private constructor for singleton pattern
     */
    logger() = default;

    /**
     * @brief Private destructor - calls shutdown()
     */
    ~logger() noexcept;

    /**
     * @brief Internal state structure to encapsulate all logger data
     *
     * Using a struct allows for easy initialization and cleanup
     * while keeping the interface clean.
     */
    struct loggerState {
        bool file_logging_enabled = false;      ///< File logging enabled flag
        bool console_logging_enabled = true;    ///< Console logging enabled flag
        Level min_level = Level::DEBUG;         ///< Minimum log level filter
        std::ofstream log_file;                 ///< Output file stream
        std::string log_file_name;              ///< Current log file name
    };

    /// Pointer to internal state (allows for clean reinitialization)
    std::unique_ptr<loggerState> m_state;

    /// Mutex for thread-safe operations
    mutable std::mutex m_mutex;

    /**
     * @brief Generate a unique log filename with timestamp
     * @return Generated filename in format "hyni_log_YYYYMMDD_HHMMSS.log"
     * @note Exception-safe - returns default name if timestamp generation fails
     */
    std::string generate_log_filename() noexcept;

    /**
     * @brief Convert log level enum to string representation
     * @param level Log level to convert
     * @return String representation of the level
     */
    std::string level_to_string(Level level) const noexcept;

    /**
     * @brief Get current timestamp as formatted string
     * @return Current time in "YYYY-MM-DD HH:MM:SS" format
     * @note Exception-safe - returns error message if time formatting fails
     */
    std::string current_time() const noexcept;
};

/**
 * @brief Convenience macros for easier logging with automatic file/line info
 *
 * These macros automatically capture __FILE__ and __LINE__ for better
 * debugging information in log messages.
 *
 * Usage examples:
 *   LOG_DEBUG("Detailed debug information");
 *   LOG_INFO("Application started successfully");
 *   LOG_WARNING("Configuration file not found, using defaults");
 *   LOG_ERROR("Failed to connect to database");
 */
#define LOG_DEBUG(msg) logger::instance().log(logger::Level::DEBUG, msg, __FILE__, __LINE__)
#define LOG_INFO(msg) logger::instance().log(logger::Level::INFO, msg, __FILE__, __LINE__)
#define LOG_WARNING(msg) logger::instance().log(logger::Level::WARNING, msg, __FILE__, __LINE__)
#define LOG_ERROR(msg) logger::instance().log(logger::Level::ERROR, msg, __FILE__, __LINE__)

#endif // LOGGING_H
