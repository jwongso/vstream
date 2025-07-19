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
 * @file vstream_engine.h
 * @brief High-level speech recognition engine wrapper for Vosk API
 *
 * This module provides a thread-safe, feature-rich interface to the Vosk
 * speech recognition engine. It encapsulates Vosk's C API with modern C++
 * idioms and adds support for speaker identification, grammar constraints,
 * and various output formats.
 *
 * Key features:
 * - Thread-safe audio processing
 * - Speaker identification support
 * - Grammar-based recognition constraints
 * - N-best alternatives
 * - Word-level timing information
 * - Partial recognition results
 *
 * @note Requires Vosk model files to be downloaded separately
 * @note Thread-safe: All public methods are protected by mutex
 *
 * @see https://alphacephei.com/vosk/ for Vosk documentation
 * @see https://alphacephei.com/vosk/models for available models
 *
 * @example
 * @code
 * // Basic usage
 * vstream_engine engine("/path/to/vosk-model");
 *
 * // Process audio
 * std::vector<int16_t> audio_data = read_audio();
 * std::string result = engine.process_audio(audio_data);
 *
 * // Parse JSON result
 * auto json_result = nlohmann::json::parse(result);
 * std::string transcript = json_result["text"];
 * @endcode
 */

#pragma once

#include <string>
#include <memory>
#include <vector>
#include <mutex>
#include <atomic>

// Forward declarations
typedef struct VoskModel VoskModel;
typedef struct VoskSpkModel VoskSpkModel;
typedef struct VoskRecognizer VoskRecognizer;

/**
 * @class vstream_engine
 * @brief Enhanced Vosk-based speech recognition engine
 *
 * This class provides a high-level interface to the Vosk speech recognition
 * system with additional features for production use:
 *
 * - **Automatic resource management**: RAII for Vosk objects
 * - **Thread safety**: All methods are thread-safe
 * - **Flexible configuration**: Runtime-configurable parameters
 * - **Enhanced output**: Support for alternatives, timing, speaker ID
 * - **Grammar support**: Constrain recognition to specific phrases
 *
 * The engine processes 16-bit PCM audio and returns recognition results
 * in JSON format compatible with various ASR standards.
 *
 * @note Models must match the configured sample rate
 * @note Large models provide better accuracy but use more memory
 * @warning Model loading can take several seconds for large models
 */
class vstream_engine {
public:
    /**
     * @struct config
     * @brief Configuration parameters for the speech recognition engine
     *
     * This structure contains all configurable parameters that affect
     * recognition behavior. Default values are optimized for general
     * speech recognition tasks.
     */
    struct config {
        /**
         * @brief Audio sample rate in Hz
         *
         * Must match the sample rate of:
         * - The Vosk model
         * - The input audio data
         *
         * @note Common values: 8000 (telephone), 16000 (wideband), 48000 (HD)
         * @note Most Vosk models are trained for 16000 Hz
         */
        int sample_rate = 16000;

        /**
         * @brief Enable speaker identification/verification
         *
         * When enabled, recognition results will include speaker vectors
         * that can be used for speaker identification or verification.
         *
         * @note Requires a speaker model to be loaded
         * @note Adds computational overhead
         */
        bool enable_speaker_id = false;

        /**
         * @brief Enable word-level timing information
         *
         * When enabled, results include start/end timestamps for each word.
         * Useful for subtitle generation or word-level alignment.
         *
         * @note Slightly increases processing time
         * @note Times are relative to the start of the audio stream
         */
        bool enable_word_times = true;

        /**
         * @brief Enable partial word results
         *
         * When enabled, partial recognition results include incomplete words.
         * Useful for real-time display of recognition progress.
         *
         * @note Only affects partial results, not final results
         */
        bool enable_partial_words = true;

        /**
         * @brief Maximum number of alternative transcriptions
         *
         * Controls N-best list generation:
         * - 0: Disabled (fastest, only top result)
         * - 1-10: Return up to N alternatives with confidence scores
         *
         * @note Higher values increase processing time
         * @note Alternatives are ranked by likelihood
         */
        int max_alternatives = 0;

        /**
         * @brief Path to speaker identification model
         *
         * Required only if enable_speaker_id is true.
         * Points to a Vosk speaker model directory.
         *
         * @note Speaker models are separate from language models
         * @see https://alphacephei.com/vosk/models for speaker models
         */
        std::string speaker_model_path;

        /**
         * @brief Default constructor with sensible defaults
         */
        config() = default;
    };

    /**
     * @brief Construct engine with default configuration
     *
     * Creates a speech recognition engine with default settings.
     * Suitable for basic speech recognition tasks at 16kHz.
     *
     * @param model_path Path to Vosk model directory
     *
     * @throws std::runtime_error if model loading fails
     * @throws std::invalid_argument if model_path is invalid
     *
     * @note Model loading is performed synchronously and may take time
     *
     * @example
     * @code
     * vstream_engine engine("/opt/models/vosk-model-en-us-0.22");
     * @endcode
     */
    explicit vstream_engine(const std::string& model_path);

    /**
     * @brief Construct engine with custom configuration
     *
     * Creates a speech recognition engine with specified configuration.
     * Use this constructor for advanced features like speaker ID or
     * custom sample rates.
     *
     * @param model_path Path to Vosk model directory
     * @param cfg Configuration parameters
     *
     * @throws std::runtime_error if model loading fails
     * @throws std::invalid_argument if configuration is invalid
     *
     * @example
     * @code
     * vstream_engine::config cfg;
     * cfg.sample_rate = 8000;  // Telephone quality
     * cfg.max_alternatives = 3;  // Get top 3 results
     * cfg.enable_speaker_id = true;
     * cfg.speaker_model_path = "/opt/models/vosk-model-spk";
     *
     * vstream_engine engine("/opt/models/vosk-model-en-us-8khz", cfg);
     * @endcode
     */
    explicit vstream_engine(const std::string& model_path, const config& cfg);

    /**
     * @brief Destructor - releases all Vosk resources
     *
     * Ensures proper cleanup of Vosk models and recognizer.
     * Thread-safe and exception-safe cleanup.
     */
    ~vstream_engine();

    /**
     * @brief Process audio data and get recognition results
     *
     * Feeds audio data to the recognizer and returns results in JSON format.
     * This is the main method for speech recognition.
     *
     * @param audio_data Vector of 16-bit PCM samples
     * @param is_final Force final recognition (default: false)
     *
     * @return JSON string containing recognition results
     *
     * Result format depends on configuration and recognition state:
     * - Partial result: `{"partial": "hello world"}`
     * - Final result: `{"text": "hello world", "result": [...]}`
     * - With alternatives: `{"alternatives": [{"text": "...", "confidence": 0.9}]}`
     * - With speaker: `{"spk": [0.1, -0.2, ...], "spk_frames": 150}`
     *
     * @note Thread-safe - can be called from multiple threads
     * @note Empty audio vectors are valid (used for flushing)
     * @note Sample rate must match configuration
     *
     * @example
     * @code
     * // Continuous recognition
     * while (capturing) {
     *     auto audio = capture_audio_chunk();
     *     std::string result = engine.process_audio(audio);
     *
     *     auto json = nlohmann::json::parse(result);
     *     if (json.contains("text")) {
     *         std::cout << "Final: " << json["text"] << std::endl;
     *     } else if (json.contains("partial")) {
     *         std::cout << "Partial: " << json["partial"] << std::endl;
     *     }
     * }
     *
     * // Force final result
     * std::string final_result = engine.process_audio({}, true);
     * @endcode
     */
    std::string process_audio(const std::vector<int16_t>& audio_data, bool is_final = false);

    /**
     * @brief Reset the recognizer state
     *
     * Clears internal buffers and resets recognition context.
     * Use this between independent utterances to ensure clean recognition.
     *
     * @note Thread-safe
     * @note Does not affect configuration or loaded models
     *
     * @example
     * @code
     * // Reset between different speakers or sessions
     * engine.reset();
     * @endcode
     */
    void reset();

    /**
     * @brief Set grammar constraints for recognition
     *
     * Limits recognition to a specific set of phrases or patterns.
     * Useful for command recognition or structured input.
     *
     * @param grammar JSON array of allowed phrases or FST grammar
     *
     * Grammar formats:
     * - Simple phrase list: `["yes", "no", "maybe"]`
     * - Pattern with alternatives: `["[ok|okay] [computer|jarvis]"]`
     * - Empty string to remove grammar constraints
     *
     * @note Grammar significantly improves accuracy for limited vocabularies
     * @note Thread-safe
     *
     * @example
     * @code
     * // Simple command grammar
     * engine.set_grammar("[\"play music\", \"stop music\", \"next song\"]");
     *
     * // Remove grammar
     * engine.set_grammar("");
     * @endcode
     */
    void set_grammar(const std::string& grammar);

    /**
     * @brief Set maximum number of alternative results
     *
     * Dynamically change the number of alternative transcriptions returned.
     *
     * @param max Maximum alternatives (0 to disable)
     *
     * @note Thread-safe
     * @note Takes effect on next recognition
     * @see config::max_alternatives
     */
    void set_max_alternatives(int max);

    /**
     * @brief Enable NLSML (Natural Language Semantics Markup Language) output
     *
     * When enabled, results are formatted as NLSML XML instead of JSON.
     * Useful for compatibility with certain telephony systems.
     *
     * @param enable True to enable NLSML format
     *
     * @note Thread-safe
     * @note NLSML is less common than JSON output
     */
    void enable_nlsml_output(bool enable);

    /**
     * @brief Check if partial results are available
     *
     * Indicates whether the recognizer has accumulated enough audio
     * to produce partial results.
     *
     * @return true if partial results are available
     *
     * @note Thread-safe
     * @note Useful for optimizing partial result queries
     */
    bool has_partial_result() const;

    /**
     * @brief Get total number of audio samples processed
     *
     * Returns cumulative count of audio samples processed since construction.
     * Useful for statistics and performance monitoring.
     *
     * @return Total sample count
     *
     * @note Thread-safe atomic operation
     * @note Count persists across reset() calls
     */
    size_t get_total_samples_processed() const { return m_total_samples; }

    /**
     * @brief Get partial enabled configuration
     *
     * Returns partially enabled transcription config.
     *
     * @return true if partial enabled, false otherwise.
     */
    bool has_partial_enabled() const { return m_config.enable_partial_words; }

private:
    /**
     * @brief Vosk language model
     * @note Owned by this class, deleted in destructor
     */
    VoskModel* m_model = nullptr;

    /**
     * @brief Vosk speaker model (optional)
     * @note Only loaded if speaker ID is enabled
     */
    VoskSpkModel* m_spk_model = nullptr;

    /**
     * @brief Vosk recognizer instance
     * @note Recreated when configuration changes
     */
    VoskRecognizer* m_recognizer = nullptr;

    /**
     * @brief Current configuration
     */
    config m_config;

    /**
     * @brief Mutex for thread-safe operations
     * @note Protects all Vosk API calls
     */
    mutable std::mutex m_mutex;

    /**
     * @brief Atomic counter for processed samples
     * @note Updated without mutex for performance
     */
    std::atomic<size_t> m_total_samples{0};

    /**
     * @brief Initialize or reinitialize the recognizer
     *
     * Creates a new recognizer instance with current configuration.
     * Called during construction and when configuration changes.
     *
     * @throws std::runtime_error if initialization fails
     *
     * @note Must be called with mutex held
     */
    void initialize_recognizer();
};
