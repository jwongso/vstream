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

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "mic_capture.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <memory>

using ::testing::_;
using ::testing::Return;
using ::testing::DoAll;
using ::testing::SetArgReferee;
using ::testing::AtLeast;
using ::testing::StrictMock;

// Testable version of mic_capture that doesn't actually use PortAudio
class testable_mic_capture {
public:
    using audio_callback_t = std::function<void(const std::vector<int16_t>&)>;

    struct config {
        int sample_rate;
        int channels;
        int frames_per_buffer;
        int device_index;
        size_t queue_size;
        int accumulate_ms;

        // Constructor with defaults
        config()
            : sample_rate(16000)
            , channels(1)
            , frames_per_buffer(160)
            , device_index(-1)
            , queue_size(1000)
            , accumulate_ms(100) {}
    };

    explicit testable_mic_capture()
        : testable_mic_capture(config{}) {}

    explicit testable_mic_capture(const config& cfg)
        : m_config(cfg)
        , m_audio_queue(cfg.queue_size)
        , m_frames_to_accumulate((cfg.sample_rate * cfg.accumulate_ms) / 1000) {

        m_accumulation_buffer.reserve(m_frames_to_accumulate * m_config.channels);
    }

    ~testable_mic_capture() {
        stop();
    }

    bool start() {
        if (m_running) return true;

        // Simulate successful PortAudio initialization
        m_running = true;
        m_dropped_frames = 0;
        m_accumulated_frames = 0;
        m_accumulation_buffer.clear();

        if (m_callback) {
            m_processing_thread = std::thread(&testable_mic_capture::processing_loop, this);
        }

        return true;
    }

    void stop() {
        if (!m_running) return;

        m_running = false;
        m_data_cv.notify_all();

        if (m_processing_thread.joinable()) {
            m_processing_thread.join();
        }

        // Clear queue
        std::vector<int16_t> discard;
        while (m_audio_queue.try_dequeue(discard)) {}
    }

    void set_audio_callback(audio_callback_t callback) {
        m_callback = std::move(callback);
    }

    bool dequeue_audio(std::vector<int16_t>& samples) {
        return m_audio_queue.try_dequeue(samples);
    }

    bool is_running() const { return m_running; }
    size_t get_dropped_frames() const { return m_dropped_frames; }

    // Test helper methods
    void simulate_audio_input(const std::vector<int16_t>& audio_data) {
        std::lock_guard<std::mutex> lock(m_simulation_mutex);

        if (!m_running) return;

        const size_t frames = audio_data.size() / m_config.channels;

        // Simulate the PortAudio callback behavior
        m_accumulation_buffer.insert(m_accumulation_buffer.end(),
                                     audio_data.begin(), audio_data.end());
        m_accumulated_frames += frames;

        // Check if we have enough frames
        if (m_accumulated_frames >= m_frames_to_accumulate) {
            // Move accumulated audio to queue
            std::vector<int16_t> audio_to_queue = std::move(m_accumulation_buffer);
            if (!m_audio_queue.try_enqueue(std::move(audio_to_queue))) {
                m_dropped_frames.fetch_add(m_accumulated_frames);
            } else {
                // Notify the processing thread
                m_data_cv.notify_one();
            }

            // Reset accumulation
            m_accumulation_buffer.clear();
            m_accumulation_buffer.reserve(m_frames_to_accumulate * m_config.channels);
            m_accumulated_frames = 0;
        }
    }

    void force_queue_full() {
        // Fill the queue to test overflow behavior
        std::vector<int16_t> dummy_audio(1600, 1000);
        while (m_audio_queue.try_enqueue(dummy_audio)) {
            // Keep filling until full
        }
    }

    size_t get_queue_size() const {
        return m_audio_queue.size_approx();
    }

    const config& get_config() const { return m_config; }

private:
    config m_config;
    moodycamel::ConcurrentQueue<std::vector<int16_t>> m_audio_queue;
    audio_callback_t m_callback;
    std::thread m_processing_thread;
    std::atomic<bool> m_running{false};
    std::atomic<size_t> m_dropped_frames{0};
    std::vector<int16_t> m_accumulation_buffer;
    size_t m_accumulated_frames = 0;
    size_t m_frames_to_accumulate;
    std::condition_variable m_data_cv;
    std::mutex m_data_mutex;
    mutable std::mutex m_simulation_mutex;

    void processing_loop() {
        std::vector<int16_t> audio_data;

        while (m_running) {
            if (m_audio_queue.try_dequeue(audio_data)) {
                if (m_callback) {
                    m_callback(audio_data);
                }
            } else {
                std::unique_lock<std::mutex> lock(m_data_mutex);
                m_data_cv.wait_for(lock, std::chrono::milliseconds(10),
                                   [this] { return !m_running || m_audio_queue.size_approx() > 0; });
            }
        }
    }
};

class MicCaptureTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset any test state
        callback_count = 0;
        last_audio_size = 0;
        received_audio.clear();
    }

    void TearDown() override {
        mic.reset();
    }

    std::unique_ptr<testable_mic_capture> mic;

    // Test helpers
    std::atomic<int> callback_count{0};
    std::atomic<size_t> last_audio_size{0};
    std::vector<std::vector<int16_t>> received_audio;
    std::mutex received_audio_mutex;

    void audio_callback(const std::vector<int16_t>& audio) {
        callback_count++;
        last_audio_size = audio.size();

        std::lock_guard<std::mutex> lock(received_audio_mutex);
        received_audio.push_back(audio);
    }

    std::vector<int16_t> create_audio_data(size_t samples, int16_t value = 1000) {
        return std::vector<int16_t>(samples, value);
    }
};

// Test default configuration
TEST_F(MicCaptureTest, DefaultConfiguration) {
    mic = std::make_unique<testable_mic_capture>();

    const auto& cfg = mic->get_config();
    EXPECT_EQ(cfg.sample_rate, 16000);
    EXPECT_EQ(cfg.channels, 1);
    EXPECT_EQ(cfg.frames_per_buffer, 160);
    EXPECT_EQ(cfg.device_index, -1);
    EXPECT_EQ(cfg.queue_size, 1000);
    EXPECT_EQ(cfg.accumulate_ms, 100);
}

// Test custom configuration
TEST_F(MicCaptureTest, CustomConfiguration) {
    testable_mic_capture::config cfg;
    cfg.sample_rate = 48000;
    cfg.channels = 2;
    cfg.accumulate_ms = 50;
    cfg.queue_size = 500;

    mic = std::make_unique<testable_mic_capture>(cfg);

    const auto& actual_cfg = mic->get_config();
    EXPECT_EQ(actual_cfg.sample_rate, 48000);
    EXPECT_EQ(actual_cfg.channels, 2);
    EXPECT_EQ(actual_cfg.accumulate_ms, 50);
    EXPECT_EQ(actual_cfg.queue_size, 500);
}

// Test basic start/stop functionality
TEST_F(MicCaptureTest, StartStop) {
    mic = std::make_unique<testable_mic_capture>();

    EXPECT_FALSE(mic->is_running());

    EXPECT_TRUE(mic->start());
    EXPECT_TRUE(mic->is_running());

    // Starting again should be safe
    EXPECT_TRUE(mic->start());
    EXPECT_TRUE(mic->is_running());

    mic->stop();
    EXPECT_FALSE(mic->is_running());

    // Stopping again should be safe
    mic->stop();
    EXPECT_FALSE(mic->is_running());
}

// Test audio callback functionality
TEST_F(MicCaptureTest, AudioCallback) {
    mic = std::make_unique<testable_mic_capture>();

    mic->set_audio_callback([this](const std::vector<int16_t>& audio) {
        audio_callback(audio);
    });

    EXPECT_TRUE(mic->start());

    // Simulate audio input - need enough data to trigger accumulation
    auto audio_chunk = create_audio_data(1600); // 100ms at 16kHz
    mic->simulate_audio_input(audio_chunk);

    // Wait for callback
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_GT(callback_count.load(), 0);
    EXPECT_EQ(last_audio_size.load(), 1600);
}

// Test audio accumulation
TEST_F(MicCaptureTest, AudioAccumulation) {
    testable_mic_capture::config cfg;
    cfg.accumulate_ms = 50; // 50ms accumulation
    cfg.sample_rate = 16000;

    mic = std::make_unique<testable_mic_capture>(cfg);

    mic->set_audio_callback([this](const std::vector<int16_t>& audio) {
        audio_callback(audio);
    });

    EXPECT_TRUE(mic->start());

    // Send smaller chunks that should accumulate
    auto small_chunk = create_audio_data(400, 1000); // 25ms at 16kHz

    // First chunk - shouldn't trigger callback yet
    mic->simulate_audio_input(small_chunk);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(callback_count.load(), 0);

    // Second chunk - should trigger callback (50ms total)
    mic->simulate_audio_input(small_chunk);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(callback_count.load(), 1);
    EXPECT_EQ(last_audio_size.load(), 800); // 25ms + 25ms = 50ms = 800 samples
}

// Test multiple callbacks
TEST_F(MicCaptureTest, MultipleCallbacks) {
    mic = std::make_unique<testable_mic_capture>();

    mic->set_audio_callback([this](const std::vector<int16_t>& audio) {
        audio_callback(audio);
    });

    EXPECT_TRUE(mic->start());

    // Send multiple chunks
    auto audio_chunk = create_audio_data(1600);

    for (int i = 0; i < 5; ++i) {
        mic->simulate_audio_input(audio_chunk);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(callback_count.load(), 5);
}

// Test manual dequeue without callback
TEST_F(MicCaptureTest, ManualDequeue) {
    mic = std::make_unique<testable_mic_capture>();

    EXPECT_TRUE(mic->start());

    // Simulate audio input
    auto audio_chunk = create_audio_data(1600);
    mic->simulate_audio_input(audio_chunk);

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Manually dequeue
    std::vector<int16_t> dequeued_audio;
    EXPECT_TRUE(mic->dequeue_audio(dequeued_audio));
    EXPECT_EQ(dequeued_audio.size(), 1600);

    // Second dequeue should return false (empty queue)
    std::vector<int16_t> empty_audio;
    EXPECT_FALSE(mic->dequeue_audio(empty_audio));
}

// Test queue overflow and dropped frames
TEST_F(MicCaptureTest, QueueOverflow) {
    testable_mic_capture::config cfg;
    cfg.queue_size = 5; // Very small queue

    mic = std::make_unique<testable_mic_capture>(cfg);

    EXPECT_TRUE(mic->start());

    // Fill the queue first
    mic->force_queue_full();

    // Now simulate more audio input
    auto audio_chunk = create_audio_data(1600);
    mic->simulate_audio_input(audio_chunk);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Should have dropped frames
    EXPECT_GT(mic->get_dropped_frames(), 0);
}

// Test stereo audio
TEST_F(MicCaptureTest, StereoAudio) {
    testable_mic_capture::config cfg;
    cfg.channels = 2;

    mic = std::make_unique<testable_mic_capture>(cfg);

    mic->set_audio_callback([this](const std::vector<int16_t>& audio) {
        audio_callback(audio);
    });

    EXPECT_TRUE(mic->start());

    // Simulate stereo audio (interleaved L/R)
    auto stereo_chunk = create_audio_data(3200); // 100ms stereo at 16kHz
    mic->simulate_audio_input(stereo_chunk);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_EQ(callback_count.load(), 1);
    EXPECT_EQ(last_audio_size.load(), 3200);
}

// Test different sample rates
TEST_F(MicCaptureTest, DifferentSampleRates) {
    testable_mic_capture::config cfg;
    cfg.sample_rate = 48000;
    cfg.accumulate_ms = 100;

    mic = std::make_unique<testable_mic_capture>(cfg);

    mic->set_audio_callback([this](const std::vector<int16_t>& audio) {
        audio_callback(audio);
    });

    EXPECT_TRUE(mic->start());

    // 100ms at 48kHz = 4800 samples
    auto audio_chunk = create_audio_data(4800);
    mic->simulate_audio_input(audio_chunk);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_EQ(callback_count.load(), 1);
    EXPECT_EQ(last_audio_size.load(), 4800);
}

// Test callback replacement
TEST_F(MicCaptureTest, CallbackReplacement) {
    mic = std::make_unique<testable_mic_capture>();

    std::atomic<int> first_callback_count{0};
    std::atomic<int> second_callback_count{0};

    // Set first callback
    mic->set_audio_callback([&first_callback_count](const std::vector<int16_t>&) {
        first_callback_count++;
    });

    EXPECT_TRUE(mic->start());

    auto audio_chunk = create_audio_data(1600);
    mic->simulate_audio_input(audio_chunk);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_EQ(first_callback_count.load(), 1);
    EXPECT_EQ(second_callback_count.load(), 0);

    // Replace callback
    mic->set_audio_callback([&second_callback_count](const std::vector<int16_t>&) {
        second_callback_count++;
    });

    mic->simulate_audio_input(audio_chunk);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_EQ(first_callback_count.load(), 1);
    EXPECT_EQ(second_callback_count.load(), 1);
}

// Test no callback set
TEST_F(MicCaptureTest, NoCallbackSet) {
    mic = std::make_unique<testable_mic_capture>();

    EXPECT_TRUE(mic->start());

    auto audio_chunk = create_audio_data(1600);
    mic->simulate_audio_input(audio_chunk);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Should still be able to dequeue manually
    std::vector<int16_t> dequeued_audio;
    EXPECT_TRUE(mic->dequeue_audio(dequeued_audio));
    EXPECT_EQ(dequeued_audio.size(), 1600);
}

// Test thread safety
TEST_F(MicCaptureTest, ThreadSafety) {
    mic = std::make_unique<testable_mic_capture>();

    std::atomic<int> total_callbacks{0};
    mic->set_audio_callback([&total_callbacks](const std::vector<int16_t>&) {
        total_callbacks++;
    });

    EXPECT_TRUE(mic->start());

    // Simulate audio from multiple threads
    std::vector<std::thread> threads;
    const int num_threads = 4;
    const int chunks_per_thread = 10;

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([this, chunks_per_thread]() {
            auto audio_chunk = create_audio_data(1600);
            for (int j = 0; j < chunks_per_thread; ++j) {
                mic->simulate_audio_input(audio_chunk);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Should have processed all chunks
    EXPECT_EQ(total_callbacks.load(), num_threads * chunks_per_thread);
}

// Test edge cases
TEST_F(MicCaptureTest, EmptyAudioInput) {
    mic = std::make_unique<testable_mic_capture>();

    mic->set_audio_callback([this](const std::vector<int16_t>& audio) {
        audio_callback(audio);
    });

    EXPECT_TRUE(mic->start());

    // Simulate empty audio input
    std::vector<int16_t> empty_audio;
    mic->simulate_audio_input(empty_audio);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // No callback should be triggered
    EXPECT_EQ(callback_count.load(), 0);
}

// Test very small accumulation times
TEST_F(MicCaptureTest, SmallAccumulationTime) {
    testable_mic_capture::config cfg;
    cfg.accumulate_ms = 10; // Very small accumulation

    mic = std::make_unique<testable_mic_capture>(cfg);

    mic->set_audio_callback([this](const std::vector<int16_t>& audio) {
        audio_callback(audio);
    });

    EXPECT_TRUE(mic->start());

    // 10ms at 16kHz = 160 samples
    auto small_chunk = create_audio_data(160);
    mic->simulate_audio_input(small_chunk);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_EQ(callback_count.load(), 1);
    EXPECT_EQ(last_audio_size.load(), 160);
}

// Test restart functionality
TEST_F(MicCaptureTest, RestartCapture) {
    mic = std::make_unique<testable_mic_capture>();

    mic->set_audio_callback([this](const std::vector<int16_t>& audio) {
        audio_callback(audio);
    });

    // First session
    EXPECT_TRUE(mic->start());
    auto audio_chunk = create_audio_data(1600);
    mic->simulate_audio_input(audio_chunk);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(callback_count.load(), 1);

    // Stop and restart
    mic->stop();
    EXPECT_FALSE(mic->is_running());

    callback_count = 0; // Reset counter
    EXPECT_TRUE(mic->start());
    mic->simulate_audio_input(audio_chunk);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(callback_count.load(), 1);
}

// Performance test (disabled by default)
TEST_F(MicCaptureTest, DISABLED_PerformanceBenchmark) {
    mic = std::make_unique<testable_mic_capture>();

    std::atomic<int> processed_chunks{0};
    mic->set_audio_callback([&processed_chunks](const std::vector<int16_t>&) {
        processed_chunks++;
    });

    EXPECT_TRUE(mic->start());

    auto start = std::chrono::high_resolution_clock::now();

    const int num_chunks = 1000;
    auto audio_chunk = create_audio_data(1600);

    for (int i = 0; i < num_chunks; ++i) {
        mic->simulate_audio_input(audio_chunk);
    }

    // Wait for all processing to complete
    while (processed_chunks.load() < num_chunks) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    std::cout << "Processed " << num_chunks << " audio chunks in "
              << duration.count() << " microseconds" << std::endl;
    std::cout << "Average: " << duration.count() / num_chunks << " us/chunk" << std::endl;
}
