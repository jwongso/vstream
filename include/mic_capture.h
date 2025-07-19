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
 * This module provides a high-performance, thread-safe audio capture system
 * optimized for Automatic Speech Recognition (ASR) applications. It uses
 * PortAudio for cross-platform audio input and a lock-free queue for
 * efficient audio data transfer between threads.
 *
 * @example
 * @code
 * // Basic usage
 * mic_capture::config cfg;
 * cfg.sample_rate = 16000;
 * cfg.accumulate_ms = 100;  // Process 100ms chunks
 *
 * mic_capture mic(cfg);
 * mic.set_audio_callback([](const std::vector<int16_t>& audio) {
 *     // Process audio data
 * });
 * mic.start();
 * @endcode
 */

#pragma once

#include <portaudio.h>
#include <moodycamel/concurrentqueue.h>
#include <vector>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <thread>
#include <chrono>

/**
 * @class mic_capture
 * @brief Thread-safe microphone audio capture with buffering for ASR
 *
 * This class provides real-time audio capture from system microphones with
 * features specifically designed for speech recognition:
 * - Configurable audio accumulation for optimal ASR chunk sizes
 * - Lock-free audio queue for low-latency processing
 * - Automatic sample rate and format configuration
 * - Drop detection for overrun scenarios
 *
 * The class uses a producer-consumer pattern where:
 * - Producer: PortAudio callback captures audio from hardware
 * - Consumer: Processing thread delivers audio to user callback
 *
 * @note The audio callback is executed in a separate thread
 * @warning Do not perform blocking operations in the audio callback
 */
class mic_capture {
public:
    /**
     * @typedef audio_callback_t
     * @brief Callback function type for audio data delivery
     *
     * @param samples Vector containing PCM audio samples (16-bit signed)
     *
     * @note Called from the processing thread, not the audio thread
     * @note The vector size depends on the accumulate_ms configuration
     */
    using audio_callback_t = std::function<void(const std::vector<int16_t>&)>;

    /**
     * @struct config
     * @brief Configuration parameters for microphone capture
     *
     * This structure contains all configurable parameters for the audio
     * capture system. Default values are optimized for speech recognition
     * at 16kHz sampling rate.
     */
    struct config {
        /**
         * @brief Audio sample rate in Hz
         * @note Common values: 8000, 16000, 44100, 48000
         * @note 16000 Hz is standard for speech recognition
         */
        int sample_rate = 16000;

        /**
         * @brief Number of audio channels
         * @note 1 = mono (recommended for ASR), 2 = stereo
         */
        int channels = 1;

        /**
         * @brief PortAudio frames per buffer
         * @note Lower values = lower latency but higher CPU usage
         * @note 160 frames = 10ms at 16kHz
         */
        int frames_per_buffer = 160;

        /**
         * @brief PortAudio device index
         * @note -1 = use system default input device
         * @note Use list_devices() to enumerate available devices
         */
        int device_index = -1;

        /**
         * @brief Maximum size of the audio queue
         * @note Prevents unbounded memory growth if consumer is slow
         */
        size_t queue_size = 1000;

        /**
         * @brief Audio accumulation time in milliseconds
         *
         * Controls how much audio is accumulated before delivering to callback.
         * This is important for ASR systems that process audio in chunks.
         *
         * @note 100ms is optimal for Vosk and similar ASR engines
         * @note Lower values = lower latency but more callback overhead
         * @note Higher values = better efficiency but higher latency
         */
        int accumulate_ms = 100;

        /**
         * @brief Default constructor with sensible defaults for ASR
         */
        config() = default;
    };

    /**
     * @brief Default constructor using default configuration
     * @throws std::runtime_error if PortAudio initialization fails
     */
    explicit mic_capture();

    /**
     * @brief Construct with custom configuration
     * @param cfg Configuration parameters
     * @throws std::runtime_error if PortAudio initialization fails
     */
    explicit mic_capture(const config& cfg);

    /**
     * @brief Destructor - stops capture and releases resources
     * @note Automatically calls stop() if still running
     */
    ~mic_capture();

    /**
     * @brief Start audio capture
     *
     * Opens the audio device, starts the capture stream, and begins
     * the processing thread. Audio will be delivered to the callback
     * function if one has been set.
     *
     * @return true if successfully started, false otherwise
     *
     * @note Safe to call multiple times (no-op if already running)
     * @note Check PortAudio error messages on console if this fails
     *
     * @see stop()
     * @see set_audio_callback()
     */
    bool start();

    /**
     * @brief Stop audio capture
     *
     * Stops the audio stream, joins the processing thread, and clears
     * any queued audio data. This method blocks until all threads have
     * cleanly terminated.
     *
     * @note Safe to call multiple times
     * @note Blocks until processing thread exits
     */
    void stop();

    /**
     * @brief Set the audio callback function
     *
     * The callback will be invoked from the processing thread whenever
     * accumulated audio data is ready. The amount of audio depends on
     * the accumulate_ms configuration.
     *
     * @param callback Function to call with audio data
     *
     * @note Can be called before or after start()
     * @note Set to nullptr to disable callback processing
     * @warning Do not perform blocking operations in the callback
     *
     * @example
     * @code
     * mic.set_audio_callback([](const std::vector<int16_t>& audio) {
     *     // Process audio - this is called from a separate thread
     *     vosk_recognizer_accept_waveform(recognizer, audio.data(), audio.size());
     * });
     * @endcode
     */
    void set_audio_callback(audio_callback_t callback);

    /**
     * @brief Directly dequeue audio data without callback
     *
     * Alternative to using callbacks - allows manual polling for audio data.
     * Useful for integrating with existing event loops.
     *
     * @param[out] samples Vector to receive audio samples
     * @return true if audio was dequeued, false if queue was empty
     *
     * @note Non-blocking operation
     * @note Can be used instead of or in addition to callbacks
     */
    bool dequeue_audio(std::vector<int16_t>& samples);

    /**
     * @brief Check if audio capture is currently running
     * @return true if capture is active, false otherwise
     */
    bool is_running() const { return m_running; }

    /**
     * @brief Get the number of dropped audio frames
     *
     * Frames are dropped when the audio queue is full, indicating that
     * the consumer cannot keep up with the audio input rate.
     *
     * @return Total number of frames dropped since start
     *
     * @note A non-zero value indicates performance issues
     * @note Reset to zero on each start()
     */
    size_t get_dropped_frames() const { return m_dropped_frames; }

    /**
     * @brief List all available audio input devices
     *
     * Prints detailed information about all audio input devices to stdout.
     * Useful for determining device indices for the configuration.
     *
     * @note Static method - can be called without instance
     * @note Output includes device index, name, channels, and sample rates
     *
     * @example
     * @code
     * mic_capture::list_devices();  // Print available devices
     * // Then use a specific device:
     * config.device_index = 2;  // Use device #2
     * @endcode
     */
    static void list_devices();

private:
    /**
     * @brief Current configuration
     */
    config m_config;

    /**
     * @brief PortAudio stream handle
     */
    PaStream* m_stream = nullptr;

    /**
     * @brief Lock-free queue for audio data transfer
     *
     * Uses moodycamel::ConcurrentQueue for efficient single-producer,
     * single-consumer audio data transfer without locks.
     */
    moodycamel::ConcurrentQueue<std::vector<int16_t>> m_audio_queue;

    /**
     * @brief User-provided audio callback function
     */
    audio_callback_t m_callback;

    /**
     * @brief Processing thread for audio callback delivery
     */
    std::thread m_processing_thread;

    /**
     * @brief Atomic flag indicating capture is active
     */
    std::atomic<bool> m_running{false};

    /**
     * @brief Counter for dropped audio frames
     */
    std::atomic<size_t> m_dropped_frames{0};

    /**
     * @brief Buffer for accumulating audio before delivery
     *
     * Audio is accumulated here until we have enough samples
     * as specified by accumulate_ms configuration.
     */
    std::vector<int16_t> m_accumulation_buffer;

    /**
     * @brief Current number of accumulated frames
     */
    size_t m_accumulated_frames = 0;

    /**
     * @brief Target number of frames to accumulate
     *
     * Calculated from sample_rate and accumulate_ms
     */
    size_t m_frames_to_accumulate;

    /**
     * @brief Condition variable for efficient thread synchronization
     */
    std::condition_variable m_data_cv;

    /**
     * @brief Mutex for condition variable
     */
    std::mutex m_data_mutex;

    /**
     * @brief PortAudio callback function
     *
     * Called by PortAudio from the audio thread when new audio data
     * is available. This function must be real-time safe.
     *
     * @param input Pointer to input audio buffer
     * @param output Unused (nullptr)
     * @param frameCount Number of frames in the buffer
     * @param timeInfo Timing information from PortAudio
     * @param statusFlags Status flags from PortAudio
     * @param userData Pointer to mic_capture instance
     * @return paContinue to keep the stream running
     *
     * @note Runs in PortAudio's audio thread - must be real-time safe
     * @warning Do not call blocking functions or allocate memory here
     */
    static int pa_callback(const void* input, void* output,
                           unsigned long frameCount,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void* userData);

    /**
     * @brief Main processing loop for audio callback delivery
     *
     * Runs in a separate thread, dequeuing audio from the lock-free
     * queue and delivering it to the user callback.
     *
     * @note Runs until m_running becomes false
     * @note Uses condition variable for efficient waiting
     */
    void processing_loop();
};
