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

#pragma once

#include "vstream_engine.h"
#include "mic_capture.h"
#include "audio_processor.h"
#include "benchmark_manager.h"
#include <hyni/hyni_websocket_server.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <chrono>

using json = nlohmann::json;

/**
 * @class vstream_app
 * @brief Main application class for the vstream speech recognition server
 *
 * This class encapsulates the entire vstream application, providing a clean
 * interface for initialization, configuration, and execution. It manages
 * the lifecycle of all components including the speech engine, WebSocket
 * server, microphone capture, and audio processing pipeline.
 *
 * Simplified version without VAD for better performance and reliability.
 */
class vstream_app {
public:
    /**
     * @struct config
     * @brief Configuration parameters for the vstream application
     */
    struct config {
        // Required parameters
        std::string model_path;                    ///< Path to Vosk model directory

        // Engine configuration
        std::string speaker_model_path;            ///< Path to speaker model (optional)
        std::string grammar;                       ///< JSON grammar specification
        int max_alternatives = 0;                  ///< Number of alternative results
        bool enable_partial_words = true;          ///< Enable partial word results
        int sample_rate = 16000;                   ///< Audio sample rate

        // Server configuration
        uint16_t port = 8080;                      ///< WebSocket server port
        int log_level = 0;                         ///< Vosk log level

        // Audio processing configuration
        int buffer_ms = 100;                       ///< Audio buffer size in milliseconds
        int finalize_ms = 2000;                    ///< Force finalization interval

        // Microphone configuration
        bool use_mic = false;                      ///< Enable microphone capture
        int mic_device = -1;                       ///< Microphone device index (-1 = default)

        // Benchmark configuration
        bool benchmark_enabled = false;            ///< Enable benchmarking
        bool benchmark_live = false;               ///< Live benchmarking without reference
        std::string benchmark_reference_file;      ///< Path to reference text file
        std::string benchmark_output_file;         ///< Output file for results
        std::string benchmark_format = "txt";

        /**
         * @brief Default constructor with sensible defaults
         */
        config() = default;
    };

    /**
     * @brief Construct vstream application with configuration
     */
    explicit vstream_app(const config& cfg);

    /**
     * @brief Destructor - ensures clean shutdown
     */
    ~vstream_app();

    /**
     * @brief Run the application main loop
     */
    int run();

    /**
     * @brief Stop the application gracefully
     */
    void stop();

    /**
     * @brief Check if the application is currently running
     */
    bool is_running() const { return m_running.load(); }

    /**
     * @brief Get application statistics
     */
    json get_stats() const;

    /**
     * @brief Parse command line arguments
     */
    static config parse_command_line(int argc, char* argv[]);

    /**
     * @brief Print usage information
     */
    static void print_usage(const char* program_name);

    /**
     * @brief Validate configuration parameters
     */
    static void validate_config(const config& cfg);

private:
    config m_config;                                          ///< Application configuration
    std::atomic<bool> m_running{false};                       ///< Running state flag

    // Core components
    std::unique_ptr<vstream_engine> m_engine;                 ///< Speech recognition engine
    std::unique_ptr<hyni_websocket_server> m_server;          ///< WebSocket server

    // Microphone components (optional)
    std::unique_ptr<mic_capture> m_mic;                       ///< Microphone capture
    std::unique_ptr<audio_processor> m_processor;             ///< Audio processing pipeline
    std::unique_ptr<benchmark_manager> m_benchmark;

    // Statistics
    std::chrono::steady_clock::time_point m_start_time;       ///< Application start time
    std::atomic<size_t> m_messages_processed{0};              ///< WebSocket messages processed

    // Benchmarking
    std::string m_benchmark_reference_file;
    std::string m_benchmark_output_file;

    /**
     * @brief Initialize the speech recognition engine
     */
    void initialize_engine();

    /**
     * @brief Initialize the WebSocket server
     */
    void initialize_server();

    /**
     * @brief Initialize microphone capture (if enabled)
     */
    void initialize_microphone();

    /**
     * @brief Setup signal handlers
     */
    void setup_signal_handlers();

    /**
     * @brief Initialize benchmarking if enabled
     */
    void initialize_benchmark();

    /**
     * @brief Handle WebSocket audio callback
     */
    void handle_websocket_audio(const hyni_audio_data& audio,
                                websocket::stream<tcp::socket>* client_ws);

    /**
     * @brief Handle WebSocket command
     */
    json handle_websocket_command(const std::string& command,
                                  const json& params,
                                  websocket::stream<tcp::socket>* client_ws);

    /**
     * @brief Print periodic statistics
     */
    void print_periodic_stats();
};
