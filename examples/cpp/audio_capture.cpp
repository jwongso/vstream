/**
 * @file audio_capture.cpp
 * @brief Implementation of PortAudio-based audio capture
 */

#include "audio_capture.h"
#include <QDebug>
#include <QTime>
#include <algorithm>
#include <cmath>

AudioCapture::AudioCapture(QObject *parent)
    : QObject(parent)
    , m_stream(nullptr)
    , m_is_recording(false)
    , m_portaudio_initialized(false)
    , m_sample_rate(16000)
    , m_buffer_size(1024)
    , m_device_index(-1)
    , m_current_level(0.0f)
    , m_vad_threshold(VAD_THRESHOLD)
    , m_vad_hangover_ms(VAD_HANGOVER_MS)
    , m_vad_active(false)
{
    // Initialize PortAudio
    if (!initializePortAudio()) {
        qWarning() << "Failed to initialize PortAudio";
        return;
    }

    // Setup VAD timer
    m_vad_timer = new QTimer(this);
    connect(m_vad_timer, &QTimer::timeout, this, &AudioCapture::updateVadStatus);
    m_vad_timer->start(VAD_UPDATE_MS);

    qDebug() << "AudioCapture initialized successfully";
}

AudioCapture::~AudioCapture()
{
    stopRecording();
    cleanupPortAudio();
}

bool AudioCapture::initializePortAudio()
{
    PaError error = Pa_Initialize();
    if (error != paNoError) {
        qWarning() << "PortAudio initialization failed:" << Pa_GetErrorText(error);
        return false;
    }

    m_portaudio_initialized = true;
    qDebug() << "PortAudio initialized. Version:" << Pa_GetVersionText();
    return true;
}

void AudioCapture::cleanupPortAudio()
{
    if (m_portaudio_initialized) {
        Pa_Terminate();
        m_portaudio_initialized = false;
    }
}

QStringList AudioCapture::getInputDevices() const
{
    QStringList devices;

    if (!m_portaudio_initialized) {
        return devices;
    }

    int device_count = Pa_GetDeviceCount();
    if (device_count < 0) {
        qWarning() << "Failed to get device count:" << Pa_GetErrorText(device_count);
        return devices;
    }

    for (int i = 0; i < device_count; ++i) {
        const PaDeviceInfo* device_info = Pa_GetDeviceInfo(i);
        if (device_info && device_info->maxInputChannels > 0) {
            QString device_name = QString::fromUtf8(device_info->name);
            const PaHostApiInfo* host_info = Pa_GetHostApiInfo(device_info->hostApi);
            if (host_info) {
                device_name += QString(" (%1)").arg(host_info->name);
            }
            devices.append(device_name);
        }
    }

    qDebug() << "Found" << devices.size() << "input devices";
    return devices;
}

bool AudioCapture::startRecording(int device_index, int sample_rate, int buffer_size)
{
    if (m_is_recording) {
        qWarning() << "Already recording";
        return false;
    }

    if (!m_portaudio_initialized) {
        emit errorOccurred("PortAudio not initialized");
        return false;
    }

    // Validate parameters
    if (sample_rate != 8000 && sample_rate != 16000 &&
        sample_rate != 32000 && sample_rate != 48000) {
        emit errorOccurred("Unsupported sample rate");
        return false;
    }

    m_sample_rate = sample_rate;
    m_buffer_size = buffer_size;
    m_device_index = device_index;

    // Setup stream parameters
    PaStreamParameters input_params;
    input_params.device = (device_index >= 0) ? device_index : Pa_GetDefaultInputDevice();

    if (input_params.device == paNoDevice) {
        emit errorOccurred("No input device available");
        return false;
    }

    const PaDeviceInfo* device_info = Pa_GetDeviceInfo(input_params.device);
    if (!device_info) {
        emit errorOccurred("Failed to get device info");
        return false;
    }

    input_params.channelCount = 1; // Mono
    input_params.sampleFormat = paFloat32;
    input_params.suggestedLatency = device_info->defaultLowInputLatency;
    input_params.hostApiSpecificStreamInfo = nullptr;

    // Open stream
    PaError error = Pa_OpenStream(
        &m_stream,
        &input_params,
        nullptr, // No output
        sample_rate,
        buffer_size,
        paClipOff,
        audioCallback,
        this
        );

    if (error != paNoError) {
        emit errorOccurred(QString("Failed to open stream: %1").arg(Pa_GetErrorText(error)));
        return false;
    }

    // Start stream
    error = Pa_StartStream(m_stream);
    if (error != paNoError) {
        Pa_CloseStream(m_stream);
        m_stream = nullptr;
        emit errorOccurred(QString("Failed to start stream: %1").arg(Pa_GetErrorText(error)));
        return false;
    }

    m_is_recording = true;
    m_audio_buffer.reserve(buffer_size);

    qDebug() << "Recording started - Device:" << device_info->name
             << "Sample rate:" << sample_rate << "Buffer size:" << buffer_size;

    return true;
}

void AudioCapture::stopRecording()
{
    if (!m_is_recording) {
        return;
    }

    m_is_recording = false;

    if (m_stream) {
        Pa_StopStream(m_stream);
        Pa_CloseStream(m_stream);
        m_stream = nullptr;
    }

    m_current_level = 0.0f;
    m_vad_active = false;
    emit vadStatus(false);

    qDebug() << "Recording stopped";
}

int AudioCapture::audioCallback(const void* input_buffer,
                                void* /*output_buffer*/,
                                unsigned long frames_per_buffer,
                                const PaStreamCallbackTimeInfo* /*time_info*/,
                                PaStreamCallbackFlags status_flags,
                                void* user_data)
{
    auto* capture = static_cast<AudioCapture*>(user_data);

    if (!capture || !capture->m_is_recording) {
        return paComplete;
    }

    if (status_flags & paInputUnderflow) {
        qWarning() << "Audio input underflow detected";
    }

    if (status_flags & paInputOverflow) {
        qWarning() << "Audio input overflow detected";
    }

    if (input_buffer) {
        capture->processAudioBuffer(static_cast<const float*>(input_buffer), frames_per_buffer);
    }

    return paContinue;
}

void AudioCapture::processAudioBuffer(const float* input_buffer, unsigned long frame_count)
{
    if (!input_buffer || frame_count == 0) {
        return;
    }

    // Calculate audio level
    float level = calculateLevel(input_buffer, frame_count);

    // Smooth level updates
    float current = m_current_level.load();
    float smoothed = current + LEVEL_SMOOTHING * (level - current);
    m_current_level = smoothed;

    // Convert to 16-bit PCM
    m_audio_buffer.clear();
    m_audio_buffer.reserve(frame_count);

    for (unsigned long i = 0; i < frame_count; ++i) {
        float sample = input_buffer[i];

        // Clamp to [-1.0, 1.0]
        sample = std::max(-1.0f, std::min(1.0f, sample));

        // Convert to 16-bit
        int16_t pcm_sample = static_cast<int16_t>(sample * 32767.0f);
        m_audio_buffer.push_back(pcm_sample);
    }

    // Emit audio data signal (thread-safe)
    emit audioData(m_audio_buffer, m_sample_rate);

    // Emit level update periodically
    static int level_counter = 0;
    if (++level_counter >= 10) { // Every ~100ms at typical buffer sizes
        emit audioLevel(smoothed);
        level_counter = 0;
    }
}

float AudioCapture::calculateLevel(const float* samples, unsigned long count)
{
    if (!samples || count == 0) {
        return 0.0f;
    }

    // Calculate RMS level
    float sum_squares = 0.0f;
    for (unsigned long i = 0; i < count; ++i) {
        float sample = samples[i];
        sum_squares += sample * sample;
    }

    float rms = std::sqrt(sum_squares / count);
    return std::min(1.0f, rms * 3.0f); // Scale for better visualization
}

void AudioCapture::updateVadStatus()
{
    if (!m_is_recording) {
        if (m_vad_active) {
            m_vad_active = false;
            emit vadStatus(false);
        }
        return;
    }

    float current_level = m_current_level.load();
    bool should_be_active = current_level > m_vad_threshold;

    // Simple VAD with hangover
    static QTime last_speech_time;
    static bool was_active = false;

    if (should_be_active) {
        last_speech_time = QTime::currentTime();
        if (!was_active) {
            m_vad_active = true;
            emit vadStatus(true);
            was_active = true;
        }
    } else {
        if (was_active) {
            int ms_since_speech = last_speech_time.msecsTo(QTime::currentTime());
            if (ms_since_speech > m_vad_hangover_ms) {
                m_vad_active = false;
                emit vadStatus(false);
                was_active = false;
            }
        }
    }
}
