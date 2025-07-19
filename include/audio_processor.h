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
#include <cstdint>

class vstream_engine;
class hyni_websocket_server;
class vad_with_hangover;
class benchmark_manager;

/**
 * @class audio_processor
 * @brief High-performance audio processing pipeline for real-time speech recognition
 *
 * The audio_processor class serves as the central component in the vstream audio processing
 * pipeline, orchestrating the flow from raw audio input through voice activity detection,
 * speech recognition, and result broadcasting. It provides intelligent speech boundary
 * detection, automatic finalization, and optimized result handling.
 *
 * @par Key Features:
 * - **Real-time Processing**: Handles continuous audio streams with minimal latency
 * - **Voice Activity Detection**: Optional WebRTC-based VAD for speech boundary detection
 * - **Intelligent Finalization**: Automatic result finalization based on silence detection
 * - **Dual Processing Modes**: VAD-enabled and time-based processing modes
 * - **Result Optimization**: Deduplication and intelligent partial/final result handling
 * - **Performance Monitoring**: Integrated benchmarking and metrics collection
 * - **WebSocket Integration**: Direct broadcasting of transcription results
 *
 * @par Processing Pipeline:
 * ```
 * Audio Input → VAD Analysis → Speech Detection → Vosk Processing → Result Handling → WebSocket Broadcast
 *      ↓             ↓              ↓               ↓                ↓                    ↓
 * Level Check   Silence Count   Engine Feed    JSON Parsing    Deduplication      Client Delivery
 * ```
 *
 * @par Operating Modes:
 *
 * **1. VAD-Enabled Mode (Recommended):**
 * - Uses WebRTC VAD for accurate speech boundary detection
 * - Automatically finalizes results after configurable silence duration
 * - Provides optimal user experience with natural speech flow
 * - Suitable for interactive applications and live transcription
 *
 * **2. Time-Based Mode:**
 * - Processes all audio as potential speech
 * - Finalizes results at regular time intervals
 * - Suitable for continuous audio streams or noisy environments
 * - Higher CPU usage but more predictable behavior
 *
 * @par Performance Characteristics:
 * - **Latency**: Sub-100ms processing latency for typical configurations
 * - **Throughput**: Handles real-time audio streams up to 48kHz sample rate
 * - **Memory**: Pre-allocated string buffers minimize GC pressure
 * - **CPU**: Optimized VAD processing with configurable aggressiveness
 * - **Scalability**: Thread-safe design supports multiple concurrent instances
 *
 * @par Configuration Parameters:
 * - `silence_frames_threshold`: Number of silent frames before finalization (VAD mode)
 * - `finalize_interval_ms`: Maximum time between forced finalizations
 * - `buffer_ms`: Audio buffer size affecting latency vs. efficiency trade-off
 * - `use_vad`: Enable/disable voice activity detection
 *
 * @par Thread Safety:
 * This class is designed to be used from a single audio processing thread.
 * Internal WebSocket broadcasting and benchmarking operations are thread-safe,
 * but the main process_audio() method should not be called concurrently.
 *
 * @par Usage Example:
 * ```cpp
 * // Create processor with VAD enabled
 * auto processor = std::make_unique<audio_processor>(
 *     engine.get(),           // Vosk engine
 *     server.get(),           // WebSocket server
 *     vad.get(),              // VAD instance
 *     5,                      // 5 frames silence threshold (500ms at 100ms/frame)
 *     true,                   // Enable VAD
 *     2000,                   // Force finalize every 2 seconds
 *     100,                    // 100ms buffer size
 *     benchmark.get()         // Optional benchmarking
 * );
 *
 * // Process audio in real-time loop
 * while (capturing) {
 *     std::vector<int16_t> audio_buffer = capture_audio();
 *     processor->process_audio(audio_buffer);
 * }
 * ```
 *
 * @see vstream_engine For speech recognition engine interface
 * @see vad_with_hangover For voice activity detection implementation
 * @see hyni_websocket_server For result broadcasting
 * @see benchmark_manager For performance monitoring
 */
class audio_processor {
public:
    /**
     * @brief Constructs an audio processor with comprehensive configuration
     *
     * Initializes the audio processing pipeline with all necessary components
     * and configuration parameters. The processor will be ready to handle
     * audio immediately after construction.
     *
     * @param engine Pointer to initialized Vosk speech recognition engine
     *               Must remain valid for the lifetime of this processor
     * @param server Pointer to WebSocket server for broadcasting results
     *               Must remain valid for the lifetime of this processor
     * @param vad Pointer to voice activity detector instance
     *            Can be nullptr if use_vad is false
     * @param silence_frames_threshold Number of consecutive silence frames
     *                                required to trigger result finalization.
     *                                Only effective when use_vad is true.
     *                                Typical range: 1-10 frames
     * @param use_vad Enable voice activity detection for intelligent speech
     *                boundary detection. When false, all audio is processed
     *                as potential speech
     * @param finalize_interval_ms Maximum time in milliseconds between forced
     *                            result finalizations. Prevents excessively
     *                            long utterances. Range: 1000-30000ms
     * @param buffer_ms Size of audio buffers in milliseconds. Affects the
     *                  granularity of VAD decisions and processing latency.
     *                  Typical range: 50-200ms
     * @param benchmark Optional benchmark manager for performance monitoring.
     *                  Can be nullptr if benchmarking is not required
     *
     * @throws std::invalid_argument If engine or server pointers are null
     * @throws std::invalid_argument If vad is null but use_vad is true
     * @throws std::invalid_argument If timing parameters are out of valid range
     *
     * @note The processor automatically detects if the engine supports partial
     *       results and configures itself accordingly.
     *
     * @par Example:
     * ```cpp
     * auto processor = std::make_unique<audio_processor>(
     *     vosk_engine.get(),    // Required: speech engine
     *     ws_server.get(),      // Required: result broadcaster
     *     webrtc_vad.get(),     // Optional: VAD (required if use_vad=true)
     *     3,                    // 3 silence frames = 300ms at 100ms/frame
     *     true,                 // Enable VAD
     *     2500,                 // Force finalize every 2.5 seconds
     *     100,                  // 100ms audio buffers
     *     bench_mgr.get()       // Optional: performance monitoring
     * );
     * ```
     */
    audio_processor(vstream_engine* engine,
                    hyni_websocket_server* server,
                    vad_with_hangover* vad,
                    int silence_frames_threshold = 2,
                    bool use_vad = true,
                    int finalize_interval_ms = 2000,
                    int buffer_ms = 100,
                    benchmark_manager* benchmark = nullptr);

    /**
     * @brief Virtual destructor for proper cleanup
     *
     * Ensures proper cleanup of resources and allows for safe inheritance.
     * No explicit cleanup is required as all resources are managed by
     * the owning components.
     */
    virtual ~audio_processor() = default;

    /**
     * @brief Processes a buffer of audio data through the recognition pipeline
     *
     * This is the main entry point for audio processing. The method handles
     * the complete pipeline from VAD analysis through speech recognition to
     * result broadcasting. It maintains internal state for speech boundary
     * detection and result optimization.
     *
     * @param audio Vector of 16-bit PCM audio samples at the configured
     *              sample rate (typically 16kHz). The buffer should contain
     *              the number of samples corresponding to the configured
     *              buffer_ms duration. Empty buffers are ignored.
     *
     * @par Processing Flow:
     * 1. **Voice Activity Detection**: Analyzes audio for speech presence (if enabled)
     * 2. **Speech Boundary Tracking**: Maintains silence frame counters and speech state
     * 3. **Engine Processing**: Feeds audio to Vosk engine for recognition
     * 4. **Result Handling**: Processes JSON responses and extracts transcriptions
     * 5. **Finalization Logic**: Triggers result finalization based on silence or time
     * 6. **Broadcasting**: Sends results to connected WebSocket clients
     * 7. **Benchmarking**: Records performance metrics (if enabled)
     *
     * @par VAD-Enabled Mode Behavior:
     * - Speech detected: Processes audio, resets silence counter, shows partial results
     * - Silence detected: Increments silence counter, triggers finalization when threshold reached
     * - Periodic finalization: Forces finalization during long speech segments
     *
     * @par Time-Based Mode Behavior:
     * - All audio processed as potential speech
     * - Periodic finalization based solely on finalize_interval_ms
     * - No silence detection or VAD status display
     *
     * @par Performance Considerations:
     * - Method should be called from a single thread (not thread-safe)
     * - Optimized for real-time processing with minimal allocations
     * - Pre-allocated string buffers reduce GC pressure
     * - VAD processing adds ~1-2ms per buffer
     *
     * @par Error Handling:
     * - Invalid JSON responses are logged but don't interrupt processing
     * - Empty or malformed audio buffers are silently ignored
     * - Engine errors are propagated to the caller
     *
     * @warning This method is not thread-safe. Call from audio thread only.
     *
     * @see handle_speech_result For JSON response processing details
     * @see force_finalize For finalization behavior
     * @see update_debug_status For VAD status monitoring
     */
    virtual void process_audio(const std::vector<int16_t>& audio);

protected:  // Changed from private to protected for testing
    /**
     * @brief Forces immediate finalization of the current recognition session
     *
     * Triggers the speech engine to finalize any pending recognition results
     * and resets the processor state for the next utterance. This method is
     * called automatically when silence thresholds are exceeded or periodic
     * finalization intervals are reached.
     *
     * @par Finalization Process:
     * 1. Sends empty audio buffer with finalize flag to engine
     * 2. Processes final JSON response for complete transcription
     * 3. Broadcasts final result if non-empty and different from previous
     * 4. Resets engine recognizer state for next utterance
     * 5. Clears internal state (speech flags, silence counters, partial text)
     * 6. Updates finalization timestamp
     *
     * @par When Called:
     * - Silence threshold exceeded (VAD mode)
     * - Periodic finalization interval reached
     * - Long speech segment detected (prevent infinite utterances)
     * - Manual finalization requested
     *
     * @note This method is thread-safe and can be called from any thread
     *
     * @see handle_final_result For final result processing
     */
    void force_finalize();

    /**
     * @brief Processes JSON response from the speech recognition engine
     *
     * Parses the JSON response from Vosk and routes it to appropriate handlers
     * based on the content type (final results vs. partial results). Handles
     * result deduplication to avoid broadcasting identical transcriptions.
     *
     * @param result_json JSON string response from the Vosk engine containing
     *                    either final transcription ("text" field) or partial
     *                    results ("partial" field)
     *
     * @par JSON Format Examples:
     * **Final Result:**
     * ```json
     * {
     *   "text": "hello world",
     *   "result": [...],
     *   "alternatives": [{"text": "hello world", "confidence": 0.95}]
     * }
     * ```
     *
     * **Partial Result:**
     * ```json
     * {
     *   "partial": "hello wo..."
     * }
     * ```
     *
     * @par Processing Logic:
     * - Final results (non-empty "text" field) are sent to handle_final_result()
     * - Partial results (non-empty "partial" field) are sent to handle_partial_result()
     * - Results identical to previous ones are filtered out to reduce noise
     * - Malformed JSON is logged but doesn't interrupt processing
     *
     * @throws Does not throw - all exceptions are caught and logged
     *
     * @see handle_final_result For final transcription processing
     * @see handle_partial_result For partial transcription processing
     */
    void handle_speech_result(const std::string& result_json);

    /**
     * @brief Processes and broadcasts final transcription results
     *
     * Handles final transcription results from the speech engine, including
     * deduplication, WebSocket broadcasting, console output, and performance
     * metrics recording. This method is called when the engine produces a
     * complete transcription.
     *
     * @param text Final transcribed text from the speech recognition engine.
     *             Should be a complete, punctuated transcription of the spoken
     *             utterance
     *
     * @par Processing Steps:
     * 1. **Deduplication**: Compares with previous final result to avoid duplicates
     * 2. **State Update**: Updates internal final text cache
     * 3. **Broadcasting**: Queues transcription for WebSocket broadcast with metadata
     * 4. **Logging**: Records final result in application logs
     * 5. **Console Output**: Displays result to stdout with [FINAL] prefix
     * 6. **Benchmarking**: Records metrics if benchmarking is enabled
     * 7. **Timing Reset**: Updates finalization timestamp
     *
     * @par Benchmark Metrics:
     * When benchmarking is enabled, records:
     * - Transcription text and type ("final")
     * - Confidence score (1.0 for final results)
     * - Audio sample count processed
     * - Processing latency in milliseconds
     *
     * @par Thread Safety:
     * Method is thread-safe for WebSocket broadcasting and benchmarking operations.
     * Console output may interleave with other threads.
     *
     * @note Empty or whitespace-only text is ignored
     * @note Identical consecutive results are filtered out automatically
     */
    void handle_final_result(const std::string& text);

    /**
     * @brief Processes and displays partial transcription results
     *
     * Handles partial (intermediate) transcription results from the speech engine.
     * These results represent the current best guess of the ongoing utterance
     * and are updated continuously during speech. Only displayed if partial
     * results are enabled in the engine configuration.
     *
     * @param partial Partial transcribed text representing the current state
     *                of the ongoing utterance. May change rapidly during speech
     *                and should not be considered final
     *
     * @par Processing Steps:
     * 1. **Deduplication**: Compares with previous partial result
     * 2. **State Update**: Updates internal partial text cache
     * 3. **Logging**: Records partial result at debug level
     * 4. **Console Display**: Shows live partial result with [PARTIAL] prefix
     *
     * @par Display Behavior:
     * - Uses carriage return (\\r) for in-place updates
     * - Appends "..." to indicate incomplete status
     * - Pads with spaces to clear previous longer text
     * - Only shown if engine has partial results enabled
     *
     * @par Performance Notes:
     * - High frequency updates (multiple per second during speech)
     * - Minimal processing overhead - just string comparison and console I/O
     * - Not broadcast via WebSocket (only final results are broadcast)
     *
     * @note Partial results are not sent to WebSocket clients
     * @note Empty partial results are ignored
     * @note Console output may flicker during rapid speech recognition
     */
    void handle_partial_result(const std::string& partial);

    /**
     * @brief Updates and logs voice activity detection debug information
     *
     * Provides periodic status updates about VAD operation for debugging and
     * monitoring purposes. Called regularly during audio processing to track
     * VAD decisions, silence frame counting, and speech detection status.
     * Only active when VAD is enabled.
     *
     * @par Debug Information Logged:
     * - Current VAD decision (SPEECH/SILENCE)
     * - Silence frame counter and threshold
     * - Processing timing and intervals
     * - VAD algorithm parameters and state
     *
     * @par Update Frequency:
     * - Logs debug information every 1000ms to avoid log spam
     * - Uses internal timestamp tracking for rate limiting
     * - Only logs when VAD status changes or at periodic intervals
     *
     * @par Typical Log Output:
     * ```
     * VAD status: SPEECH, silence frames: 0, threshold: 5
     * VAD status: SILENCE, silence frames: 3/5, threshold: 5
     * ```
     *
     * @note Only active when use_vad is true
     * @note Log level is DEBUG - may not appear in production logs
     * @note Minimal performance impact due to rate limiting
     */
    void update_debug_status();

    // Member variables with comprehensive documentation

    /**
     * @brief Pointer to the Vosk speech recognition engine
     *
     * Primary interface to the Vosk speech recognition system. Used for:
     * - Processing audio buffers into transcription results
     * - Checking partial result capability
     * - Resetting recognizer state between utterances
     * - Configuring recognition parameters
     *
     * @note Must remain valid for the lifetime of this processor
     * @warning Not owned by this class - do not delete
     */
    vstream_engine* m_engine;

    /**
     * @brief Pointer to the WebSocket server for result broadcasting
     *
     * Used to broadcast transcription results to connected WebSocket clients.
     * Provides the primary output mechanism for real-time transcription delivery.
     *
     * @note Must remain valid for the lifetime of this processor
     * @warning Not owned by this class - do not delete
     */
    hyni_websocket_server* m_server;

    /**
     * @brief Pointer to the voice activity detector with hangover
     *
     * WebRTC-based VAD implementation with hangover logic for robust
     * speech boundary detection. Can be nullptr if VAD is disabled.
     *
     * @note May be nullptr if use_vad is false
     * @warning Not owned by this class - do not delete
     */
    vad_with_hangover* m_vad;

    /**
     * @brief Session identifier for WebSocket message routing
     *
     * Unique identifier used to associate transcription results with
     * specific client sessions. Currently set to "mic-capture" for
     * microphone-based processing.
     *
     * @note Used in WebSocket message metadata
     */
    std::string m_session_id;

    /**
     * @brief Flag indicating whether partial results should be displayed
     *
     * Automatically determined from engine capabilities during initialization.
     * Controls whether partial transcription results are shown to the user
     * during ongoing speech recognition.
     *
     * @note Set automatically based on engine configuration
     */
    bool m_show_partial;

    /**
     * @brief Number of consecutive silence frames required for finalization
     *
     * Threshold for triggering automatic result finalization in VAD mode.
     * Calculated as: silence_duration_ms / buffer_ms. Higher values provide
     * more tolerance for brief pauses in speech.
     *
     * @par Typical Values:
     * - 2-3 frames: Responsive (200-300ms silence)
     * - 4-5 frames: Balanced (400-500ms silence)
     * - 6-8 frames: Conservative (600-800ms silence)
     */
    int m_silence_frames_threshold;

    /**
     * @brief Enable/disable voice activity detection processing
     *
     * When true, uses VAD for intelligent speech boundary detection.
     * When false, processes all audio as potential speech with time-based
     * finalization only.
     *
     * @note Affects processing mode and CPU usage
     */
    bool m_use_vad;

    /**
     * @brief Maximum interval between forced result finalizations (milliseconds)
     *
     * Prevents excessively long utterances by forcing periodic finalization
     * even during continuous speech. Provides a safety mechanism for both
     * VAD and time-based modes.
     *
     * @par Typical Range: 1000-5000ms
     */
    int m_finalize_interval_ms;

    /**
     * @brief Audio buffer duration in milliseconds
     *
     * Determines the temporal granularity of audio processing and VAD decisions.
     * Smaller values provide lower latency but higher processing overhead.
     *
     * @par Impact on Performance:
     * - 50ms: Low latency, high CPU usage
     * - 100ms: Balanced performance
     * - 200ms: Lower CPU, higher latency
     */
    int m_buffer_ms;

    /**
     * @brief Timestamp of the last forced finalization
     *
     * Used to track time intervals for periodic finalization logic.
     * Updated whenever force_finalize() is called, either due to
     * silence detection or time-based triggers.
     */
    std::chrono::steady_clock::time_point m_last_finalize_time;

    /**
     * @brief Timestamp of the last debug status update
     *
     * Rate-limits debug status logging to prevent log spam during
     * continuous audio processing. Updated in update_debug_status().
     */
    std::chrono::steady_clock::time_point m_last_debug_time;

    // State tracking variables

    /**
     * @brief Flag indicating recent speech activity
     *
     * Tracks whether speech was detected in recent audio buffers.
     * Used for state management and silence frame counting logic.
     * Reset to false after finalization.
     */
    bool m_was_speaking = false;

    /**
     * @brief Counter for consecutive silence frames detected
     *
     * Incremented for each silence frame when was_speaking is true.
     * Reset to zero when speech is detected. Used to trigger
     * finalization when m_silence_frames_threshold is reached.
     */
    int m_silence_frames = 0;

    /**
     * @brief Cache of the last final transcription result
     *
     * Used for deduplication to prevent broadcasting identical final
     * results consecutively. Cleared after finalization reset.
     *
     * @note Pre-allocated with reserve(256) for performance
     */
    std::string m_last_final_text;

    /**
     * @brief Cache of the last partial transcription result
     *
     * Used for deduplication to prevent displaying identical partial
     * results consecutively. Updated frequently during speech.
     *
     * @note Pre-allocated with reserve(256) for performance
     */
    std::string m_last_partial_text;

    // Performance optimization

    /**
     * @brief Reusable string buffer for JSON result processing
     *
     * Pre-allocated string buffer used to store JSON responses from
     * the speech engine. Reduces memory allocation overhead during
     * high-frequency audio processing.
     *
     * @note Pre-allocated with reserve(1024) for typical JSON sizes
     */
    std::string m_result_buffer;

    /**
     * @brief Optional benchmark manager for performance monitoring
     *
     * When provided, records detailed performance metrics including
     * latency, confidence scores, and transcription quality measures.
     * Can be nullptr if benchmarking is not required.
     *
     * @note May be nullptr - always check before use
     * @warning Not owned by this class - do not delete
     */
    benchmark_manager* m_benchmark = nullptr;

    /**
     * @brief Accumulated audio sample count for benchmark correlation
     *
     * Tracks the total number of audio samples processed since the last
     * finalization. Used by benchmark manager to correlate processing
     * metrics with audio duration. Reset after each final result.
     */
    size_t m_accumulated_audio_samples = 0;
};
