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

/**
 * @file vad.h
 * @brief Voice Activity Detection (VAD) using WebRTC's implementation
 *
 * This module provides two levels of voice activity detection:
 * 1. Basic WebRTC VAD wrapper for frame-level speech detection
 * 2. Enhanced VAD with hangover and startup time for robust speech segmentation
 *
 * The WebRTC VAD is widely used in real-time communication and provides
 * excellent performance with low computational overhead. The enhanced version
 * adds temporal smoothing to handle typical speech patterns better.
 *
 * Key features:
 * - Multiple aggressiveness levels for different use cases
 * - Support for various sample rates (8kHz to 48kHz)
 * - Frame-based and stream-based processing
 * - Configurable hangover time to prevent speech cutoff
 * - Startup time to filter out brief noises
 *
 * @note Based on WebRTC's libfvad implementation
 * @see https://github.com/dpirch/libfvad for the underlying library
 *
 * @example
 * @code
 * // Basic usage
 * webrtc_vad::config cfg;
 * cfg.sample_rate = 16000;
 * cfg.mode = webrtc_vad::Aggressiveness::LOW_BITRATE;
 *
 * webrtc_vad vad(cfg);
 * bool is_speech = vad.process(audio_chunk);
 *
 * // With hangover for better speech segmentation
 * vad_with_hangover::config enhanced_cfg;
 * enhanced_cfg.hangover_ms = 300;
 * vad_with_hangover enhanced_vad(enhanced_cfg);
 * bool speaking = enhanced_vad.process(audio_chunk);
 * @endcode
 */

#pragma once

#include <vector>
#include <memory>
#include <cstdint>
#include <chrono>

// Forward declaration
struct Fvad;

/**
 * @class webrtc_vad
 * @brief WebRTC Voice Activity Detection wrapper
 *
 * This class provides a C++ interface to WebRTC's Voice Activity Detection
 * algorithm. VAD is used to distinguish speech from non-speech segments in
 * audio streams, which is crucial for:
 * - Speech recognition (skip processing silence)
 * - Audio compression (reduce bitrate during silence)
 * - Echo cancellation (detect when to apply)
 * - Recording optimization (pause during silence)
 *
 * The WebRTC VAD uses a Gaussian Mixture Model (GMM) based approach that
 * analyzes audio features to classify frames as speech or non-speech.
 *
 * @note Thread-safe: Each instance maintains its own state
 * @note Real-time capable: Low latency, suitable for live audio
 */
class webrtc_vad {
public:
    /**
     * @enum Aggressiveness
     * @brief VAD aggressiveness levels controlling speech/silence trade-off
     *
     * Higher aggressiveness levels are more likely to classify frames as
     * non-speech, which can help in noisy environments but may clip the
     * beginning or end of utterances.
     */
    enum class Aggressiveness {
        /**
         * @brief Least aggressive mode - best for high-quality audio
         *
         * - Minimizes false rejections of speech
         * - May include more background noise
         * - Best for clean recording environments
         */
        QUALITY = 0,

        /**
         * @brief Balanced mode - good for most applications
         *
         * - Good balance between speech detection and noise rejection
         * - Recommended for general ASR applications
         * - Works well with typical microphone input
         */
        LOW_BITRATE = 1,

        /**
         * @brief Aggressive mode - for noisy environments
         *
         * - More likely to reject noise as non-speech
         * - May occasionally clip speech starts/ends
         * - Good for environments with consistent background noise
         */
        AGGRESSIVE = 2,

        /**
         * @brief Most aggressive mode - maximum noise rejection
         *
         * - Highest noise rejection
         * - Higher risk of clipping speech
         * - Use only in very noisy environments
         */
        VERY_AGGRESSIVE = 3
    };

    /**
     * @struct config
     * @brief Configuration parameters for WebRTC VAD
     *
     * All parameters must be within specific ranges supported by WebRTC VAD.
     * Invalid configurations will throw exceptions during construction.
     */
    struct config {
        /**
         * @brief Audio sample rate in Hz
         *
         * Must be one of: 8000, 16000, 32000, or 48000 Hz
         *
         * @note 16000 Hz is recommended for speech recognition
         * @note Must match the actual audio sample rate
         */
        int sample_rate = 16000;

        /**
         * @brief VAD aggressiveness mode
         *
         * Controls the trade-off between speech detection sensitivity
         * and noise rejection. See Aggressiveness enum for details.
         *
         * @note LOW_BITRATE is recommended for most ASR applications
         */
        Aggressiveness mode = Aggressiveness::LOW_BITRATE;

        /**
         * @brief Frame duration in milliseconds
         *
         * Must be one of: 10, 20, or 30 ms
         *
         * Determines the audio frame size for VAD processing:
         * - 10ms: 160 samples at 16kHz (lowest latency)
         * - 20ms: 320 samples at 16kHz (recommended)
         * - 30ms: 480 samples at 16kHz (highest efficiency)
         *
         * @note 20ms provides good balance between latency and efficiency
         */
        int frame_duration_ms = 20;

        /**
         * @brief Default constructor with recommended settings
         */
        config() = default;
    };

    /**
     * @brief Default constructor using default configuration
     *
     * Creates a VAD instance with:
     * - 16kHz sample rate
     * - LOW_BITRATE aggressiveness
     * - 20ms frame duration
     *
     * @throws std::runtime_error if VAD initialization fails
     */
    explicit webrtc_vad();

    /**
     * @brief Construct with custom configuration
     *
     * @param cfg Configuration parameters
     *
     * @throws std::invalid_argument if configuration is invalid
     * @throws std::runtime_error if VAD initialization fails
     *
     * @example
     * @code
     * webrtc_vad::config cfg;
     * cfg.sample_rate = 8000;  // Telephone quality
     * cfg.mode = webrtc_vad::Aggressiveness::AGGRESSIVE;
     * webrtc_vad vad(cfg);
     * @endcode
     */
    explicit webrtc_vad(const config& cfg);

    /**
     * @brief Destructor - releases WebRTC VAD resources
     */
    ~webrtc_vad();

    /**
     * @brief Process a single audio frame
     *
     * Processes exactly one frame of audio data. The frame size must match
     * the configured frame duration and sample rate.
     *
     * @param samples Pointer to audio samples (16-bit PCM)
     * @param num_samples Number of samples (must equal frame_size)
     *
     * @return true if speech is detected, false otherwise
     *
     * @throws std::invalid_argument if num_samples != frame_size
     *
     * @note Low-level API - consider using process() instead
     *
     * @example
     * @code
     * int16_t frame[320];  // 20ms at 16kHz
     * bool is_speech = vad.process_frame(frame, 320);
     * @endcode
     */
    bool process_frame(const int16_t* samples, size_t num_samples);

    /**
     * @brief Process variable-length audio with internal buffering
     *
     * Handles audio chunks of any size by maintaining an internal buffer.
     * Incomplete frames are buffered until enough samples are available.
     *
     * @param audio Vector of audio samples (16-bit PCM)
     *
     * @return true if speech is detected in the most recent complete frame
     *
     * @note Recommended API for stream processing
     * @note Returns last frame's result if multiple frames are processed
     *
     * @example
     * @code
     * // Process audio stream in chunks
     * while (audio_stream.is_active()) {
     *     auto chunk = audio_stream.read();
     *     if (vad.process(chunk)) {
     *         // Speech detected
     *     }
     * }
     * @endcode
     */
    bool process(const std::vector<int16_t>& audio);

    /**
     * @brief Get the required frame size in samples
     *
     * @return Number of samples per frame based on configuration
     *
     * @note Frame size = sample_rate * frame_duration_ms / 1000
     *
     * @example
     * @code
     * size_t frame_size = vad.get_frame_size();
     * // frame_size = 320 for 16kHz, 20ms configuration
     * @endcode
     */
    size_t get_frame_size() const { return m_frame_size; }

    /**
     * @brief Reset internal state and buffers
     *
     * Clears internal buffers and resets VAD state. Use between
     * independent audio streams or speakers.
     *
     * @note Does not change configuration
     */
    void reset();

    /**
     * @brief Change VAD aggressiveness mode
     *
     * Dynamically adjust the aggressiveness level without recreating
     * the VAD instance.
     *
     * @param mode New aggressiveness level
     *
     * @throws std::runtime_error if mode change fails
     *
     * @note Takes effect immediately on next frame
     */
    void set_mode(Aggressiveness mode);

private:
    /**
     * @brief Current configuration
     */
    config m_config;

    /**
     * @brief WebRTC VAD instance (opaque pointer)
     */
    Fvad* m_vad;

    /**
     * @brief Calculated frame size in samples
     */
    size_t m_frame_size;

    /**
     * @brief Internal buffer for incomplete frames
     */
    std::vector<int16_t> m_buffer;

    /**
     * @brief Last VAD result for stream processing
     */
    bool m_last_vad_result = false;

    /**
     * @brief Validate configuration parameters
     *
     * @throws std::invalid_argument if configuration is invalid
     */
    void validate_config();
};

/**
 * @class vad_with_hangover
 * @brief Enhanced VAD with temporal smoothing for robust speech detection
 *
 * This class extends basic VAD with temporal features that improve
 * speech segmentation for real-world applications:
 *
 * - **Hangover time**: Continues marking speech for a period after VAD
 *   reports silence, preventing premature cutoff of utterances
 * - **Startup time**: Requires sustained speech before transitioning to
 *   speaking state, filtering out brief noises
 *
 * These features are essential for ASR applications where:
 * - Natural speech has brief pauses between words
 * - Background noises can cause false triggers
 * - Complete utterances must be captured
 *
 * @note Adds latency equal to startup_ms for speech onset detection
 * @note Thread-safe: Each instance maintains its own state
 *
 * @see webrtc_vad for the underlying VAD implementation
 */
class vad_with_hangover {
public:
    /**
     * @struct config
     * @brief Configuration for enhanced VAD with temporal smoothing
     */
    struct config {
        /**
         * @brief Configuration for underlying WebRTC VAD
         *
         * @see webrtc_vad::config for parameter details
         */
        webrtc_vad::config vad_config;

        /**
         * @brief Hangover duration in milliseconds
         *
         * Time to continue reporting speech after VAD detects silence.
         * Prevents cutting off word endings and handles brief pauses.
         *
         * Typical values:
         * - 200-300ms: Natural speech flow
         * - 500ms: Conservative, ensures complete utterances
         * - 100ms: Aggressive, faster response
         *
         * @note Higher values increase latency but improve robustness
         */
        int hangover_ms = 300;

        /**
         * @brief Startup duration in milliseconds
         *
         * Minimum duration of continuous speech required to enter
         * speaking state. Filters out brief noises and clicks.
         *
         * Typical values:
         * - 50-100ms: Quick response, may trigger on short noises
         * - 200ms: Conservative, filters most non-speech sounds
         *
         * @note Lower values improve responsiveness but may increase false positives
         */
        int startup_ms = 100;

        /**
         * @brief Default constructor with recommended settings
         */
        config() = default;
    };

    /**
     * @brief Default constructor using default configuration
     *
     * Creates enhanced VAD with:
     * - 300ms hangover time
     * - 100ms startup time
     * - Default WebRTC VAD settings
     */
    explicit vad_with_hangover();

    /**
     * @brief Construct with custom configuration
     *
     * @param cfg Configuration parameters
     *
     * @throws std::invalid_argument if configuration is invalid
     * @throws std::runtime_error if initialization fails
     *
     * @example
     * @code
     * vad_with_hangover::config cfg;
     * cfg.hangover_ms = 500;  // Conservative hangover
     * cfg.startup_ms = 50;    // Quick startup
     * cfg.vad_config.mode = webrtc_vad::Aggressiveness::AGGRESSIVE;
     *
     * vad_with_hangover vad(cfg);
     * @endcode
     */
    explicit vad_with_hangover(const config& cfg);

    /**
     * @brief Process audio and get smoothed speech detection result
     *
     * Applies VAD with temporal smoothing. The result considers:
     * - Current VAD decision
     * - Recent speech history (startup time)
     * - Time since last speech (hangover time)
     *
     * @param audio Audio samples to process
     *
     * @return true if currently in speaking state, false otherwise
     *
     * @note State persists across calls - use reset() to clear
     *
     * @example
     * @code
     * vad_with_hangover vad;
     * bool was_speaking = false;
     *
     * while (capturing) {
     *     auto audio = get_audio_chunk();
     *     bool is_speaking = vad.process(audio);
     *
     *     if (is_speaking && !was_speaking) {
     *         // Speech started
     *         start_recording();
     *     } else if (!is_speaking && was_speaking) {
     *         // Speech ended (after hangover)
     *         stop_recording();
     *     }
     *
     *     was_speaking = is_speaking;
     * }
     * @endcode
     */
    bool process(const std::vector<int16_t>& audio);

    /**
     * @brief Reset all internal state
     *
     * Clears speech detection state and timing information.
     * Use between independent utterances or speakers.
     */
    void reset();

    /**
     * @brief Get current speaking state
     *
     * @return true if currently in speaking state
     *
     * @note Returns the last result from process()
     * @note Includes hangover time in the speaking state
     */
    bool is_speaking() const { return m_is_speaking; }

private:
    /**
     * @brief Configuration parameters
     */
    config m_config;

    /**
     * @brief Underlying WebRTC VAD instance
     */
    webrtc_vad m_vad;

    /**
     * @brief Current speaking state (includes smoothing)
     */
    bool m_is_speaking = false;

    /**
     * @brief Timestamp when speech started
     */
    std::chrono::steady_clock::time_point m_speech_start;

    /**
     * @brief Timestamp when speech ended
     */
    std::chrono::steady_clock::time_point m_speech_end;

    /**
     * @brief Timestamp of last detected speech frame
     */
    std::chrono::steady_clock::time_point m_last_speech;

    /**
     * @brief Consecutive frames with speech detected
     */
    int m_speech_frame_count = 0;

    /**
     * @brief Consecutive frames with silence detected
     */
    int m_silence_frame_count = 0;
};
