/**
 * @file audio_capture.h
 * @brief PortAudio-based microphone capture for Qt applications
 *
 * Provides real-time audio capture with level monitoring and VAD simulation
 * for the vstream Qt6 client application.
 */

#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <portaudio.h>
#include <vector>
#include <atomic>
#include <memory>

/**
 * @class AudioCapture
 * @brief High-performance audio capture using PortAudio
 *
 * The AudioCapture class provides real-time microphone input for the vstream
 * Qt6 client, featuring:
 *
 * - **Multi-device Support**: Enumeration and selection of audio input devices
 * - **Flexible Sample Rates**: Support for 8kHz, 16kHz, 32kHz, and 48kHz
 * - **Real-time Monitoring**: Audio level detection and VAD simulation
 * - **Qt Integration**: Signal-based interface for seamless Qt integration
 * - **Thread Safety**: Safe operation with Qt's event loop
 * - **Error Handling**: Comprehensive PortAudio error management
 *
 * @par Audio Pipeline:
 * ```
 * Microphone → PortAudio → Level Detection → Format Conversion → Qt Signals
 *      ↓            ↓           ↓               ↓                ↓
 *   Hardware    Callback    Peak Analysis   16-bit PCM      WebSocket
 * ```
 *
 * @par Supported Configurations:
 * - Sample rates: 8000, 16000, 32000, 48000 Hz
 * - Format: 16-bit signed PCM
 * - Channels: Mono (1 channel)
 * - Buffer sizes: 512-2048 samples (configurable)
 *
 * @par Thread Safety:
 * Audio callbacks run in PortAudio's audio thread. Qt signals are emitted
 * safely using Qt::QueuedConnection for thread-safe communication.
 */
class AudioCapture : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs audio capture instance
     * @param parent Parent QObject for Qt memory management
     */
    explicit AudioCapture(QObject *parent = nullptr);

    /**
     * @brief Destructor - ensures clean shutdown
     */
    ~AudioCapture();

    /**
     * @brief Enumerates available audio input devices
     * @return List of device names suitable for user selection
     */
    QStringList getInputDevices() const;

    /**
     * @brief Starts audio recording from specified device
     * @param device_index Device index from getInputDevices() (or -1 for default)
     * @param sample_rate Desired sample rate (8000, 16000, 32000, or 48000)
     * @param buffer_size Buffer size in samples (default: 1024)
     * @return true if recording started successfully, false otherwise
     */
    bool startRecording(int device_index = -1, int sample_rate = 16000, int buffer_size = 1024);

    /**
     * @brief Stops audio recording
     */
    void stopRecording();

    /**
     * @brief Checks if currently recording
     * @return true if recording is active, false otherwise
     */
    bool isRecording() const { return m_is_recording.load(); }

    /**
     * @brief Gets the current sample rate
     * @return Sample rate in Hz, or 0 if not recording
     */
    int getCurrentSampleRate() const { return m_sample_rate; }

    /**
     * @brief Gets the current audio level (0.0 to 1.0)
     * @return Normalized audio level
     */
    float getCurrentLevel() const { return m_current_level.load(); }

signals:
    /**
     * @brief Emitted when new audio data is available
     * @param samples Vector of 16-bit PCM samples
     * @param sample_rate Sample rate of the audio data
     *
     * This signal is emitted for each audio buffer captured from the microphone.
     * The data is ready for transmission to vstream via WebSocket.
     */
    void audioData(const std::vector<int16_t>& samples, int sample_rate);

    /**
     * @brief Emitted when audio level changes
     * @param level Normalized audio level (0.0 = silence, 1.0 = maximum)
     *
     * Updated approximately every 100ms for real-time level monitoring.
     * Can be used to drive audio level meters and visual feedback.
     */
    void audioLevel(float level);

    /**
     * @brief Emitted when Voice Activity Detection status changes
     * @param active true if speech detected, false for silence
     *
     * Simple VAD simulation based on audio level and duration.
     * Not as sophisticated as WebRTC VAD but useful for basic feedback.
     */
    void vadStatus(bool active);

    /**
     * @brief Emitted when audio errors occur
     * @param error_message Description of the error
     */
    void errorOccurred(const QString& error_message);

private slots:
    /**
     * @brief Updates VAD status based on current audio level
     */
    void updateVadStatus();

private:
    /**
     * @brief Initializes PortAudio library
     * @return true if successful, false otherwise
     */
    bool initializePortAudio();

    /**
     * @brief Cleans up PortAudio resources
     */
    void cleanupPortAudio();

    /**
     * @brief PortAudio callback for audio input
     * @param input_buffer Input audio samples from microphone
     * @param output_buffer Output buffer (unused for input-only)
     * @param frames_per_buffer Number of sample frames in buffer
     * @param time_info Timing information from PortAudio
     * @param status_flags Status flags indicating stream conditions
     * @param user_data Pointer to AudioCapture instance
     * @return PortAudio callback result code
     */
    static int audioCallback(const void* input_buffer,
                             void* output_buffer,
                             unsigned long frames_per_buffer,
                             const PaStreamCallbackTimeInfo* time_info,
                             PaStreamCallbackFlags status_flags,
                             void* user_data);

    /**
     * @brief Processes audio buffer in callback context
     * @param input_buffer Raw audio samples from PortAudio
     * @param frame_count Number of sample frames
     */
    void processAudioBuffer(const float* input_buffer, unsigned long frame_count);

    /**
     * @brief Calculates RMS level of audio buffer
     * @param samples Audio samples
     * @param count Number of samples
     * @return RMS level (0.0 to 1.0)
     */
    float calculateLevel(const float* samples, unsigned long count);

    // PortAudio state
    PaStream* m_stream;                    ///< PortAudio stream handle
    std::atomic<bool> m_is_recording;      ///< Recording state flag
    std::atomic<bool> m_portaudio_initialized; ///< PortAudio init state

    // Audio configuration
    int m_sample_rate;                     ///< Current sample rate
    int m_buffer_size;                     ///< Buffer size in samples
    int m_device_index;                    ///< Current device index

    // Level monitoring
    std::atomic<float> m_current_level;    ///< Current audio level
    QTimer* m_vad_timer;                   ///< VAD update timer
    float m_vad_threshold;                 ///< VAD threshold level
    int m_vad_hangover_ms;                 ///< VAD hangover duration
    bool m_vad_active;                     ///< Current VAD state

    // Thread-safe audio buffer
    std::vector<int16_t> m_audio_buffer;   ///< Converted audio samples

    // Constants
    static constexpr float VAD_THRESHOLD = 0.01f;    ///< Default VAD threshold
    static constexpr int VAD_UPDATE_MS = 100;        ///< VAD update interval
    static constexpr int VAD_HANGOVER_MS = 500;      ///< VAD hangover duration
    static constexpr float LEVEL_SMOOTHING = 0.1f;   ///< Level smoothing factor
};
