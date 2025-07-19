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

#include <string>
#include <vector>
#include <chrono>
#include <functional>
#include <atomic>
#include <fstream>

/**
 * @class benchmark_manager
 * @brief Comprehensive benchmarking system for vstream (Vosk-based ASR)
 *
 * Provides detailed performance analysis including:
 * - Word Error Rate (WER) and Character Error Rate (CER)
 * - Real-time factor and latency metrics
 * - Confidence scoring analysis
 * - Voice Activity Detection performance
 * - Detailed segment-by-segment analysis
 */
class benchmark_manager {
public:
    /**
     * @struct transcription_segment
     * @brief Individual transcription segment with timing and quality metrics
     */
    struct transcription_segment {
        std::string text;                                           ///< Transcribed text
        std::string type;                                           ///< "partial" or "final"
        std::chrono::steady_clock::time_point start_time;           ///< Segment start time
        std::chrono::steady_clock::time_point end_time;             ///< Segment end time
        double confidence = 1.0;                                    ///< Confidence score (0-1)
        size_t audio_samples = 0;                                   ///< Audio samples processed
        double processing_latency_ms = 0.0;                         ///< Processing latency
        bool vad_detected = false;                                  ///< VAD speech detection
        int silence_frames_before = 0;                              ///< Silence frames before speech
    };

    /**
     * @struct benchmark_results
     * @brief Complete benchmark results with all metrics
     */
    struct benchmark_results {
        // Text comparison
        std::string reference_text;
        std::string hypothesis_text;

        // Accuracy metrics
        double word_error_rate = 0.0;
        double character_error_rate = 0.0;
        int total_words = 0;
        int word_errors = 0;
        int word_substitutions = 0;
        int word_deletions = 0;
        int word_insertions = 0;

        // Timing metrics
        double total_audio_duration_ms = 0.0;
        double total_processing_time_ms = 0.0;
        double real_time_factor = 0.0;
        double average_latency_ms = 0.0;
        double min_latency_ms = 0.0;
        double max_latency_ms = 0.0;

        // Throughput metrics
        size_t total_samples_processed = 0;
        size_t total_segments = 0;
        double samples_per_second = 0.0;

        // Quality metrics
        double average_confidence = 0.0;
        double min_confidence = 1.0;
        double max_confidence = 0.0;

        // VAD-specific metrics
        double vad_accuracy = 0.0;                                  ///< VAD accuracy vs ground truth
        int vad_false_positives = 0;                                ///< False speech detections
        int vad_false_negatives = 0;                                ///< Missed speech segments
        double average_silence_before_speech_ms = 0.0;              ///< Silence before speech detection

        // Vosk-specific metrics
        int partial_segments = 0;                                   ///< Number of partial results
        int final_segments = 0;                                     ///< Number of final results
        double partial_to_final_ratio = 0.0;                       ///< Ratio of partial to final

        // Detailed segment data
        std::vector<transcription_segment> segments;
    };

    /**
     * @brief Progress callback function type
     */
    using progress_callback_t = std::function<void(const benchmark_results&)>;

    /**
     * @brief Constructor
     */
    benchmark_manager();

    /**
     * @brief Destructor
     */
    ~benchmark_manager();

    /**
     * @brief Set reference text for accuracy calculation
     * @param text Reference transcription
     */
    void set_reference_text(const std::string& text);

    /**
     * @brief Set ground truth VAD labels for VAD performance analysis
     * @param labels Vector of VAD labels (true = speech, false = silence)
     * @param frame_duration_ms Duration of each VAD frame in milliseconds
     */
    void set_vad_ground_truth(const std::vector<bool>& labels, double frame_duration_ms = 20.0);

    /**
     * @brief Start benchmarking session
     */
    void start();

    /**
     * @brief Stop benchmarking and return final results
     * @return Complete benchmark results
     */
    benchmark_results stop();

    /**
     * @brief Add a transcription segment (from vstream engine)
     * @param text Transcribed text
     * @param type Segment type ("partial" or "final")
     * @param confidence Confidence score
     * @param audio_samples Number of audio samples processed
     * @param processing_latency_ms Processing latency in milliseconds
     */
    void add_transcription(const std::string& text,
                           const std::string& type,
                           double confidence = 1.0,
                           size_t audio_samples = 0,
                           double processing_latency_ms = 0.0);

    /**
     * @brief Add VAD decision for performance analysis
     * @param is_speech VAD decision
     * @param silence_frames_before Number of silence frames before this decision
     */
    void add_vad_decision(bool is_speech, int silence_frames_before = 0);

    /**
     * @brief Get current benchmark results (for live monitoring)
     * @return Current results
     */
    benchmark_results get_current_results() const;

    /**
     * @brief Set progress callback for live updates
     * @param callback Callback function
     */
    void set_progress_callback(progress_callback_t callback) { m_progress_callback = callback; }

    /**
     * @brief Export results to file
     * @param results Benchmark results
     * @param output_path Output file path
     * @param model_path Path to Vosk model (for metadata)
     * @param format Export format ("txt", "json", "csv")
     */
    void export_results(const benchmark_results& results,
                        const std::string& output_path,
                        const std::string& model_path = "",
                        const std::string& format = "txt") const;

    /**
     * @brief Export comparison between two benchmark results
     * @param vstream_results Results from vstream (Vosk)
     * @param wstream_results Results from wstream (Whisper)
     * @param output_path Output file path
     */
    static void export_comparison(const benchmark_results& vstream_results,
                                  const benchmark_results& wstream_results,
                                  const std::string& output_path);

    // Static utility methods
    static std::vector<std::string> tokenize(const std::string& text);
    static std::string normalize_text(const std::string& text);
    static double calculate_wer(const std::string& reference, const std::string& hypothesis,
                                int* substitutions = nullptr, int* deletions = nullptr, int* insertions = nullptr);
    static double calculate_cer(const std::string& reference, const std::string& hypothesis);

private:
    bool m_is_running;
    std::chrono::steady_clock::time_point m_start_time;
    std::chrono::steady_clock::time_point m_last_segment_time;

    std::string m_reference_text;
    std::vector<transcription_segment> m_segments;
    size_t m_total_samples;

    // VAD analysis
    std::vector<bool> m_vad_ground_truth;
    std::vector<bool> m_vad_decisions;
    double m_vad_frame_duration_ms;

    progress_callback_t m_progress_callback;

    // Helper methods
    static int levenshtein_distance(const std::vector<std::string>& ref,
                                    const std::vector<std::string>& hyp,
                                    int* subs = nullptr, int* dels = nullptr, int* ins = nullptr);

    void export_txt_format(const benchmark_results& results, std::ofstream& file, const std::string& model_path) const;
    void export_json_format(const benchmark_results& results, std::ofstream& file, const std::string& model_path) const;
    void export_csv_format(const benchmark_results& results, std::ofstream& file) const;
};
