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
#include <hyni/hyni_websocket_server.h>
#include <string>
#include <vector>
#include <chrono>
#include <cstdint>

class benchmark_manager;

/**
 * @class audio_processor
 * @brief Simplified audio processing pipeline for real-time speech recognition
 *
 * The audio_processor class provides a streamlined audio processing pipeline
 * using time-based finalization only (no VAD). This approach works better
 * for continuous formal speech where VAD can cause fragmentation.
 *
 * @par Key Features:
 * - **Time-based Processing**: Consistent finalization intervals
 * - **No VAD Complexity**: Eliminates VAD-related fragmentation issues
 * - **Optimized for Continuous Speech**: Better for formal presentations
 * - **Performance Monitoring**: Integrated benchmarking support
 * - **WebSocket Integration**: Direct broadcasting of transcription results
 *
 * @par Processing Pipeline:
 * ```
 * Audio Input → Engine Processing → Result Handling → WebSocket Broadcast
 *      ↓              ↓                 ↓                    ↓
 * Always Process  JSON Parsing    Deduplication      Client Delivery
 * ```
 *
 * @par Operating Mode:
 * - Processes all audio as speech
 * - Finalizes results at regular time intervals
 * - No silence detection or VAD complexity
 * - Consistent behavior and timing
 */
class audio_processor {
public:
    /**
     * @brief Constructs a simplified audio processor
     *
     * Initializes the audio processing pipeline without VAD complexity.
     * Uses time-based finalization only for consistent behavior.
     *
     * @param engine Pointer to initialized Vosk speech recognition engine
     * @param server Pointer to WebSocket server for broadcasting results
     * @param finalize_interval_ms Time interval for forced finalization (milliseconds)
     * @param buffer_ms Audio buffer size (for statistics and logging)
     * @param benchmark Optional benchmark manager for performance monitoring
     */
    audio_processor(vstream_engine* engine,
                    hyni_websocket_server* server,
                    int finalize_interval_ms = 2000,
                    int buffer_ms = 100,
                    benchmark_manager* benchmark = nullptr);

    /**
     * @brief Virtual destructor for proper cleanup
     */
    virtual ~audio_processor() = default;

    /**
     * @brief Processes audio data through the recognition pipeline
     *
     * Processes all audio as speech and handles time-based finalization.
     * Much simpler than VAD-based processing with consistent behavior.
     *
     * @param audio Vector of 16-bit PCM audio samples
     */
    virtual void process_audio(const std::vector<int16_t>& audio);

protected:  // Protected for testing
    /**
     * @brief Forces immediate finalization of the current recognition session
     */
    void force_finalize();

    /**
     * @brief Processes JSON response from the speech recognition engine
     * @param result_json JSON string response from Vosk
     */
    void handle_speech_result(const std::string& result_json);

    /**
     * @brief Processes and broadcasts final transcription results
     * @param text Final transcribed text
     */
    void handle_final_result(const std::string& text);

    /**
     * @brief Processes and displays partial transcription results
     * @param partial Partial transcribed text
     */
    void handle_partial_result(const std::string& partial);

    // Core components
    vstream_engine* m_engine;                    ///< Speech recognition engine
    hyni_websocket_server* m_server;             ///< WebSocket server
    std::string m_session_id;                    ///< Session identifier
    bool m_show_partial;                         ///< Show partial results flag

    // Timing configuration
    int m_finalize_interval_ms;                  ///< Finalization interval
    int m_buffer_ms;                             ///< Buffer size (for logging)

    // Timing state
    std::chrono::steady_clock::time_point m_last_finalize_time; ///< Last finalization time

    // Result caching (for deduplication)
    std::string m_last_final_text;               ///< Last final result
    std::string m_last_partial_text;             ///< Last partial result

    // Performance optimization
    std::string m_result_buffer;                 ///< Reusable string buffer

    // Benchmarking
    benchmark_manager* m_benchmark = nullptr;    ///< Performance monitoring
    size_t m_accumulated_audio_samples = 0;      ///< Sample counter
};
