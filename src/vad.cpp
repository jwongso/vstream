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

#include "vad.h"
#include <fvad.h>
#include <stdexcept>
#include <cstring>
#include "logger.h"

webrtc_vad::webrtc_vad() : webrtc_vad(config{}) {}

webrtc_vad::webrtc_vad(const config& cfg) : m_config(cfg) {
    validate_config();

    // Create VAD instance
    m_vad = fvad_new();
    if (!m_vad) {
        throw std::runtime_error("Failed to create WebRTC VAD instance");
    }

    // Set sample rate
    if (fvad_set_sample_rate(m_vad, m_config.sample_rate) < 0) {
        fvad_free(m_vad);
        throw std::runtime_error("Invalid sample rate for WebRTC VAD");
    }

    // Set operating mode
    if (fvad_set_mode(m_vad, static_cast<int>(m_config.mode)) < 0) {
        fvad_free(m_vad);
        throw std::runtime_error("Failed to set WebRTC VAD mode");
    }

    // Calculate frame size
    m_frame_size = (m_config.sample_rate * m_config.frame_duration_ms) / 1000;

    LOG_INFO("WebRTC VAD initialized: sample_rate=" + std::to_string(m_config.sample_rate) +
             ", mode=" + std::to_string(static_cast<int>(m_config.mode)) +
             ", frame_size=" + std::to_string(m_frame_size));
}

webrtc_vad::~webrtc_vad() {
    if (m_vad) {
        fvad_free(m_vad);
    }
}

void webrtc_vad::validate_config() {
    // Validate sample rate
    if (m_config.sample_rate != 8000 &&
        m_config.sample_rate != 16000 &&
        m_config.sample_rate != 32000 &&
        m_config.sample_rate != 48000) {
        throw std::invalid_argument("Sample rate must be 8000, 16000, 32000, or 48000 Hz");
    }

    // Validate frame duration
    if (m_config.frame_duration_ms != 10 &&
        m_config.frame_duration_ms != 20 &&
        m_config.frame_duration_ms != 30) {
        throw std::invalid_argument("Frame duration must be 10, 20, or 30 ms");
    }
}

bool webrtc_vad::process_frame(const int16_t* samples, size_t num_samples) {
    if (num_samples != m_frame_size) {
        LOG_WARNING("WebRTC VAD: Invalid frame size " + std::to_string(num_samples) +
                    ", expected " + std::to_string(m_frame_size));
        return false;
    }

    int result = fvad_process(m_vad, samples, num_samples);

    if (result < 0) {
        LOG_ERROR("WebRTC VAD processing error: " + std::to_string(result));
        return false;
    }

    // result is 1 for speech, 0 for non-speech
    return result == 1;
}

bool webrtc_vad::process(const std::vector<int16_t>& audio) {
    // Add to buffer
    m_buffer.insert(m_buffer.end(), audio.begin(), audio.end());

    bool any_speech_detected = false;

    // Process complete frames
    while (m_buffer.size() >= m_frame_size) {
        bool is_speech = process_frame(m_buffer.data(), m_frame_size);

        if (is_speech) {
            any_speech_detected = true;
        }

        // Remove processed frame
        m_buffer.erase(m_buffer.begin(), m_buffer.begin() + m_frame_size);
    }

    // Update state
    if (any_speech_detected) {
        m_last_vad_result = true;
    }

    return m_last_vad_result;
}

void webrtc_vad::reset() {
    m_buffer.clear();
    m_last_vad_result = false;
    // Note: fvad doesn't have a reset function, but clearing the buffer is sufficient
}

void webrtc_vad::set_mode(Aggressiveness mode) {
    if (fvad_set_mode(m_vad, static_cast<int>(mode)) < 0) {
        throw std::runtime_error("Failed to set WebRTC VAD mode");
    }
    m_config.mode = mode;
}

vad_with_hangover::vad_with_hangover() : vad_with_hangover(config{}) {}

vad_with_hangover::vad_with_hangover(const config& cfg)
    : m_config(cfg), m_vad(cfg.vad_config) {
    reset();
}

bool vad_with_hangover::process(const std::vector<int16_t>& audio) {
    bool vad_result = m_vad.process(audio);
    auto now = std::chrono::steady_clock::now();

    if (vad_result) {
        m_speech_frame_count++;
        m_silence_frame_count = 0;
        m_last_speech = now;

        // Need enough consecutive speech frames to start
        int startup_frames = m_config.startup_ms / m_config.vad_config.frame_duration_ms;

        if (!m_is_speaking && m_speech_frame_count >= startup_frames) {
            m_is_speaking = true;
            m_speech_start = now;
            LOG_INFO("Speech started");
        }
    } else {
        m_silence_frame_count++;
        m_speech_frame_count = 0;

        if (m_is_speaking) {
            // Check if we're within hangover period
            auto silence_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        now - m_last_speech).count();

            if (silence_duration > m_config.hangover_ms) {
                m_is_speaking = false;
                m_speech_end = now;
                LOG_INFO("Speech ended after " +
                         std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            m_speech_end - m_speech_start).count()) + "ms");
            }
        }
    }

    return m_is_speaking;
}

void vad_with_hangover::reset() {
    m_vad.reset();
    m_is_speaking = false;
    m_speech_frame_count = 0;
    m_silence_frame_count = 0;
}
