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

#include "vstream_engine.h"
#include "logger.h"
#include <vosk_api.h>
#include <stdexcept>
#include <iostream>
#include <sstream>

// Constructor with default config
vstream_engine::vstream_engine(const std::string& model_path)
    : vstream_engine(model_path, config{}) {
}

// Constructor with custom config
vstream_engine::vstream_engine(const std::string& model_path, const config& cfg)
    : m_config(cfg) {

    // Set Vosk log level (0 = info/error, -1 = errors only)
    vosk_set_log_level(0);

    // Load main model
    m_model = vosk_model_new(model_path.c_str());
    if (!m_model) {
        throw std::runtime_error("Failed to load Vosk model from: " + model_path);
    }

    // Load speaker model if requested
    if (m_config.enable_speaker_id && !m_config.speaker_model_path.empty()) {
        m_spk_model = vosk_spk_model_new(m_config.speaker_model_path.c_str());
        if (!m_spk_model) {
            std::cerr << "Warning: Failed to load speaker model from: "
                      << m_config.speaker_model_path << std::endl;
            m_config.enable_speaker_id = false;
        }
    }

    initialize_recognizer();

    std::cout << "vstream engine initialized successfully" << std::endl;
    std::cout << "  Model: " << model_path << std::endl;
    std::cout << "  Sample rate: " << m_config.sample_rate << " Hz" << std::endl;
    std::cout << "  Speaker ID: " << (m_config.enable_speaker_id ? "enabled" : "disabled") << std::endl;
}

vstream_engine::~vstream_engine() {
    if (m_recognizer) {
        vosk_recognizer_free(m_recognizer);
    }
    if (m_spk_model) {
        vosk_spk_model_free(m_spk_model);
    }
    if (m_model) {
        vosk_model_free(m_model);
    }
}

void vstream_engine::initialize_recognizer() {
    // Create recognizer with or without speaker model
    if (m_config.enable_speaker_id && m_spk_model) {
        m_recognizer = vosk_recognizer_new_spk(m_model,
                                              static_cast<float>(m_config.sample_rate),
                                              m_spk_model);
    } else {
        m_recognizer = vosk_recognizer_new(m_model,
                                          static_cast<float>(m_config.sample_rate));
    }

    if (!m_recognizer) {
        throw std::runtime_error("Failed to create Vosk recognizer");
    }

    // Configure recognizer
    if (m_config.enable_word_times) {
        vosk_recognizer_set_words(m_recognizer, 1);
    }

    if (m_config.enable_partial_words) {
        vosk_recognizer_set_partial_words(m_recognizer, 1);
    } else {
        vosk_recognizer_set_partial_words(m_recognizer, 0);
    }

    if (m_config.max_alternatives > 0) {
        vosk_recognizer_set_max_alternatives(m_recognizer, m_config.max_alternatives);
    }
}

std::string vstream_engine::process_audio(const std::vector<int16_t>& audio_data, bool is_final) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (audio_data.empty() && !is_final) {
        return "{}";
    }

    m_total_samples += audio_data.size();

    static int process_count = 0;
    static bool just_got_final = false;  // Track if we just got a final result
    process_count++;

    if (!audio_data.empty()) {
        // If we just got a final result, skip some audio to avoid echoes
        if (just_got_final) {
            just_got_final = false;
            // Optionally reset the recognizer to ensure clean state
            vosk_recognizer_reset(m_recognizer);
            LOG_DEBUG("Reset recognizer after final result");
        }

        const size_t chunk_size = 1600; // 100ms at 16kHz
        size_t processed = 0;
        std::string last_result;

        while (processed < audio_data.size()) {
            size_t current_chunk = std::min(chunk_size, audio_data.size() - processed);

            int result = vosk_recognizer_accept_waveform_s(
                m_recognizer,
                audio_data.data() + processed,
                current_chunk
                );

            if (result > 0) {
                // Complete utterance
                std::string final_result = vosk_recognizer_result(m_recognizer);
                LOG_INFO("Vosk final result: " + logger::truncate_text(final_result, 200));
                just_got_final = true;  // Mark that we got a final result
                return final_result;
            } else if (result == 0) {
                last_result = vosk_recognizer_partial_result(m_recognizer);
            } else {
                LOG_ERROR("Vosk error processing audio, result code: " + std::to_string(result));
            }

            processed += current_chunk;
        }

        return last_result;
    }

    if (is_final) {
        std::string final_result = vosk_recognizer_final_result(m_recognizer);
        LOG_INFO("Vosk final result (forced): " + logger::truncate_text(final_result, 200));
        just_got_final = true;
        return final_result;
    }

    return "{}";
}

void vstream_engine::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    vosk_recognizer_reset(m_recognizer);
}

void vstream_engine::set_grammar(const std::string& grammar) {
    std::lock_guard<std::mutex> lock(m_mutex);
    vosk_recognizer_set_grm(m_recognizer, grammar.c_str());
}

void vstream_engine::set_max_alternatives(int max) {
    std::lock_guard<std::mutex> lock(m_mutex);
    vosk_recognizer_set_max_alternatives(m_recognizer, max);
}

void vstream_engine::enable_nlsml_output(bool enable) {
    std::lock_guard<std::mutex> lock(m_mutex);
    vosk_recognizer_set_nlsml(m_recognizer, enable ? 1 : 0);
}

bool vstream_engine::has_partial_result() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string partial = vosk_recognizer_partial_result(m_recognizer);
    return partial.find("\"partial\" : \"\"") == std::string::npos;
}
