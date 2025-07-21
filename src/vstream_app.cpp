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

#include "vstream_app.h"
#include "benchmark_manager.h"
#include "logger.h"
#include <vosk_api.h>
#include <iostream>
#include <csignal>
#include <thread>
#include <stdexcept>
#include <algorithm>
#include <fstream>
#include <sstream>

// Global pointer for signal handling
static vstream_app* g_app_instance = nullptr;

// Signal handler
void signal_handler(int signal) {
    LOG_INFO("Received signal " + std::to_string(signal) + ", shutting down...");
    if (g_app_instance) {
        g_app_instance->stop();
    }
}

vstream_app::vstream_app(const config& cfg) : m_config(cfg) {
    validate_config(m_config);

    // Initialize logger
    logger::instance().init(false, false);
    logger::instance().set_min_level(logger::Level::DEBUG);

    LOG_INFO("vstream application initializing...");

    // Set global instance for signal handling
    g_app_instance = this;

    // Record start time
    m_start_time = std::chrono::steady_clock::now();
}

vstream_app::~vstream_app() {
    stop();
    g_app_instance = nullptr;
}

int vstream_app::run() {
    try {
        LOG_INFO("vstream starting...");

        setup_signal_handlers();

        // Set Vosk log level
        vosk_set_log_level(m_config.log_level);

        LOG_INFO("vstream - Enhanced Vosk-based Speech Recognition Server");
        LOG_INFO("================================================");

        // Console banner
        std::cout << "vstream - Enhanced Vosk-based Speech Recognition Server\n";
        std::cout << "================================================\n";

        // Initialize components
        initialize_engine();
        initialize_server();

        if (m_config.benchmark_enabled) {
            initialize_benchmark();
        }

        if (m_config.use_mic) {
            initialize_microphone();
        }

        // Start server
        LOG_INFO("Starting WebSocket server on port " + std::to_string(m_config.port) + "...");
        std::cout << "Starting WebSocket server on port " << m_config.port << "...\n";

        try {
            m_server->start();
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to start WebSocket server: " + std::string(e.what()));
            return 1;
        }

        LOG_INFO("Server ready. Waiting for connections...");
        std::cout << "Server ready. Waiting for connections...\n\n";

        // Mark as running
        m_running = true;

        // Main application loop
        while (m_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            print_periodic_stats();
        }

        LOG_INFO("Shutting down...");

        // Stop benchmark and export results
        if (m_benchmark && m_config.benchmark_enabled) {
            LOG_INFO("Finalizing benchmark results...");
            auto results = m_benchmark->stop();

            std::string output_file = m_config.benchmark_output_file;
            if (output_file.empty()) {
                output_file = "benchmark_results_" +
                              std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                                 std::chrono::system_clock::now().time_since_epoch()).count()) + ".txt";
            }

            m_benchmark->export_results(results, output_file, m_config.model_path);

            // Print summary to console
            std::cout << "\n=== BENCHMARK SUMMARY ===\n";
            std::cout << "Word Error Rate: " << std::fixed << std::setprecision(2)
                      << results.word_error_rate << "%\n";
            std::cout << "Character Error Rate: " << results.character_error_rate << "%\n";
            std::cout << "Real-time Factor: " << results.real_time_factor << "x\n";
            std::cout << "Average Latency: " << results.average_latency_ms << " ms\n";
            std::cout << "Average Confidence: " << results.average_confidence << "\n";
            std::cout << "Results exported to: " << output_file << "\n";
        }

        // Cleanup microphone
        if (m_mic) {
            LOG_INFO("Stopping microphone capture...");
            m_mic->stop();
            m_mic.reset();
        }

        // Cleanup server
        LOG_INFO("Stopping server...");
        std::cout << "Stopping server...\n";
        m_server->stop();

        LOG_INFO("Server stopped successfully");
        std::cout << "Server stopped successfully.\n";

        return 0;

    } catch (const std::exception& e) {
        LOG_ERROR("Fatal error: " + std::string(e.what()));
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}

void vstream_app::stop() {
    LOG_INFO("Stop requested");
    m_running = false;
}

json vstream_app::get_stats() const {
    auto uptime = std::chrono::steady_clock::now() - m_start_time;

    json stats;
    stats["uptime_seconds"] = std::chrono::duration_cast<std::chrono::seconds>(uptime).count();
    stats["messages_processed"] = m_messages_processed.load();
    stats["running"] = m_running.load();

    if (m_engine) {
        stats["samples_processed"] = m_engine->get_total_samples_processed();
    }

    if (m_server) {
        stats["connected_clients"] = m_server->get_client_count();
    }

    if (m_mic) {
        stats["microphone_enabled"] = true;
        stats["dropped_frames"] = m_mic->get_dropped_frames();
    } else {
        stats["microphone_enabled"] = false;
    }

    // Add benchmark stats if enabled
    if (m_benchmark && m_config.benchmark_enabled) {
        auto benchmark_results = m_benchmark->get_current_results();
        stats["benchmark"] = {
            {"enabled", true},
            {"word_error_rate", benchmark_results.word_error_rate},
            {"character_error_rate", benchmark_results.character_error_rate},
            {"real_time_factor", benchmark_results.real_time_factor},
            {"average_confidence", benchmark_results.average_confidence},
            {"total_segments", benchmark_results.total_segments},
            {"partial_segments", benchmark_results.partial_segments},
            {"final_segments", benchmark_results.final_segments}
        };
    } else {
        stats["benchmark"] = {{"enabled", false}};
    }

    return stats;
}

vstream_app::config vstream_app::parse_command_line(int argc, char* argv[]) {
    config cfg;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--model" && i + 1 < argc) {
            cfg.model_path = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            cfg.port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--spk-model" && i + 1 < argc) {
            cfg.speaker_model_path = argv[++i];
        } else if (arg == "--alternatives" && i + 1 < argc) {
            cfg.max_alternatives = std::stoi(argv[++i]);
        } else if (arg == "--no-partial") {
            cfg.enable_partial_words = false;
        } else if (arg == "--grammar" && i + 1 < argc) {
            cfg.grammar = argv[++i];
        } else if (arg == "--log-level" && i + 1 < argc) {
            cfg.log_level = std::stoi(argv[++i]);
        } else if (arg == "--mic") {
            cfg.use_mic = true;
        } else if (arg == "--finalize-ms" && i + 1 < argc) {
            cfg.finalize_ms = std::stoi(argv[++i]);
        } else if (arg == "--mic-device" && i + 1 < argc) {
            cfg.mic_device = std::stoi(argv[++i]);
        } else if (arg == "--buffer-ms" && i + 1 < argc) {
            cfg.buffer_ms = std::stoi(argv[++i]);
        } else if (arg == "--benchmark" && i + 1 < argc) {
            cfg.benchmark_reference_file = argv[++i];
            cfg.benchmark_enabled = true;
        } else if (arg == "--benchmark-output" && i + 1 < argc) {
            cfg.benchmark_output_file = argv[++i];
        } else if (arg == "--benchmark-live") {
            cfg.benchmark_enabled = true;
            cfg.benchmark_live = true;
        } else if (arg == "--benchmark-format" && i + 1 < argc) {
            cfg.benchmark_format = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            // Help is handled by caller
            continue;
        } else if (arg == "--list-devices") {
            // List devices is handled by caller
            continue;
        } else {
            throw std::invalid_argument("Unknown argument: " + arg);
        }
    }

    return cfg;
}

void vstream_app::print_usage(const char* program_name) {
    std::cout << "vstream - Enhanced Vosk-based Speech Recognition Server\n"
              << "Usage: " << program_name << " [options]\n"
              << "Options:\n"
              << "  --model PATH       Path to Vosk model directory (required)\n"
              << "  --port PORT        WebSocket server port (default: 8080)\n"
              << "  --mic              Enable microphone capture\n"
              << "  --mic-device N     Specify microphone device index\n"
              << "  --buffer-ms MS     Audio buffer size in milliseconds (default: 100)\n"
              << "                     Lower = less latency, Higher = better efficiency\n"
              << "  --finalize-ms MS   Finalization interval in milliseconds (default: 2000)\n"
              << "                     Controls how often results are finalized\n"
              << "                     Lower = more frequent results, Higher = longer context\n"
              << "  --list-devices     List available audio input devices\n"
              << "  --spk-model PATH   Path to speaker model (optional)\n"
              << "  --alternatives N   Enable N-best results (default: 0)\n"
              << "  --no-partial       Disable partial results\n"
              << "  --grammar JSON     Set grammar as JSON array\n"
              << "  --log-level N      Set Vosk log level (default: 0)\n"
              << "\n"
              << "Benchmark Options:\n"
              << "  --benchmark FILE   Enable benchmarking with reference text file\n"
              << "  --benchmark-live   Enable live benchmarking (no reference file)\n"
              << "  --benchmark-output FILE  Output file for benchmark results\n"
              << "  --benchmark-format FMT   Output format: txt, json, csv (default: txt)\n"
              << "\n"
              << "  --help             Show this help message\n"
              << "\n"
              << "Examples:\n"
              << "  Fast response:     --buffer-ms 50 --finalize-ms 1000\n"
              << "  Balanced:          --buffer-ms 100 --finalize-ms 2000\n"
              << "  Long context:      --buffer-ms 200 --finalize-ms 5000\n"
              << "\n"
              << "Benchmark Examples:\n"
              << "  File benchmark:    --model model --benchmark reference.txt --mic\n"
              << "  Live benchmark:    --model model --benchmark-live --mic\n"
              << "  JSON output:       --benchmark ref.txt --benchmark-format json\n";
}

void vstream_app::validate_config(const config& cfg) {
    if (cfg.model_path.empty()) {
        throw std::invalid_argument("Model path is required");
    }

    if (cfg.port == 0) {
        throw std::invalid_argument("Invalid port number: " + std::to_string(cfg.port));
    }

    if (cfg.buffer_ms <= 0 || cfg.buffer_ms > 5000) {
        throw std::invalid_argument("Buffer size must be between 1 and 5000 ms");
    }

    if (cfg.finalize_ms <= 0 || cfg.finalize_ms > 30000) {
        throw std::invalid_argument("Finalize interval must be between 1 and 30000 ms");
    }

    if (cfg.max_alternatives < 0 || cfg.max_alternatives > 10) {
        throw std::invalid_argument("Max alternatives must be between 0 and 10");
    }

    if (cfg.sample_rate != 8000 && cfg.sample_rate != 16000 &&
        cfg.sample_rate != 32000 && cfg.sample_rate != 48000) {
        throw std::invalid_argument("Sample rate must be 8000, 16000, 32000, or 48000 Hz");
    }

    // Validate benchmark options
    if (cfg.benchmark_enabled && !cfg.benchmark_live && cfg.benchmark_reference_file.empty()) {
        throw std::invalid_argument("Benchmark enabled but no reference file specified");
    }

    if (!cfg.benchmark_format.empty()) {
        if (cfg.benchmark_format != "txt" && cfg.benchmark_format != "json" && cfg.benchmark_format != "csv") {
            throw std::invalid_argument("Invalid benchmark format. Must be: txt, json, or csv");
        }
    }
}

void vstream_app::initialize_engine() {
    LOG_INFO("Initializing Vosk engine with model: " + m_config.model_path);

    // Create engine configuration
    vstream_engine::config engine_config;
    engine_config.sample_rate = m_config.sample_rate;
    engine_config.enable_speaker_id = !m_config.speaker_model_path.empty();
    engine_config.speaker_model_path = m_config.speaker_model_path;
    engine_config.max_alternatives = m_config.max_alternatives;
    engine_config.enable_partial_words = m_config.enable_partial_words;

    m_engine = std::make_unique<vstream_engine>(m_config.model_path, engine_config);

    if (!m_config.grammar.empty()) {
        m_engine->set_grammar(m_config.grammar);
        LOG_INFO("Grammar set: " + m_config.grammar);
    }

    LOG_INFO("Vosk engine initialized successfully");
}

void vstream_app::initialize_server() {
    LOG_INFO("Initializing WebSocket server on port " + std::to_string(m_config.port));

    m_server = std::make_unique<hyni_websocket_server>(m_config.port);

    // Set audio callback
    m_server->set_audio_callback([this](const hyni_audio_data& audio,
                                        websocket::stream<tcp::socket>* client_ws) {
        handle_websocket_audio(audio, client_ws);
    });

    // Set command handler
    m_server->set_command_handler([this](const std::string& command,
                                         const json& params,
                                         websocket::stream<tcp::socket>* client_ws) -> json {
        return handle_websocket_command(command, params, client_ws);
    });

    LOG_INFO("WebSocket server initialized");
}

void vstream_app::initialize_benchmark() {
    LOG_INFO("Initializing benchmark manager...");

    m_benchmark = std::make_unique<benchmark_manager>();

    // Load reference text if provided
    if (!m_config.benchmark_reference_file.empty()) {
        std::ifstream ref_file(m_config.benchmark_reference_file);
        if (!ref_file.is_open()) {
            throw std::runtime_error("Cannot open benchmark reference file: " +
                                     m_config.benchmark_reference_file);
        }

        std::stringstream buffer;
        buffer << ref_file.rdbuf();
        std::string reference_text = buffer.str();

        m_benchmark->set_reference_text(reference_text);
        LOG_INFO("Benchmark reference text loaded (" +
                 std::to_string(reference_text.length()) + " characters)");

        std::cout << "Benchmark mode: Reference file loaded\n";
        std::cout << "Reference: " << m_config.benchmark_reference_file << "\n";
    } else if (m_config.benchmark_live) {
        LOG_INFO("Benchmark mode: Live benchmarking (no reference)");
        std::cout << "Benchmark mode: Live performance monitoring\n";
    }

    // Set up progress callback for live updates
    if (m_config.benchmark_live) {
        m_benchmark->set_progress_callback([](const benchmark_manager::benchmark_results& results) {
            static auto last_update = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();

            // Update every 5 seconds
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_update).count() >= 5) {
                std::cout << "\r[Live] Segments: " << results.total_segments
                          << " | Avg Confidence: " << std::fixed << std::setprecision(3)
                          << results.average_confidence
                          << " | RTF: " << std::setprecision(2) << results.real_time_factor << "x";
                if (!results.reference_text.empty()) {
                    std::cout << " | WER: " << results.word_error_rate << "%";
                }
                std::cout << "           " << std::flush;
                last_update = now;
            }
        });
    }

    m_benchmark->start();
    LOG_INFO("Benchmark manager initialized and started");
}

void vstream_app::initialize_microphone() {
    LOG_INFO("Setting up microphone capture...");
    std::cout << "Setting up microphone capture...\n";

    // Mic configuration
    mic_capture::config mic_cfg;
    mic_cfg.sample_rate = m_config.sample_rate;
    mic_cfg.device_index = m_config.mic_device;
    mic_cfg.frames_per_buffer = m_config.buffer_ms * 16;  // Convert ms to samples
    mic_cfg.accumulate_ms = m_config.buffer_ms;

    LOG_INFO("Microphone configuration: sample_rate=" + std::to_string(mic_cfg.sample_rate) +
             ", buffer_ms=" + std::to_string(m_config.buffer_ms));

    m_mic = std::make_unique<mic_capture>(mic_cfg);

    // Create simplified audio processor (no VAD)
    m_processor = std::make_unique<audio_processor>(
        m_engine.get(),
        m_server.get(),
        m_config.finalize_ms,
        m_config.buffer_ms,
        m_benchmark.get());

    // Set callback
    m_mic->set_audio_callback([this](const std::vector<int16_t>& audio) {
        if (m_processor && !audio.empty()) {
            m_processor->process_audio(audio);
        }
    });

    if (!m_mic->start()) {
        throw std::runtime_error("Failed to start microphone capture");
    }

    LOG_INFO("Microphone capture started successfully");

    // Log configuration summary
    LOG_INFO("Configuration summary:");
    LOG_INFO("  Buffer size: " + std::to_string(m_config.buffer_ms) + "ms");
    LOG_INFO("  Finalization interval: " + std::to_string(m_config.finalize_ms) + "ms");
    LOG_INFO("  Partial results: " + std::string(m_config.enable_partial_words ? "enabled" : "disabled"));
    LOG_INFO("  Benchmark enabled: " + std::string(m_config.benchmark_enabled ? "yes" : "no"));
}

void vstream_app::setup_signal_handlers() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    LOG_INFO("Signal handlers installed");
}

void vstream_app::handle_websocket_audio(const hyni_audio_data& audio,
                                         websocket::stream<tcp::socket>* /*client_ws*/) {
    auto processing_start = std::chrono::steady_clock::now();

    auto result_json = m_engine->process_audio(audio.samples);
    m_messages_processed++;

    auto processing_end = std::chrono::steady_clock::now();
    double processing_latency_ms = std::chrono::duration<double, std::milli>(
                                       processing_end - processing_start).count();

    try {
        auto result = json::parse(result_json);

        // Check for text in various fields
        std::string text;
        std::string type = "partial";
        float confidence = 1.0f;

        if (result.contains("text") && !result["text"].is_null()) {
            text = result["text"].get<std::string>();
            type = "final";
        } else if (result.contains("partial") && !result["partial"].is_null()) {
            text = result["partial"].get<std::string>();
            type = "partial";
        }

        // If alternatives are present, extract confidence
        if (result.contains("alternatives") && result["alternatives"].is_array()
            && !result["alternatives"].empty()) {
            auto& alt = result["alternatives"][0];
            if (alt.contains("confidence")) {
                confidence = alt["confidence"].get<float>();
            }
        }

        if (!text.empty()) {
            m_server->queue_transcription(text, audio.session_id, confidence);
            LOG_DEBUG("WebSocket transcription queued: " + text);

            // Add to benchmark if enabled
            if (m_benchmark && m_config.benchmark_enabled) {
                m_benchmark->add_transcription(text, type, confidence,
                                               audio.samples.size(), processing_latency_ms);
            }
        }

    } catch (const std::exception& e) {
        LOG_ERROR("Error processing WebSocket audio result: " + std::string(e.what()));
    }
}

json vstream_app::handle_websocket_command(const std::string& command,
                                           const json& params,
                                           websocket::stream<tcp::socket>* /*client_ws*/) {
    LOG_DEBUG("Received command: " + command);
    json response;
    response["command"] = command;

    if (command == "reset") {
        m_engine->reset();
        response["status"] = "ok";
        response["message"] = "Recognizer reset";
        LOG_INFO("Recognizer reset via command");
    } else if (command == "set_grammar") {
        if (params.contains("grammar")) {
            m_engine->set_grammar(params["grammar"].dump());
            response["status"] = "ok";
            response["message"] = "Grammar updated";
            LOG_INFO("Grammar updated via command");
        } else {
            response["status"] = "error";
            response["message"] = "Missing grammar parameter";
            LOG_WARNING("set_grammar command missing grammar parameter");
        }
    } else if (command == "stats") {
        response["status"] = "ok";
        response["stats"] = get_stats();
        LOG_DEBUG("Stats requested via command");
    } else if (command == "benchmark_results") {
        if (m_benchmark && m_config.benchmark_enabled) {
            auto benchmark_results = m_benchmark->get_current_results();
            response["status"] = "ok";
            response["benchmark"] = {
                {"word_error_rate", benchmark_results.word_error_rate},
                {"character_error_rate", benchmark_results.character_error_rate},
                {"real_time_factor", benchmark_results.real_time_factor},
                {"average_confidence", benchmark_results.average_confidence},
                {"total_segments", benchmark_results.total_segments},
                {"partial_segments", benchmark_results.partial_segments},
                {"final_segments", benchmark_results.final_segments}
            };
        } else {
            response["status"] = "error";
            response["message"] = "Benchmark not enabled";
        }
        LOG_DEBUG("Benchmark results requested via command");
    } else if (command == "stop") {
        stop();
        response["status"] = "ok";
        response["message"] = "Server stopping";
        LOG_INFO("Stop requested via command");
    } else {
        response["status"] = "error";
        response["message"] = "Unknown command";
        LOG_WARNING("Unknown command received: " + command);
    }

    return response;
}

void vstream_app::print_periodic_stats() {
    static auto last_stats = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();

    if (std::chrono::duration_cast<std::chrono::seconds>(now - last_stats).count() >= 30) {
        auto stats = get_stats();
        LOG_INFO("Stats: " + std::to_string(stats["connected_clients"].get<size_t>()) + " clients, " +
                 std::to_string(stats["messages_processed"].get<size_t>()) + " messages processed");

        // Print benchmark stats if enabled
        if (m_config.benchmark_enabled && stats.contains("benchmark") && stats["benchmark"]["enabled"]) {
            LOG_INFO("Benchmark: WER=" + std::to_string(stats["benchmark"]["word_error_rate"].get<double>()) +
                     "%, RTF=" + std::to_string(stats["benchmark"]["real_time_factor"].get<double>()) + "x");
        }

        last_stats = now;
    }
}
