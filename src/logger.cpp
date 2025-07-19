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

#include "logger.h"
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>
#include <mutex>

logger& logger::instance() noexcept {
    static logger instance;
    return instance;
}

logger::~logger() noexcept {
    shutdown();
}

void logger::init(bool enable_file_logging, bool enable_console_logging) noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);

    try {
        if (m_state) {
            // Clean up existing state first
            if (m_state->file_logging_enabled && m_state->log_file.is_open()) {
                m_state->log_file.close();
            }
        }

        m_state = std::make_unique<loggerState>();
        m_state->file_logging_enabled = enable_file_logging;
        m_state->console_logging_enabled = enable_console_logging;

        if (enable_file_logging) {
            m_state->log_file_name = generate_log_filename();
            m_state->log_file.open(m_state->log_file_name, std::ios::out | std::ios::app);

            if (!m_state->log_file.is_open()) {
                if (enable_console_logging) {
                    std::cerr << "Failed to open log file: " << m_state->log_file_name << std::endl;
                }
                m_state->file_logging_enabled = false;
            } else {
                // Write initial header
                m_state->log_file << "=== Logging started ===" << std::endl;
                m_state->log_file << "Log file: " << m_state->log_file_name << std::endl;
                m_state->log_file << "====" << std::endl << std::endl;
            }
        }
    } catch (...) {
        // Ensure we have some form of logging even if initialization partially fails
        if (m_state) {
            m_state->file_logging_enabled = false;
            m_state->console_logging_enabled = enable_console_logging;
        }
    }
}

bool logger::is_enabled() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state && (m_state->file_logging_enabled || m_state->console_logging_enabled);
}

std::string logger::generate_log_filename() noexcept {
    try {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);

        std::tm tm_buf;
        localtime_r(&in_time_t, &tm_buf);

        std::stringstream ss;
        ss << "hyni_log_"
           << std::put_time(&tm_buf, "%Y%m%d_%H%M%S")
           << ".log";
        return ss.str();
    } catch (...) {
        return "hyni_log_default.log";
    }
}

std::string logger::level_to_string(Level level) const noexcept {
    switch(level) {
    case Level::DEBUG:   return "DEBUG";
    case Level::INFO:    return "INFO";
    case Level::WARNING: return "WARNING";
    case Level::ERROR:   return "ERROR";
    default:             return "UNKNOWN";
    }
}

std::string logger::current_time() const noexcept {
    try {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);

        std::tm tm_buf;
        localtime_r(&in_time_t, &tm_buf);

        std::stringstream ss;
        ss << std::put_time(&tm_buf, "%Y-%m-%d %X");
        return ss.str();
    } catch (...) {
        return "[TIME_ERROR]";
    }
}

void logger::log(Level level, const std::string& message,
                 const std::string& file, int line) noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);

    try {
        if (!m_state || level < m_state->min_level) return;

        std::stringstream log_entry;
        log_entry << "[" << current_time() << "] "
                  << "[" << level_to_string(level) << "] ";

        if (!file.empty() && line != -1) {
            try {
                log_entry << "[" << std::filesystem::path(file).filename().string()
                         << ":" << line << "] ";
            } catch (...) {
                // If filesystem operations fail, just use the raw filename
                log_entry << "[" << file << ":" << line << "] ";
            }
        }

        log_entry << message;

        const std::string final_message = log_entry.str();

        if (m_state->console_logging_enabled) {
            try {
                std::cerr << final_message << std::endl;
            } catch (...) {
                // Console output failed, but continue with file logging if available
            }
        }

        if (m_state->file_logging_enabled && m_state->log_file.is_open()) {
            try {
                m_state->log_file << final_message << std::endl;
            } catch (...) {
                // File logging failed, disable it to prevent further errors
                m_state->file_logging_enabled = false;
                if (m_state->log_file.is_open()) {
                    m_state->log_file.close();
                }
            }
        }
    } catch (...) {
        // Last resort: try to output error to stderr if possible
        try {
            std::cerr << "[LOGGER_ERROR] Failed to log message" << std::endl;
        } catch (...) {
            // Nothing more we can do
        }
    }
}

void logger::log_section(const std::string& title,
                         const std::vector<std::string>& messages,
                         Level level) noexcept {
    try {
        if (!m_state || level < m_state->min_level) return;

        log(level, "\n==== " + title + " ====");
        for (const auto& msg : messages) {
            log(level, msg);
        }
        log(level, "=====================================");
    } catch (...) {
        // Error in section logging, try to log at least an error message
        log(Level::ERROR, "Failed to log section: " + title);
    }
}

void logger::set_min_level(Level level) noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        if (m_state) {
            m_state->min_level = level;
        }
    } catch (...) {
        // Ignore errors in setting log level
    }
}

std::string logger::get_log_file_name() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        return m_state ? m_state->log_file_name : "";
    } catch (...) {
        return "";
    }
}

void logger::flush() noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        if (m_state && m_state->file_logging_enabled && m_state->log_file.is_open()) {
            m_state->log_file.flush();
        }
    } catch (...) {
        // Ignore flush errors
    }
}

void logger::shutdown() noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        if (m_state) {
            if (m_state->file_logging_enabled && m_state->log_file.is_open()) {
                try {
                    m_state->log_file << std::endl << "=== Logging ended ===" << std::endl;
                    m_state->log_file.close();
                } catch (...) {
                    // Ignore errors during shutdown
                }
            }
            m_state.reset();
        }
    } catch (...) {
        // Ignore all errors during shutdown
    }
}

std::string logger::truncate_text(const std::string& text, size_t max_length) noexcept {
    try {
        return text.length() > max_length ? text.substr(0, max_length) + "..." : text;
    } catch (...) {
        return "[TEXT_ERROR]";
    }
}

std::string logger::get_json_keys(const nlohmann::json& j) noexcept {
    try {
        std::string keys;
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (!keys.empty()) keys += ", ";
            keys += it.key();
        }
        return keys.empty() ? "(none)" : keys;
    } catch (...) {
        return "[JSON_ERROR]";
    }
}
