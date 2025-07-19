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

#include "audio_processor.h"
#include "vstream_engine.h"
#include "vad.h"
#include "benchmark_manager.h"
#include "logger.h"
#include <hyni/hyni_websocket_server.h>
#include <nlohmann/json.hpp>
#include <iostream>

using json = nlohmann::json;

audio_processor::audio_processor(vstream_engine* engine,
                                 hyni_websocket_server* server,
                                 vad_with_hangover* vad,
                                 int silence_frames_threshold,
                                 bool use_vad,
                                 int finalize_interval_ms,
                                 int buffer_ms,
                                 benchmark_manager* benchmark)
    : m_engine(engine)
    , m_server(server)
    , m_vad(vad)
    , m_session_id("mic-capture")
    , m_silence_frames_threshold(silence_frames_threshold)
    , m_use_vad(use_vad)
    , m_finalize_interval_ms(finalize_interval_ms)
    , m_buffer_ms(buffer_ms)
    , m_last_finalize_time(std::chrono::steady_clock::now())
    , m_last_debug_time(std::chrono::steady_clock::now())
    , m_benchmark(benchmark) {

    m_show_partial = m_engine->has_partial_enabled();

    // Pre-allocate string buffer
    m_result_buffer.reserve(1024);
    m_last_final_text.reserve(256);
    m_last_partial_text.reserve(256);

    if (m_use_vad) {
        LOG_INFO("audio_processor initialized with VAD (silence threshold: " +
                 std::to_string(m_silence_frames_threshold) + " frames = " +
                 std::to_string(m_silence_frames_threshold * m_buffer_ms) + "ms)");
    } else {
        LOG_INFO("audio_processor initialized without VAD (finalize every " +
                 std::to_string(m_finalize_interval_ms) + "ms)");
    }
}

void audio_processor::process_audio(const std::vector<int16_t>& audio) {
    // Check if this is speech (always true if VAD is disabled)
    bool is_speech = m_use_vad ? m_vad->process(audio) : true;

    auto now = std::chrono::steady_clock::now();

    // Update debug status periodically
    if (m_use_vad) {
        update_debug_status();
    }

    m_accumulated_audio_samples += audio.size();

    if (is_speech) {
        m_was_speaking = true;
        m_silence_frames = 0;

        // Process with Vosk
        m_result_buffer = m_engine->process_audio(audio);
        handle_speech_result(m_result_buffer);

        // Add periodic finalization even during speech for more frequent results
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now - m_last_finalize_time).count();

        if (elapsed >= m_finalize_interval_ms) {
            LOG_INFO("Periodic finalization during speech after " + std::to_string(elapsed) + "ms");
            force_finalize();
            m_last_finalize_time = now;
        }
    } else {
        // Silence detected (only happens when VAD is enabled)
        if (m_was_speaking) {
            m_silence_frames++;

            // More verbose logging for silence frame counting
            if (m_silence_frames % 2 == 0) {  // Log every other frame
                LOG_DEBUG("Silence frame count: " + std::to_string(m_silence_frames) +
                          "/" + std::to_string(m_silence_frames_threshold) +
                          " (" + std::to_string(m_silence_frames * m_buffer_ms) +
                          "ms/" + std::to_string(m_silence_frames_threshold * m_buffer_ms) + "ms)");
            }

            if (m_silence_frames >= m_silence_frames_threshold) {
                LOG_INFO("Finalizing speech after " +
                         std::to_string(m_silence_frames * m_buffer_ms) + "ms of silence");
                force_finalize();
            }
        }

        // Show silence indicator only if showing partials and using VAD
        if (m_use_vad && m_show_partial && m_silence_frames % 10 == 0) {
            std::cout << "\r[Silence]                    " << std::flush;
        }
    }
}

void audio_processor::handle_speech_result(const std::string& result_json) {
    try {
        auto result = json::parse(result_json);

        // Handle final result
        if (result.contains("text") && !result["text"].is_null()) {
            std::string text = result["text"].get<std::string>();
            if (!text.empty() && text != m_last_final_text) {
                handle_final_result(text);
            }
        }
        // Handle partial result
        else if (m_show_partial && result.contains("partial") && !result["partial"].is_null()) {
            std::string partial = result["partial"].get<std::string>();
            if (!partial.empty() && partial != m_last_partial_text) {
                handle_partial_result(partial);
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Error parsing JSON: " + std::string(e.what()));
    }
}

void audio_processor::handle_final_result(const std::string& text) {
    m_last_final_text = text;
    m_server->queue_transcription(text, m_session_id, 1.0f);
    LOG_INFO("[FINAL] Recognized: " + text);

    // Always show final results
    std::cout << "\n[FINAL] " << text << std::endl;

    if (m_benchmark) {
        auto processing_end = std::chrono::steady_clock::now();
        double latency_ms = std::chrono::duration<double, std::milli>(
                                processing_end - m_last_finalize_time).count();

        m_benchmark->add_transcription(text, "final", 1.0f,
                                       m_accumulated_audio_samples, latency_ms);
        m_accumulated_audio_samples = 0;  // Reset counter
    }

    // Reset finalize timer
    m_last_finalize_time = std::chrono::steady_clock::now();
}

void audio_processor::handle_partial_result(const std::string& partial) {
    m_last_partial_text = partial;
    LOG_DEBUG("[PARTIAL] " + partial);

    // Only show partial results if enabled
    std::cout << "\r[PARTIAL] " << partial << "...              " << std::flush;
}

void audio_processor::update_debug_status() {
    auto now = std::chrono::steady_clock::now();
    auto debug_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             now - m_last_debug_time).count();

    if (debug_elapsed >= 1000) {  // Log once per second
        bool is_speech = m_vad->process(std::vector<int16_t>{});  // Get current state
        LOG_DEBUG("VAD status: " + std::string(is_speech ? "SPEECH" : "SILENCE") +
                  ", silence frames: " + std::to_string(m_silence_frames) +
                  ", threshold: " + std::to_string(m_silence_frames_threshold));
        m_last_debug_time = now;
    }
}

void audio_processor::force_finalize() {
    // Force final result
    m_result_buffer = m_engine->process_audio({}, true);

    try {
        auto result = json::parse(m_result_buffer);
        if (result.contains("text") && !result["text"].is_null()) {
            std::string text = result["text"].get<std::string>();
            if (!text.empty() && text != m_last_final_text) {
                handle_final_result(text);
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Error parsing final JSON: " + std::string(e.what()));
    }

    // Reset recognizer for next utterance
    m_engine->reset();
    m_was_speaking = false;
    m_silence_frames = 0;
    m_last_partial_text.clear();

    // Reset finalize timer
    m_last_finalize_time = std::chrono::steady_clock::now();
}
