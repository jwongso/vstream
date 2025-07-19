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
#include "audio_processor.h"
#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>

using ::testing::_;
using ::testing::Return;
using ::testing::DoAll;
using ::testing::SetArgReferee;
using ::testing::AtLeast;

// Forward declarations for the actual classes
class vstream_engine;
class hyni_websocket_server;
class vad_with_hangover;

// Mock interfaces
class mock_vstream_engine {
public:
    MOCK_METHOD(std::string, process_audio, (const std::vector<int16_t>& audio, bool finalize));
    MOCK_METHOD(void, reset, ());
    MOCK_METHOD(bool, has_partial_enabled, (), (const));
    MOCK_METHOD(size_t, get_total_samples_processed, (), (const));
};

class mock_websocket_server {
public:
    MOCK_METHOD(void, queue_transcription, (const std::string& text, const std::string& session_id, float confidence));
    MOCK_METHOD(size_t, get_client_count, (), (const));
};

class mock_vad {
public:
    MOCK_METHOD(bool, process, (const std::vector<int16_t>& audio));
};

// Create a testable version of audio_processor that doesn't call the base constructor
class testable_audio_processor {
public:
    testable_audio_processor(mock_vstream_engine* engine,
                             mock_websocket_server* server,
                             mock_vad* vad,
                             int silence_frames_threshold = 2,
                             bool use_vad = true,
                             int finalize_interval_ms = 2000,
                             int buffer_ms = 100)
        : m_mock_engine(engine)
        , m_mock_server(server)
        , m_mock_vad(vad)
        , m_session_id("mic-capture")
        , m_silence_frames_threshold(silence_frames_threshold)
        , m_use_vad(use_vad)
        , m_finalize_interval_ms(finalize_interval_ms)
        , m_buffer_ms(buffer_ms)
        , m_last_finalize_time(std::chrono::steady_clock::now())
        , m_last_debug_time(std::chrono::steady_clock::now()) {

        m_show_partial = engine->has_partial_enabled();

        // Pre-allocate string buffers
        m_result_buffer.reserve(1024);
        m_last_final_text.reserve(256);
        m_last_partial_text.reserve(256);
    }

    void process_audio(const std::vector<int16_t>& audio) {
        // Check if this is speech (always true if VAD is disabled)
        bool is_speech = m_use_vad ? m_mock_vad->process(audio) : true;

        auto now = std::chrono::steady_clock::now();

        if (is_speech) {
            m_was_speaking = true;
            m_silence_frames = 0;

            // Process with mock engine
            m_result_buffer = m_mock_engine->process_audio(audio, false);
            handle_speech_result(m_result_buffer);

            // Add periodic finalization even during speech for more frequent results
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                               now - m_last_finalize_time).count();

            if (elapsed >= m_finalize_interval_ms) {
                force_finalize();
                m_last_finalize_time = now;
            }
        } else {
            // Silence detected (only happens when VAD is enabled)
            if (m_was_speaking) {
                m_silence_frames++;

                if (m_silence_frames >= m_silence_frames_threshold) {
                    force_finalize();
                }
            }
        }
    }

private:
    void handle_speech_result(const std::string& result_json) {
        try {
            auto result = nlohmann::json::parse(result_json);

            // Handle final result
            if (result.contains("text") && !result["text"].is_null()) {
                std::string text = result["text"].get<std::string>();
                if (!text.empty() && text != m_last_final_text) {
                    handle_final_result(text);
                }
            }
            // Handle partial result
            else if (m_show_partial && result.contains("partial") && !result["partial"].is_null()) {
                std::string partial = result["partial"].get<std::string>();
                if (!partial.empty() && partial != m_last_partial_text) {
                    handle_partial_result(partial);
                }
            }
        } catch (const std::exception& e) {
            // Error handling
        }
    }

    void handle_final_result(const std::string& text) {
        m_last_final_text = text;
        m_mock_server->queue_transcription(text, m_session_id, 1.0f);
        m_last_finalize_time = std::chrono::steady_clock::now();
    }

    void handle_partial_result(const std::string& partial) {
        m_last_partial_text = partial;
    }

    void force_finalize() {
        // Force final result
        m_result_buffer = m_mock_engine->process_audio({}, true);

        try {
            auto result = nlohmann::json::parse(m_result_buffer);
            if (result.contains("text") && !result["text"].is_null()) {
                std::string text = result["text"].get<std::string>();
                if (!text.empty() && text != m_last_final_text) {
                    handle_final_result(text);
                }
            }
        } catch (const std::exception& e) {
            // Error handling
        }

        // Reset recognizer for next utterance
        m_mock_engine->reset();
        m_was_speaking = false;
        m_silence_frames = 0;
        m_last_partial_text.clear();

        // Reset finalize timer
        m_last_finalize_time = std::chrono::steady_clock::now();
    }

    // Member variables (same as audio_processor)
    mock_vstream_engine* m_mock_engine;
    mock_websocket_server* m_mock_server;
    mock_vad* m_mock_vad;

    std::string m_session_id;
    bool m_show_partial;
    int m_silence_frames_threshold;
    bool m_use_vad;
    int m_finalize_interval_ms;
    int m_buffer_ms;

    std::chrono::steady_clock::time_point m_last_finalize_time;
    std::chrono::steady_clock::time_point m_last_debug_time;

    // State tracking
    bool m_was_speaking = false;
    int m_silence_frames = 0;
    std::string m_last_final_text;
    std::string m_last_partial_text;

    // Performance optimization
    std::string m_result_buffer;
};

class AudioProcessorTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<mock_vstream_engine>();
        server = std::make_unique<mock_websocket_server>();
        vad = std::make_unique<mock_vad>();
    }

    void TearDown() override {
        processor.reset();
        vad.reset();
        server.reset();
        engine.reset();
    }

    std::unique_ptr<mock_vstream_engine> engine;
    std::unique_ptr<mock_websocket_server> server;
    std::unique_ptr<mock_vad> vad;
    std::unique_ptr<testable_audio_processor> processor;

    // Helper to create test audio data
    std::vector<int16_t> create_audio_data(size_t samples, int16_t value = 1000) {
        return std::vector<int16_t>(samples, value);
    }
};

// Test basic initialization
TEST_F(AudioProcessorTest, InitializationWithVAD) {
    EXPECT_CALL(*engine, has_partial_enabled()).WillOnce(Return(true));

    processor = std::make_unique<testable_audio_processor>(
        engine.get(), server.get(), vad.get(),
        3, true, 2000, 100
        );

    EXPECT_NE(processor, nullptr);
}

TEST_F(AudioProcessorTest, InitializationWithoutVAD) {
    EXPECT_CALL(*engine, has_partial_enabled()).WillOnce(Return(false));

    processor = std::make_unique<testable_audio_processor>(
        engine.get(), server.get(), vad.get(),
        3, false, 2000, 100
        );

    EXPECT_NE(processor, nullptr);
}

// Test speech processing with final results
TEST_F(AudioProcessorTest, ProcessSpeechWithFinalResult) {
    EXPECT_CALL(*engine, has_partial_enabled()).WillOnce(Return(true));
    EXPECT_CALL(*vad, process(_)).WillRepeatedly(Return(true)); // Always speech

    processor = std::make_unique<testable_audio_processor>(
        engine.get(), server.get(), vad.get(),
        3, true, 2000, 100
        );

    auto audio = create_audio_data(1600);

    // Mock engine returning final result
    std::string final_json = R"({"text": "hello world"})";
    EXPECT_CALL(*engine, process_audio(audio, false))
        .WillOnce(Return(final_json));

    EXPECT_CALL(*server, queue_transcription("hello world", "mic-capture", 1.0f))
        .Times(1);

    processor->process_audio(audio);
}

// Test speech processing with partial results
TEST_F(AudioProcessorTest, ProcessSpeechWithPartialResult) {
    EXPECT_CALL(*engine, has_partial_enabled()).WillOnce(Return(true));
    EXPECT_CALL(*vad, process(_)).WillRepeatedly(Return(true));

    processor = std::make_unique<testable_audio_processor>(
        engine.get(), server.get(), vad.get(),
        3, true, 2000, 100
        );

    auto audio = create_audio_data(1600);

    // Mock engine returning partial result
    std::string partial_json = R"({"partial": "hello"})";
    EXPECT_CALL(*engine, process_audio(audio, false))
        .WillOnce(Return(partial_json));

    // Partial results shouldn't be queued to server
    EXPECT_CALL(*server, queue_transcription(_, _, _)).Times(0);

    processor->process_audio(audio);
}

// Test silence detection and finalization
TEST_F(AudioProcessorTest, SilenceTriggersFinalization) {
    EXPECT_CALL(*engine, has_partial_enabled()).WillOnce(Return(true));

    processor = std::make_unique<testable_audio_processor>(
        engine.get(), server.get(), vad.get(),
        2, true, 2000, 100  // 2 frames of silence = 200ms
        );

    auto audio = create_audio_data(1600);

    // First, process speech
    EXPECT_CALL(*vad, process(_)).WillOnce(Return(true));
    std::string speech_json = R"({"partial": "hello"})";
    EXPECT_CALL(*engine, process_audio(audio, false))
        .WillOnce(Return(speech_json));

    processor->process_audio(audio);

    // Then process silence frames
    EXPECT_CALL(*vad, process(_)).WillRepeatedly(Return(false));

    // On the second silence frame, should trigger finalization
    EXPECT_CALL(*engine, process_audio(std::vector<int16_t>{}, true))
        .WillOnce(Return(R"({"text": "hello world"})"));
    EXPECT_CALL(*engine, reset()).Times(1);
    EXPECT_CALL(*server, queue_transcription("hello world", "mic-capture", 1.0f))
        .Times(1);

    // Process 2 silence frames
    processor->process_audio(audio);
    processor->process_audio(audio);
}

// Test periodic finalization during continuous speech
TEST_F(AudioProcessorTest, PeriodicFinalizationDuringSpeech) {
    EXPECT_CALL(*engine, has_partial_enabled()).WillOnce(Return(true));
    EXPECT_CALL(*vad, process(_)).WillRepeatedly(Return(true)); // Always speech

    processor = std::make_unique<testable_audio_processor>(
        engine.get(), server.get(), vad.get(),
        3, true, 100, 50  // Finalize every 100ms, 50ms buffer
        );

    auto audio = create_audio_data(800); // 50ms of audio at 16kHz

    // First few calls return partial
    EXPECT_CALL(*engine, process_audio(audio, false))
        .WillOnce(Return(R"({"partial": "hello"})"))
        .WillOnce(Return(R"({"partial": "hello world"})"))
        .WillOnce(Return(R"({"partial": "hello world test"})"));

    // After 100ms, should force finalization
    EXPECT_CALL(*engine, process_audio(std::vector<int16_t>{}, true))
        .WillOnce(Return(R"({"text": "hello world test"})"));
    EXPECT_CALL(*engine, reset()).Times(1);
    EXPECT_CALL(*server, queue_transcription("hello world test", "mic-capture", 1.0f))
        .Times(1);

    // Process 3 frames (150ms)
    processor->process_audio(audio);
    processor->process_audio(audio);

    // Sleep to ensure time passes
    std::this_thread::sleep_for(std::chrono::milliseconds(101));

    processor->process_audio(audio);
}

// Test no VAD mode
TEST_F(AudioProcessorTest, NoVADMode) {
    EXPECT_CALL(*engine, has_partial_enabled()).WillOnce(Return(true));

    processor = std::make_unique<testable_audio_processor>(
        engine.get(), server.get(), vad.get(),
        3, false, 100, 50  // VAD disabled
        );

    auto audio = create_audio_data(800);

    // VAD should never be called
    EXPECT_CALL(*vad, process(_)).Times(0);

    // Process audio normally
    EXPECT_CALL(*engine, process_audio(audio, false))
        .WillRepeatedly(Return(R"({"partial": "test"})"));

    processor->process_audio(audio);
}

// Test duplicate text filtering
TEST_F(AudioProcessorTest, DuplicateTextFiltering) {
    EXPECT_CALL(*engine, has_partial_enabled()).WillOnce(Return(true));
    EXPECT_CALL(*vad, process(_)).WillRepeatedly(Return(true));

    processor = std::make_unique<testable_audio_processor>(
        engine.get(), server.get(), vad.get()
        );

    auto audio = create_audio_data(1600);

    // Return same text multiple times
    std::string same_json = R"({"text": "duplicate text"})";
    EXPECT_CALL(*engine, process_audio(audio, false))
        .WillOnce(Return(same_json))
        .WillOnce(Return(same_json))
        .WillOnce(Return(same_json));

    // Should only queue once
    EXPECT_CALL(*server, queue_transcription("duplicate text", "mic-capture", 1.0f))
        .Times(1);

    processor->process_audio(audio);
    processor->process_audio(audio);
    processor->process_audio(audio);
}

// Test error handling for invalid JSON
TEST_F(AudioProcessorTest, InvalidJSONHandling) {
    EXPECT_CALL(*engine, has_partial_enabled()).WillOnce(Return(true));
    EXPECT_CALL(*vad, process(_)).WillRepeatedly(Return(true));

    processor = std::make_unique<testable_audio_processor>(
        engine.get(), server.get(), vad.get()
        );

    auto audio = create_audio_data(1600);

    // Return invalid JSON
    EXPECT_CALL(*engine, process_audio(audio, false))
        .WillOnce(Return("invalid json {"));

    // Should not crash, and no transcription should be queued
    EXPECT_CALL(*server, queue_transcription(_, _, _)).Times(0);

    EXPECT_NO_THROW(processor->process_audio(audio));
}

// Test empty audio handling
TEST_F(AudioProcessorTest, EmptyAudioHandling) {
    EXPECT_CALL(*engine, has_partial_enabled()).WillOnce(Return(true));

    processor = std::make_unique<testable_audio_processor>(
        engine.get(), server.get(), vad.get()
        );

    std::vector<int16_t> empty_audio;

    // VAD should be called with empty audio if VAD is enabled
    EXPECT_CALL(*vad, process(empty_audio)).WillOnce(Return(false));

    // Should handle gracefully
    EXPECT_NO_THROW(processor->process_audio(empty_audio));
}

// Test state transitions
TEST_F(AudioProcessorTest, StateTransitions) {
    EXPECT_CALL(*engine, has_partial_enabled()).WillOnce(Return(true));

    processor = std::make_unique<testable_audio_processor>(
        engine.get(), server.get(), vad.get(),
        2, true, 2000, 100
        );

    auto audio = create_audio_data(1600);

    // Speech -> Silence -> Speech pattern
    ::testing::InSequence seq;

    // First speech
    EXPECT_CALL(*vad, process(_)).WillOnce(Return(true));
    EXPECT_CALL(*engine, process_audio(audio, false))
        .WillOnce(Return(R"({"partial": "first"})"));

    // Silence (not enough to trigger finalization)
    EXPECT_CALL(*vad, process(_)).WillOnce(Return(false));

    // Speech again
    EXPECT_CALL(*vad, process(_)).WillOnce(Return(true));
    EXPECT_CALL(*engine, process_audio(audio, false))
        .WillOnce(Return(R"({"partial": "first second"})"));

    processor->process_audio(audio);
    processor->process_audio(audio);
    processor->process_audio(audio);
}
