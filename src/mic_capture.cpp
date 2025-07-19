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

#include "mic_capture.h"
#include "logger.h"
#include <iostream>
#include <cstring>

mic_capture::mic_capture() {}

mic_capture::mic_capture(const config& cfg)
    : m_config(cfg), m_audio_queue(cfg.queue_size) {

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        throw std::runtime_error("PortAudio initialization error: " +
                                 std::string(Pa_GetErrorText(err)));
    }

    // Calculate frames needed for accumulation
    m_frames_to_accumulate = (m_config.sample_rate * m_config.accumulate_ms) / 1000;
    m_accumulation_buffer.reserve(m_frames_to_accumulate * m_config.channels);

    LOG_INFO("Mic capture initialized. Accumulating " +
             std::to_string(m_config.accumulate_ms) + "ms of audio");
}

mic_capture::~mic_capture() {
    stop();
    Pa_Terminate();
}

bool mic_capture::start() {
    if (m_running) return true;

    PaStreamParameters inputParams;
    memset(&inputParams, 0, sizeof(inputParams));

    inputParams.device = (m_config.device_index < 0) ?
                             Pa_GetDefaultInputDevice() : m_config.device_index;

    if (inputParams.device == paNoDevice) {
        LOG_ERROR("No audio input device found");
        return false;
    }

    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(inputParams.device);
    LOG_INFO("Using audio device: " + std::string(deviceInfo->name));

    inputParams.channelCount = m_config.channels;
    inputParams.sampleFormat = paInt16;
    inputParams.suggestedLatency = deviceInfo->defaultLowInputLatency;
    inputParams.hostApiSpecificStreamInfo = nullptr;

    PaError err = Pa_OpenStream(&m_stream,
                                &inputParams,
                                nullptr,
                                m_config.sample_rate,
                                m_config.frames_per_buffer,
                                paClipOff,
                                &mic_capture::pa_callback,
                                this);

    if (err != paNoError) {
        LOG_ERROR("Failed to open stream: " + std::string(Pa_GetErrorText(err)));
        return false;
    }

    err = Pa_StartStream(m_stream);
    if (err != paNoError) {
        LOG_ERROR("Failed to start stream: " + std::string(Pa_GetErrorText(err)));
        Pa_CloseStream(m_stream);
        m_stream = nullptr;
        return false;
    }

    m_running = true;
    m_dropped_frames = 0;

    if (m_callback) {
        m_processing_thread = std::thread(&mic_capture::processing_loop, this);
    }

    return true;
}

void mic_capture::stop() {
    if (!m_running) return;

    m_running = false;
    m_data_cv.notify_all();  // Wake up the processing thread

    if (m_processing_thread.joinable()) {
        m_processing_thread.join();
    }

    if (m_stream) {
        Pa_StopStream(m_stream);
        Pa_CloseStream(m_stream);
        m_stream = nullptr;
    }

    // Clear queue
    std::vector<int16_t> discard;
    while (m_audio_queue.try_dequeue(discard)) {}

    LOG_INFO("Microphone capture stopped");
}

int mic_capture::pa_callback(const void* input, void* /*output*/,
                             unsigned long frameCount,
                             const PaStreamCallbackTimeInfo* /*timeInfo*/,
                             PaStreamCallbackFlags /*statusFlags*/,
                             void* userData) {
    auto* self = static_cast<mic_capture*>(userData);
    const int16_t* samples = static_cast<const int16_t*>(input);
    const size_t sample_count = frameCount * self->m_config.channels;

    // Add to accumulation buffer
    self->m_accumulation_buffer.insert(self->m_accumulation_buffer.end(),
                                       samples, samples + sample_count);
    self->m_accumulated_frames += frameCount;

    // Check if we have enough frames
    if (self->m_accumulated_frames >= self->m_frames_to_accumulate) {
        // Move accumulated audio to queue
        if (!self->m_audio_queue.try_enqueue(std::move(self->m_accumulation_buffer))) {
            self->m_dropped_frames.fetch_add(self->m_accumulated_frames);
        } else {
            // Notify the processing thread
            self->m_data_cv.notify_one();
        }

        // Reset accumulation
        self->m_accumulation_buffer.clear();
        self->m_accumulation_buffer.reserve(self->m_frames_to_accumulate * self->m_config.channels);
        self->m_accumulated_frames = 0;
    }

    return paContinue;
}

void mic_capture::processing_loop() {
    LOG_INFO("Processing thread started");

    std::vector<int16_t> audio_data;

    while (m_running) {
        // Try to dequeue first
        if (m_audio_queue.try_dequeue(audio_data)) {
            if (m_callback) {
                m_callback(audio_data);
            }
        } else {
            // Wait for notification with timeout
            std::unique_lock<std::mutex> lock(m_data_mutex);
            m_data_cv.wait_for(lock, std::chrono::milliseconds(100),
                               [this] { return !m_running || m_audio_queue.size_approx() > 0; });
        }
    }

    LOG_INFO("Processing thread stopped");
}

bool mic_capture::dequeue_audio(std::vector<int16_t>& samples) {
    return m_audio_queue.try_dequeue(samples);
}

void mic_capture::set_audio_callback(audio_callback_t callback) {
    m_callback = std::move(callback);
}

void mic_capture::list_devices() {
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio initialization error: " << Pa_GetErrorText(err) << std::endl;
        return;
    }

    int numDevices = Pa_GetDeviceCount();
    std::cout << "Available audio input devices:\n";
    std::cout << "-----------------------------\n";

    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i);
        if (deviceInfo->maxInputChannels > 0) {
            std::cout << "Device #" << i << ": " << deviceInfo->name << "\n";
            std::cout << "  Input channels: " << deviceInfo->maxInputChannels << "\n";
            std::cout << "  Default sample rate: " << deviceInfo->defaultSampleRate << " Hz\n";

            if (i == Pa_GetDefaultInputDevice()) {
                std::cout << "  (Default input device)\n";
            }
            std::cout << "\n";
        }
    }

    Pa_Terminate();
}
