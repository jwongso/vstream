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

#include "benchmark_manager.h"
#include "logger.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <numeric>
#include <map>
#include <regex>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

benchmark_manager::benchmark_manager()
    : m_is_running(false)
    , m_total_samples(0)
    , m_vad_frame_duration_ms(20.0) {
    // Initialize timestamps to current time
    m_start_time = std::chrono::steady_clock::now();
    m_last_segment_time = m_start_time;
}

benchmark_manager::~benchmark_manager() {
    if (m_is_running) {
        stop();
    }
}

void benchmark_manager::set_reference_text(const std::string& text) {
    m_reference_text = normalize_text(text);
    LOG_INFO("Benchmark reference text set (" + std::to_string(m_reference_text.length()) + " characters)");
    std::cout << "[Benchmark] Reference text set ("
              << m_reference_text.length() << " characters)" << std::endl;
}

void benchmark_manager::set_vad_ground_truth(const std::vector<bool>& labels, double frame_duration_ms) {
    m_vad_ground_truth = labels;
    m_vad_frame_duration_ms = frame_duration_ms;
    LOG_INFO("VAD ground truth set (" + std::to_string(labels.size()) + " frames, " +
             std::to_string(frame_duration_ms) + "ms per frame)");
}

void benchmark_manager::start() {
    m_segments.clear();
    m_vad_decisions.clear();
    m_total_samples = 0;
    m_is_running = true;
    m_start_time = std::chrono::steady_clock::now();
    m_last_segment_time = m_start_time;

    LOG_INFO("Benchmark manager started");
    std::cout << "[Benchmark] Benchmark session started" << std::endl;
}

benchmark_manager::benchmark_results benchmark_manager::stop() {
    m_is_running = false;
    auto end_time = std::chrono::steady_clock::now();

    benchmark_results results = get_current_results();

    // Calculate final timing metrics
    results.total_processing_time_ms =
        std::chrono::duration<double, std::milli>(end_time - m_start_time).count();

    LOG_INFO("Benchmark completed - WER: " + std::to_string(results.word_error_rate) +
             "%, CER: " + std::to_string(results.character_error_rate) +
             "%, RTF: " + std::to_string(results.real_time_factor) + "x");

    std::cout << "\n[Benchmark] Session completed:" << std::endl;
    std::cout << "  WER: " << std::fixed << std::setprecision(2)
              << results.word_error_rate << "%" << std::endl;
    std::cout << "  CER: " << results.character_error_rate << "%" << std::endl;
    std::cout << "  RTF: " << results.real_time_factor << "x" << std::endl;
    std::cout << "  Avg Latency: " << results.average_latency_ms << " ms" << std::endl;
    std::cout << "  Avg Confidence: " << std::setprecision(3) << results.average_confidence << std::endl;

    return results;
}

void benchmark_manager::add_transcription(const std::string& text,
                                          const std::string& type,
                                          double confidence,
                                          size_t audio_samples,
                                          double processing_latency_ms) {
    if (!m_is_running) return;

    auto now = std::chrono::steady_clock::now();

    transcription_segment segment;
    segment.text = normalize_text(text);
    segment.type = type;
    segment.start_time = m_last_segment_time;
    segment.end_time = now;
    segment.confidence = confidence;
    segment.audio_samples = audio_samples;

    if (processing_latency_ms > 0) {
        segment.processing_latency_ms = processing_latency_ms;
    } else {
        // Fall back to time between calls (less accurate)
        segment.processing_latency_ms = std::chrono::duration<double, std::milli>(
                                            segment.end_time - segment.start_time).count();
    }

    m_segments.push_back(segment);
    m_total_samples += audio_samples;
    m_last_segment_time = now;

    // Log significant transcriptions
    if (type == "final" && !text.empty()) {
        LOG_DEBUG("Benchmark final transcription: " + text +
                  " (confidence: " + std::to_string(confidence) + ")");
    }

    // Call progress callback if set
    if (m_progress_callback) {
        m_progress_callback(get_current_results());
    }
}

void benchmark_manager::add_vad_decision(bool is_speech, int silence_frames_before) {
    if (!m_is_running) return;

    m_vad_decisions.push_back(is_speech);

    // Update VAD-specific data in the last segment if available
    if (!m_segments.empty()) {
        auto& last_segment = m_segments.back();
        last_segment.vad_detected = is_speech;
        last_segment.silence_frames_before = silence_frames_before;
    }
}

benchmark_manager::benchmark_results benchmark_manager::get_current_results() const {
    benchmark_results results;

    // Build hypothesis text from final segments only
    std::vector<std::string> final_texts;
    int partial_count = 0, final_count = 0;

    for (const auto& segment : m_segments) {
        if (segment.type == "final" && !segment.text.empty()) {
            final_texts.push_back(segment.text);
            final_count++;
        } else if (segment.type == "partial") {
            partial_count++;
        }
    }

    // Join final texts
    results.hypothesis_text.clear();
    for (size_t i = 0; i < final_texts.size(); ++i) {
        if (i > 0) results.hypothesis_text += " ";
        results.hypothesis_text += final_texts[i];
    }
    results.hypothesis_text = normalize_text(results.hypothesis_text);

    results.reference_text = m_reference_text;
    results.segments = m_segments;
    results.partial_segments = partial_count;
    results.final_segments = final_count;
    results.partial_to_final_ratio = final_count > 0 ? static_cast<double>(partial_count) / final_count : 0.0;

    // Calculate accuracy metrics
    if (!m_reference_text.empty() && !results.hypothesis_text.empty()) {
        results.word_error_rate = calculate_wer(
            m_reference_text,
            results.hypothesis_text,
            &results.word_substitutions,
            &results.word_deletions,
            &results.word_insertions
            );

        results.character_error_rate = calculate_cer(
            m_reference_text,
            results.hypothesis_text
            );

        auto ref_words = tokenize(m_reference_text);
        results.total_words = ref_words.size();
        results.word_errors = results.word_substitutions +
                              results.word_deletions +
                              results.word_insertions;
    }

    // Calculate timing metrics
    if (!m_segments.empty()) {
        std::vector<double> latencies;
        std::vector<double> confidences;
        std::vector<double> silence_before_speech;

        for (const auto& segment : m_segments) {
            if (segment.processing_latency_ms > 0) {
                latencies.push_back(segment.processing_latency_ms);
            }

            confidences.push_back(segment.confidence);

            if (segment.vad_detected && segment.silence_frames_before > 0) {
                silence_before_speech.push_back(segment.silence_frames_before * m_vad_frame_duration_ms);
            }
        }

        if (!latencies.empty()) {
            results.average_latency_ms = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
            auto minmax = std::minmax_element(latencies.begin(), latencies.end());
            results.min_latency_ms = *minmax.first;
            results.max_latency_ms = *minmax.second;
        }

        if (!confidences.empty()) {
            results.average_confidence = std::accumulate(confidences.begin(), confidences.end(), 0.0) / confidences.size();
            auto minmax = std::minmax_element(confidences.begin(), confidences.end());
            results.min_confidence = *minmax.first;
            results.max_confidence = *minmax.second;
        }

        if (!silence_before_speech.empty()) {
            results.average_silence_before_speech_ms =
                std::accumulate(silence_before_speech.begin(), silence_before_speech.end(), 0.0) / silence_before_speech.size();
        }
    }

    // Calculate VAD metrics
    if (!m_vad_ground_truth.empty() && !m_vad_decisions.empty()) {
        size_t min_size = std::min(m_vad_ground_truth.size(), m_vad_decisions.size());
        int correct = 0, false_positives = 0, false_negatives = 0;

        for (size_t i = 0; i < min_size; ++i) {
            bool ground_truth = m_vad_ground_truth[i];
            bool decision = m_vad_decisions[i];

            if (ground_truth == decision) {
                correct++;
            } else if (!ground_truth && decision) {
                false_positives++;
            } else if (ground_truth && !decision) {
                false_negatives++;
            }
        }

        results.vad_accuracy = (static_cast<double>(correct) / min_size) * 100.0;
        results.vad_false_positives = false_positives;
        results.vad_false_negatives = false_negatives;
    }

    // Calculate throughput metrics
    results.total_samples_processed = m_total_samples;
    results.total_segments = m_segments.size();

    if (m_is_running) {
        auto duration = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - m_start_time).count();
        if (duration > 0) {
            results.samples_per_second = m_total_samples / duration;
        }
    }

    // Calculate audio duration (assuming 16kHz sample rate)
    const double sample_rate = 16000.0;
    results.total_audio_duration_ms = (m_total_samples / sample_rate) * 1000.0;

    // Calculate real-time factor
    if (results.total_audio_duration_ms > 0) {
        if (m_is_running) {
            auto current_processing_time = std::chrono::duration<double, std::milli>(
                                               std::chrono::steady_clock::now() - m_start_time).count();
            results.real_time_factor = current_processing_time / results.total_audio_duration_ms;
        } else if (results.total_processing_time_ms > 0) {
            results.real_time_factor = results.total_processing_time_ms / results.total_audio_duration_ms;
        }
    }

    return results;
}

void benchmark_manager::export_results(const benchmark_results& results,
                                       const std::string& output_path,
                                       const std::string& model_path,
                                       const std::string& format) const {
    std::ofstream file(output_path);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open benchmark output file: " + output_path);
        std::cerr << "[Benchmark] Failed to open output file: " << output_path << std::endl;
        return;
    }

    if (format == "json") {
        export_json_format(results, file, model_path);
    } else if (format == "csv") {
        export_csv_format(results, file);
    } else {
        export_txt_format(results, file, model_path);
    }

    file.close();
    LOG_INFO("Benchmark results exported to: " + output_path);
    std::cout << "[Benchmark] Results exported to: " << output_path << std::endl;
}

void benchmark_manager::export_comparison(const benchmark_results& vstream_results,
                                          const benchmark_results& wstream_results,
                                          const std::string& output_path) {
    std::ofstream file(output_path);
    if (!file.is_open()) {
        std::cerr << "[Benchmark] Failed to open comparison output file: " << output_path << std::endl;
        return;
    }

    file << "=== VSTREAM vs WSTREAM COMPARISON ===" << std::endl;
    file << std::endl;

    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    file << "Generated: " << std::ctime(&now_time_t) << std::endl;

    file << std::left << std::setw(30) << "METRIC"
         << std::setw(15) << "VSTREAM (Vosk)"
         << std::setw(15) << "WSTREAM (Whisper)"
         << "WINNER" << std::endl;
    file << std::string(75, '-') << std::endl;

    // Helper lambda to compare and format results
    auto compare_metric = [&](const std::string& name, double vstream_val, double wstream_val,
                              bool lower_is_better = true, const std::string& unit = "") {
        bool vstream_wins = lower_is_better ? (vstream_val < wstream_val) : (vstream_val > wstream_val);
        file << std::left << std::setw(30) << name
             << std::setw(15) << (std::to_string(vstream_val) + unit)
             << std::setw(15) << (std::to_string(wstream_val) + unit)
             << (vstream_wins ? "VSTREAM" : "WSTREAM") << std::endl;
    };

    // Accuracy metrics (lower is better)
    compare_metric("Word Error Rate", vstream_results.word_error_rate, wstream_results.word_error_rate, true, "%");
    compare_metric("Character Error Rate", vstream_results.character_error_rate, wstream_results.character_error_rate, true, "%");

    // Performance metrics (lower is better for RTF and latency)
    compare_metric("Real-Time Factor", vstream_results.real_time_factor, wstream_results.real_time_factor, true, "x");
    compare_metric("Average Latency", vstream_results.average_latency_ms, wstream_results.average_latency_ms, true, "ms");

    // Quality metrics (higher is better)
    compare_metric("Average Confidence", vstream_results.average_confidence, wstream_results.average_confidence, false);

    file << std::endl;
    file << "DETAILED ANALYSIS:" << std::endl;
    file << "  VSTREAM processed " << vstream_results.total_segments << " segments" << std::endl;
    file << "  WSTREAM processed " << wstream_results.total_segments << " segments" << std::endl;
    file << "  VSTREAM partial/final ratio: " << std::fixed << std::setprecision(2)
         << vstream_results.partial_to_final_ratio << std::endl;

    // Calculate overall winner
    int vstream_wins = 0, wstream_wins = 0;
    if (vstream_results.word_error_rate < wstream_results.word_error_rate) vstream_wins++;
    else wstream_wins++;

    if (vstream_results.real_time_factor < wstream_results.real_time_factor) vstream_wins++;
    else wstream_wins++;

    if (vstream_results.average_confidence > wstream_results.average_confidence) vstream_wins++;
    else wstream_wins++;

    file << std::endl;
    file << "OVERALL WINNER: " << (vstream_wins > wstream_wins ? "VSTREAM" : "WSTREAM")
         << " (" << std::max(vstream_wins, wstream_wins) << "/3 metrics)" << std::endl;

    file.close();
    std::cout << "[Benchmark] Comparison exported to: " << output_path << std::endl;
}

// Static utility methods
std::vector<std::string> benchmark_manager::tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::istringstream stream(text);
    std::string word;

    while (stream >> word) {
        // Convert to lowercase and remove punctuation
        std::transform(word.begin(), word.end(), word.begin(), ::tolower);
        word.erase(std::remove_if(word.begin(), word.end(),
                                  [](char c) { return !std::isalnum(c); }),
                   word.end());

        if (!word.empty()) {
            tokens.push_back(word);
        }
    }

    return tokens;
}

std::string benchmark_manager::normalize_text(const std::string& text) {
    std::string normalized = text;

    // Convert to lowercase
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);

    // Remove extra whitespace
    auto new_end = std::unique(normalized.begin(), normalized.end(),
                               [](char a, char b) { return std::isspace(a) && std::isspace(b); });
    normalized.erase(new_end, normalized.end());

    // Trim leading/trailing whitespace
    normalized.erase(0, normalized.find_first_not_of(" \t\n\r"));
    normalized.erase(normalized.find_last_not_of(" \t\n\r") + 1);

    return normalized;
}

double benchmark_manager::calculate_wer(const std::string& reference,
                                        const std::string& hypothesis,
                                        int* substitutions,
                                        int* deletions,
                                        int* insertions) {
    auto ref_words = tokenize(reference);
    auto hyp_words = tokenize(hypothesis);

    if (ref_words.empty()) {
        return hyp_words.empty() ? 0.0 : 100.0;
    }

    int subs = 0, dels = 0, ins = 0;
    int distance = levenshtein_distance(ref_words, hyp_words, &subs, &dels, &ins);

    if (substitutions) *substitutions = subs;
    if (deletions) *deletions = dels;
    if (insertions) *insertions = ins;

    return (distance * 100.0) / ref_words.size();
}

double benchmark_manager::calculate_cer(const std::string& reference,
                                        const std::string& hypothesis) {
    if (reference.empty()) {
        return hypothesis.empty() ? 0.0 : 100.0;
    }

    // Convert strings to character vectors (excluding spaces)
    std::vector<std::string> ref_chars, hyp_chars;
    for (char c : reference) {
        if (!std::isspace(c)) {
            ref_chars.push_back(std::string(1, c));
        }
    }
    for (char c : hypothesis) {
        if (!std::isspace(c)) {
            hyp_chars.push_back(std::string(1, c));
        }
    }

    int distance = levenshtein_distance(ref_chars, hyp_chars, nullptr, nullptr, nullptr);
    return (distance * 100.0) / ref_chars.size();
}

// Private helper methods
int benchmark_manager::levenshtein_distance(const std::vector<std::string>& ref,
                                            const std::vector<std::string>& hyp,
                                            int* subs, int* dels, int* ins) {
    const size_t m = ref.size();
    const size_t n = hyp.size();

    // Create DP table
    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1, 0));

    // Initialize base cases
    for (size_t i = 0; i <= m; ++i) dp[i][0] = i;
    for (size_t j = 0; j <= n; ++j) dp[0][j] = j;

    // Fill DP table
    for (size_t i = 1; i <= m; ++i) {
        for (size_t j = 1; j <= n; ++j) {
            if (ref[i-1] == hyp[j-1]) {
                dp[i][j] = dp[i-1][j-1];
            } else {
                dp[i][j] = 1 + std::min({dp[i-1][j],    // deletion
                                         dp[i][j-1],      // insertion
                                         dp[i-1][j-1]});  // substitution
            }
        }
    }

    // Backtrack to count operation types
    if (subs != nullptr || dels != nullptr || ins != nullptr) {
        int sub_count = 0, del_count = 0, ins_count = 0;
        size_t i = m, j = n;

        while (i > 0 || j > 0) {
            if (i == 0) {
                ins_count++;
                j--;
            } else if (j == 0) {
                del_count++;
                i--;
            } else if (ref[i-1] == hyp[j-1]) {
                i--;
                j--;
            } else {
                int min_val = std::min({dp[i-1][j], dp[i][j-1], dp[i-1][j-1]});

                if (dp[i-1][j-1] == min_val) {
                    sub_count++;
                    i--;
                    j--;
                } else if (dp[i-1][j] == min_val) {
                    del_count++;
                    i--;
                } else {
                    ins_count++;
                    j--;
                }
            }
        }

        if (subs != nullptr) *subs = sub_count;
        if (dels != nullptr) *dels = del_count;
        if (ins != nullptr) *ins = ins_count;
    }

    return dp[m][n];
}

void benchmark_manager::export_txt_format(const benchmark_results& results,
                                          std::ofstream& file,
                                          const std::string& model_path) const {
    file << "=== VSTREAM BENCHMARK RESULTS ===" << std::endl;
    file << std::endl;

    // Add model information
    file << "MODEL INFORMATION:" << std::endl;
    if (!model_path.empty()) {
        file << "  Model: " << fs::path(model_path).filename().string() << std::endl;
        file << "  Full path: " << model_path << std::endl;

        // Get model directory size
        try {
            uintmax_t dir_size = 0;
            for (const auto& entry : fs::recursive_directory_iterator(model_path)) {
                if (entry.is_regular_file()) {
                    dir_size += entry.file_size();
                }
            }
            file << "  Size: " << std::fixed << std::setprecision(2)
                 << (dir_size / (1024.0 * 1024.0)) << " MB ("
                 << dir_size << " bytes)" << std::endl;
        } catch (...) {
            file << "  Size: Unknown" << std::endl;
        }
    }

    // Add timestamp
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    file << "  Timestamp: " << std::ctime(&now_time_t);
    file << std::endl;

    file << "ACCURACY METRICS:" << std::endl;
    file << "  Word Error Rate (WER): " << std::fixed << std::setprecision(2)
         << results.word_error_rate << "%" << std::endl;
    file << "  Character Error Rate (CER): " << results.character_error_rate << "%" << std::endl;
    file << "  Total Words: " << results.total_words << std::endl;
    file << "  Word Errors: " << results.word_errors << std::endl;
    file << "    Substitutions: " << results.word_substitutions << std::endl;
    file << "    Deletions: " << results.word_deletions << std::endl;
    file << "    Insertions: " << results.word_insertions << std::endl;
    file << std::endl;

    file << "TIMING METRICS:" << std::endl;
    file << "  Total Audio Duration: " << results.total_audio_duration_ms / 1000.0 << " s"
         << std::endl;
    file << "  Total Processing Time: " << results.total_processing_time_ms / 1000.0 << " s"
         << std::endl;
    file << "  Real-Time Factor: " << results.real_time_factor << "x" << std::endl;
    file << "  Average Latency: " << results.average_latency_ms << " ms" << std::endl;
    file << "  Min Latency: " << results.min_latency_ms << " ms" << std::endl;
    file << "  Max Latency: " << results.max_latency_ms << " ms" << std::endl;
    file << std::endl;

    file << "VOSK-SPECIFIC METRICS:" << std::endl;
    file << "  Partial Segments: " << results.partial_segments << std::endl;
    file << "  Final Segments: " << results.final_segments << std::endl;
    file << "  Partial/Final Ratio: " << std::fixed << std::setprecision(2)
         << results.partial_to_final_ratio << std::endl;
    file << std::endl;

    file << "QUALITY METRICS:" << std::endl;
    file << "  Average Confidence: " << std::fixed << std::setprecision(3)
         << results.average_confidence << std::endl;
    file << "  Min Confidence: " << results.min_confidence << std::endl;
    file << "  Max Confidence: " << results.max_confidence << std::endl;
    file << std::endl;

    if (results.vad_accuracy > 0) {
        file << "VAD METRICS:" << std::endl;
        file << "  VAD Accuracy: " << std::fixed << std::setprecision(2)
             << results.vad_accuracy << "%" << std::endl;
        file << "  False Positives: " << results.vad_false_positives << std::endl;
        file << "  False Negatives: " << results.vad_false_negatives << std::endl;
        file << "  Avg Silence Before Speech: " << results.average_silence_before_speech_ms
             << " ms" << std::endl;
        file << std::endl;
    }

    file << "THROUGHPUT METRICS:" << std::endl;
    file << "  Total Samples: " << results.total_samples_processed << std::endl;
    file << "  Total Segments: " << results.total_segments << std::endl;
    file << "  Samples/Second: " << std::fixed << std::setprecision(0)
         << results.samples_per_second << std::endl;
    file << std::endl;

    if (!results.reference_text.empty()) {
        file << "REFERENCE TEXT:" << std::endl;
        file << results.reference_text << std::endl;
        file << std::endl;
    }

    file << "HYPOTHESIS TEXT:" << std::endl;
    file << results.hypothesis_text << std::endl;
    file << std::endl;

    // Segment analysis
    if (results.total_segments > 0) {
        file << "SEGMENT ANALYSIS:" << std::endl;
        file << "  Average segment duration: " << std::fixed << std::setprecision(2)
             << (results.total_audio_duration_ms / results.total_segments) << " ms" << std::endl;
        file << "  Average processing time per segment: " << results.average_latency_ms << " ms" << std::endl;
        if (results.total_audio_duration_ms > 0) {
            file << "  Segment RTF: " << std::fixed << std::setprecision(2)
            << (results.average_latency_ms / (results.total_audio_duration_ms / results.total_segments))
            << "x" << std::endl;
        }
    }
}

void benchmark_manager::export_json_format(const benchmark_results& results,
                                           std::ofstream& file,
                                           const std::string& model_path) const {
    json output;

    // Metadata
    output["metadata"] = {
        {"model_path", model_path},
        {"timestamp", std::chrono::duration_cast<std::chrono::seconds>(
                          std::chrono::system_clock::now().time_since_epoch()).count()},
        {"engine", "vstream"},
        {"backend", "vosk"}
    };

    // Accuracy metrics
    output["accuracy"] = {
        {"word_error_rate", results.word_error_rate},
        {"character_error_rate", results.character_error_rate},
        {"total_words", results.total_words},
        {"word_errors", results.word_errors},
        {"word_substitutions", results.word_substitutions},
        {"word_deletions", results.word_deletions},
        {"word_insertions", results.word_insertions}
    };

    // Timing metrics
    output["timing"] = {
        {"total_audio_duration_ms", results.total_audio_duration_ms},
        {"total_processing_time_ms", results.total_processing_time_ms},
        {"real_time_factor", results.real_time_factor},
        {"average_latency_ms", results.average_latency_ms},
        {"min_latency_ms", results.min_latency_ms},
        {"max_latency_ms", results.max_latency_ms}
    };

    // Vosk-specific metrics
    output["vosk_metrics"] = {
        {"partial_segments", results.partial_segments},
        {"final_segments", results.final_segments},
        {"partial_to_final_ratio", results.partial_to_final_ratio}
    };

    // Quality metrics
    output["quality"] = {
        {"average_confidence", results.average_confidence},
        {"min_confidence", results.min_confidence},
        {"max_confidence", results.max_confidence}
    };

    // VAD metrics
    if (results.vad_accuracy > 0) {
        output["vad"] = {
            {"accuracy", results.vad_accuracy},
            {"false_positives", results.vad_false_positives},
            {"false_negatives", results.vad_false_negatives},
            {"average_silence_before_speech_ms", results.average_silence_before_speech_ms}
        };
    }

    // Text data
    output["text"] = {
        {"reference", results.reference_text},
        {"hypothesis", results.hypothesis_text}
    };

    // Throughput
    output["throughput"] = {
        {"total_samples", results.total_samples_processed},
        {"total_segments", results.total_segments},
        {"samples_per_second", results.samples_per_second}
    };

    file << output.dump(2);
}

void benchmark_manager::export_csv_format(const benchmark_results& results, std::ofstream& file) const {
    // CSV header
    file << "metric,value,unit\n";

    // Write all metrics as CSV rows
    file << "word_error_rate," << results.word_error_rate << ",percent\n";
    file << "character_error_rate," << results.character_error_rate << ",percent\n";
    file << "real_time_factor," << results.real_time_factor << ",ratio\n";
    file << "average_latency," << results.average_latency_ms << ",milliseconds\n";
    file << "average_confidence," << results.average_confidence << ",score\n";
    file << "total_segments," << results.total_segments << ",count\n";
    file << "partial_segments," << results.partial_segments << ",count\n";
    file << "final_segments," << results.final_segments << ",count\n";
    file << "total_words," << results.total_words << ",count\n";
    file << "word_errors," << results.word_errors << ",count\n";
    file << "samples_per_second," << results.samples_per_second << ",rate\n";

    if (results.vad_accuracy > 0) {
        file << "vad_accuracy," << results.vad_accuracy << ",percent\n";
        file << "vad_false_positives," << results.vad_false_positives << ",count\n";
        file << "vad_false_negatives," << results.vad_false_negatives << ",count\n";
    }
}
