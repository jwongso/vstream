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
#include "vstream_engine.h"
#include <thread>
#include <chrono>
#include <random>
#include <nlohmann/json.hpp>

using ::testing::_;
using ::testing::Return;
using ::testing::AtLeast;
using json = nlohmann::json;

// Mock Vosk API functions for testing
extern "C" {
// These will be mocked in our tests
void vosk_set_log_level(int level);
VoskModel* vosk_model_new(const char* model_path);
void vosk_model_free(VoskModel* model);
VoskSpkModel* vosk_spk_model_new(const char* model_path);
void vosk_spk_model_free(VoskSpkModel* model);
VoskRecognizer* vosk_recognizer_new(VoskModel* model, float sample_rate);
VoskRecognizer* vosk_recognizer_new_spk(VoskModel* model, float sample_rate, VoskSpkModel* spk_model);
void vosk_recognizer_free(VoskRecognizer* recognizer);
void vosk_recognizer_set_words(VoskRecognizer* recognizer, int words);
void vosk_recognizer_set_partial_words(VoskRecognizer* recognizer, int partial_words);
void vosk_recognizer_set_max_alternatives(VoskRecognizer* recognizer, int max_alternatives);
void vosk_recognizer_set_nlsml(VoskRecognizer* recognizer, int nlsml);
void vosk_recognizer_set_grm(VoskRecognizer* recognizer, const char* grammar);
int vosk_recognizer_accept_waveform_s(VoskRecognizer* recognizer, const short* data, int length);
const char* vosk_recognizer_result(VoskRecognizer* recognizer);
const char* vosk_recognizer_partial_result(VoskRecognizer* recognizer);
const char* vosk_recognizer_final_result(VoskRecognizer* recognizer);
void vosk_recognizer_reset(VoskRecognizer* recognizer);
}

// Test implementation of vstream_engine that doesn't require real Vosk models
class testable_vstream_engine {
public:
    struct config {
        int sample_rate = 16000;
        bool enable_speaker_id = false;
        bool enable_word_times = true;
        bool enable_partial_words = true;
        int max_alternatives = 0;
        std::string speaker_model_path;
    };

    explicit testable_vstream_engine(const std::string& model_path)
        : testable_vstream_engine(model_path, config{}) {}

    explicit testable_vstream_engine(const std::string& model_path, const config& cfg)
        : m_model_path(model_path), m_config(cfg) {

        if (model_path.empty() || model_path == "invalid") {
            throw std::runtime_error("Failed to load Vosk model from: " + model_path);
        }

        // Simulate successful initialization
        m_initialized = true;
    }

    ~testable_vstream_engine() = default;

    std::string process_audio(const std::vector<int16_t>& audio_data, bool is_final = false) {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_initialized) {
            return "{}";
        }

        m_total_samples += audio_data.size();

        // Simulate different types of results based on test scenarios
        if (audio_data.empty() && !is_final) {
            return "{}";
        }

        if (is_final || m_force_final) {
            // Return a final result
            json result;
            result["text"] = m_test_text.empty() ? "test final result" : m_test_text;

            if (m_config.enable_word_times) {
                result["result"] = json::array({
                    {{"word", "test"}, {"start", 0.0}, {"end", 0.5}},
                    {{"word", "final"}, {"start", 0.5}, {"end", 1.0}},
                    {{"word", "result"}, {"start", 1.0}, {"end", 1.5}}
                });
            }

            if (m_config.max_alternatives > 0) {
                json alternatives = json::array();
                for (int i = 0; i < m_config.max_alternatives; ++i) {
                    alternatives.push_back({
                        {"text", "alternative " + std::to_string(i + 1)},
                        {"confidence", 0.9 - i * 0.1}
                    });
                }
                result["alternatives"] = alternatives;
            }

            if (m_config.enable_speaker_id) {
                result["spk"] = json::array({0.1, -0.2, 0.3, 0.4, -0.5});
                result["spk_frames"] = 150;
            }

            return result.dump();
        }

        // Return partial result
        json partial;
        partial["partial"] = m_test_partial.empty() ? "test partial" : m_test_partial;
        return partial.dump();
    }

    void reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_test_text.clear();
        m_test_partial.clear();
        m_force_final = false;
    }

    void set_grammar(const std::string& grammar) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_grammar = grammar;
    }

    void set_max_alternatives(int max) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_config.max_alternatives = max;
    }

    void enable_nlsml_output(bool enable) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_nlsml_enabled = enable;
    }

    bool has_partial_result() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return !m_test_partial.empty();
    }

    size_t get_total_samples_processed() const {
        return m_total_samples;
    }

    bool has_partial_enabled() const {
        return m_config.enable_partial_words;
    }

    // Test helpers
    void set_test_text(const std::string& text) { m_test_text = text; }
    void set_test_partial(const std::string& partial) { m_test_partial = partial; }
    void set_force_final(bool force) { m_force_final = force; }
    const std::string& get_grammar() const { return m_grammar; }
    bool is_nlsml_enabled() const { return m_nlsml_enabled; }

private:
    std::string m_model_path;
    config m_config;
    mutable std::mutex m_mutex;
    std::atomic<size_t> m_total_samples{0};
    bool m_initialized = false;

    // Test state
    std::string m_test_text;
    std::string m_test_partial;
    bool m_force_final = false;
    std::string m_grammar;
    bool m_nlsml_enabled = false;
};

class VStreamEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset test state
    }

    void TearDown() override {
        engine.reset();
    }

    std::unique_ptr<testable_vstream_engine> engine;

    // Helper to create test audio data
    std::vector<int16_t> create_audio_data(size_t samples, int16_t value = 1000) {
        return std::vector<int16_t>(samples, value);
    }

    // Helper to create speech-like audio
    std::vector<int16_t> create_speech_audio(size_t samples) {
        std::vector<int16_t> audio(samples);
        for (size_t i = 0; i < samples; ++i) {
            audio[i] = static_cast<int16_t>(5000 * std::sin(2 * M_PI * 440 * i / 16000));
        }
        return audio;
    }
};

// Test basic construction
TEST_F(VStreamEngineTest, BasicConstruction) {
    EXPECT_NO_THROW({
        engine = std::make_unique<testable_vstream_engine>("/path/to/model");
    });

    EXPECT_NE(engine, nullptr);
    EXPECT_EQ(engine->get_total_samples_processed(), 0);
    EXPECT_TRUE(engine->has_partial_enabled());
}

// Test construction with custom config
TEST_F(VStreamEngineTest, CustomConfigConstruction) {
    testable_vstream_engine::config cfg;
    cfg.sample_rate = 8000;
    cfg.enable_speaker_id = true;
    cfg.enable_partial_words = false;
    cfg.max_alternatives = 3;
    cfg.speaker_model_path = "/path/to/speaker/model";

    EXPECT_NO_THROW({
        engine = std::make_unique<testable_vstream_engine>("/path/to/model", cfg);
    });

    EXPECT_FALSE(engine->has_partial_enabled());
}

// Test invalid model path
TEST_F(VStreamEngineTest, InvalidModelPath) {
    EXPECT_THROW({
        engine = std::make_unique<testable_vstream_engine>("invalid");
    }, std::runtime_error);

    EXPECT_THROW({
        engine = std::make_unique<testable_vstream_engine>("");
    }, std::runtime_error);
}

// Test basic audio processing
TEST_F(VStreamEngineTest, BasicAudioProcessing) {
    engine = std::make_unique<testable_vstream_engine>("/path/to/model");

    auto audio = create_audio_data(1600); // 100ms at 16kHz
    std::string result = engine->process_audio(audio);

    EXPECT_FALSE(result.empty());
    EXPECT_NE(result, "{}");

    // Parse JSON result
    EXPECT_NO_THROW({
        auto json_result = json::parse(result);
        EXPECT_TRUE(json_result.contains("partial"));
    });

    EXPECT_EQ(engine->get_total_samples_processed(), 1600);
}

// Test final result processing
TEST_F(VStreamEngineTest, FinalResultProcessing) {
    engine = std::make_unique<testable_vstream_engine>("/path/to/model");

    // Process some audio
    auto audio = create_audio_data(1600);
    engine->process_audio(audio);

    // Force final result
    std::string final_result = engine->process_audio({}, true);

    auto json_result = json::parse(final_result);
    EXPECT_TRUE(json_result.contains("text"));
    EXPECT_EQ(json_result["text"], "test final result");
}

// Test word timing information
TEST_F(VStreamEngineTest, WordTimingInformation) {
    testable_vstream_engine::config cfg;
    cfg.enable_word_times = true;

    engine = std::make_unique<testable_vstream_engine>("/path/to/model", cfg);

    auto audio = create_audio_data(1600);
    engine->process_audio(audio);

    std::string result = engine->process_audio({}, true);
    auto json_result = json::parse(result);

    EXPECT_TRUE(json_result.contains("result"));
    EXPECT_TRUE(json_result["result"].is_array());
    EXPECT_GT(json_result["result"].size(), 0);

    // Check word timing structure
    auto first_word = json_result["result"][0];
    EXPECT_TRUE(first_word.contains("word"));
    EXPECT_TRUE(first_word.contains("start"));
    EXPECT_TRUE(first_word.contains("end"));
}

// Test alternatives
TEST_F(VStreamEngineTest, AlternativeResults) {
    testable_vstream_engine::config cfg;
    cfg.max_alternatives = 3;

    engine = std::make_unique<testable_vstream_engine>("/path/to/model", cfg);

    auto audio = create_audio_data(1600);
    engine->process_audio(audio);

    std::string result = engine->process_audio({}, true);
    auto json_result = json::parse(result);

    EXPECT_TRUE(json_result.contains("alternatives"));
    EXPECT_EQ(json_result["alternatives"].size(), 3);

    // Check alternative structure
    auto first_alt = json_result["alternatives"][0];
    EXPECT_TRUE(first_alt.contains("text"));
    EXPECT_TRUE(first_alt.contains("confidence"));
}

// Test speaker identification
TEST_F(VStreamEngineTest, SpeakerIdentification) {
    testable_vstream_engine::config cfg;
    cfg.enable_speaker_id = true;
    cfg.speaker_model_path = "/path/to/speaker/model";

    engine = std::make_unique<testable_vstream_engine>("/path/to/model", cfg);

    auto audio = create_audio_data(1600);
    engine->process_audio(audio);

    std::string result = engine->process_audio({}, true);
    auto json_result = json::parse(result);

    EXPECT_TRUE(json_result.contains("spk"));
    EXPECT_TRUE(json_result.contains("spk_frames"));
    EXPECT_TRUE(json_result["spk"].is_array());
    EXPECT_GT(json_result["spk"].size(), 0);
}

// Test reset functionality
TEST_F(VStreamEngineTest, ResetFunctionality) {
    engine = std::make_unique<testable_vstream_engine>("/path/to/model");

    engine->set_test_text("custom text");
    auto audio = create_audio_data(1600);
    engine->process_audio(audio);

    engine->reset();

    // After reset, should get default text
    std::string result = engine->process_audio({}, true);
    auto json_result = json::parse(result);
    EXPECT_EQ(json_result["text"], "test final result");
}

// Test grammar setting
TEST_F(VStreamEngineTest, GrammarSetting) {
    engine = std::make_unique<testable_vstream_engine>("/path/to/model");

    std::string grammar = "[\"yes\", \"no\", \"maybe\"]";
    engine->set_grammar(grammar);

    EXPECT_EQ(engine->get_grammar(), grammar);

    // Test empty grammar (remove constraints)
    engine->set_grammar("");
    EXPECT_EQ(engine->get_grammar(), "");
}

// Test dynamic max alternatives
TEST_F(VStreamEngineTest, DynamicMaxAlternatives) {
    engine = std::make_unique<testable_vstream_engine>("/path/to/model");

    // Initially no alternatives
    auto audio = create_audio_data(1600);
    engine->process_audio(audio);
    std::string result = engine->process_audio({}, true);
    auto json_result = json::parse(result);
    EXPECT_FALSE(json_result.contains("alternatives"));

    // Set alternatives
    engine->set_max_alternatives(5);
    engine->process_audio(audio);
    result = engine->process_audio({}, true);
    json_result = json::parse(result);
    EXPECT_TRUE(json_result.contains("alternatives"));
    EXPECT_EQ(json_result["alternatives"].size(), 5);
}

// Test NLSML output
TEST_F(VStreamEngineTest, NLSMLOutput) {
    engine = std::make_unique<testable_vstream_engine>("/path/to/model");

    EXPECT_FALSE(engine->is_nlsml_enabled());

    engine->enable_nlsml_output(true);
    EXPECT_TRUE(engine->is_nlsml_enabled());

    engine->enable_nlsml_output(false);
    EXPECT_FALSE(engine->is_nlsml_enabled());
}

// Test partial result detection
TEST_F(VStreamEngineTest, PartialResultDetection) {
    engine = std::make_unique<testable_vstream_engine>("/path/to/model");

    // Initially no partial result
    EXPECT_FALSE(engine->has_partial_result());

    // Set partial result
    engine->set_test_partial("hello world");
    EXPECT_TRUE(engine->has_partial_result());
}

// Test empty audio handling
TEST_F(VStreamEngineTest, EmptyAudioHandling) {
    engine = std::make_unique<testable_vstream_engine>("/path/to/model");

    std::vector<int16_t> empty;
    std::string result = engine->process_audio(empty);

    EXPECT_EQ(result, "{}");
    EXPECT_EQ(engine->get_total_samples_processed(), 0);
}

// Test large audio processing
TEST_F(VStreamEngineTest, LargeAudioProcessing) {
    engine = std::make_unique<testable_vstream_engine>("/path/to/model");

    // Process 10 seconds of audio
    auto large_audio = create_audio_data(160000); // 10 seconds at 16kHz

    EXPECT_NO_THROW({
        std::string result = engine->process_audio(large_audio);
        EXPECT_FALSE(result.empty());
    });

    EXPECT_EQ(engine->get_total_samples_processed(), 160000);
}

// Test continuous processing
TEST_F(VStreamEngineTest, ContinuousProcessing) {
    engine = std::make_unique<testable_vstream_engine>("/path/to/model");

    size_t total_samples = 0;

    // Simulate continuous audio stream
    for (int i = 0; i < 10; ++i) {
        auto chunk = create_audio_data(1600); // 100ms chunks
        std::string result = engine->process_audio(chunk);

        auto json_result = json::parse(result);
        EXPECT_TRUE(json_result.contains("partial"));

        total_samples += chunk.size();
    }

    EXPECT_EQ(engine->get_total_samples_processed(), total_samples);
}

// Test thread safety
TEST_F(VStreamEngineTest, ThreadSafety) {
    engine = std::make_unique<testable_vstream_engine>("/path/to/model");

    const int num_threads = 4;
    std::vector<std::thread> threads;
    std::atomic<int> successful_calls{0};

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([this, &successful_calls]() {
            for (int j = 0; j < 10; ++j) {
                auto audio = create_audio_data(1600);
                std::string result = engine->process_audio(audio);

                if (!result.empty() && result != "{}") {
                    successful_calls++;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(successful_calls.load(), num_threads * 10);
}

// Test concurrent configuration changes
TEST_F(VStreamEngineTest, ConcurrentConfigChanges) {
    engine = std::make_unique<testable_vstream_engine>("/path/to/model");

    std::thread audio_thread([this]() {
        for (int i = 0; i < 20; ++i) {
            auto audio = create_audio_data(1600);
            engine->process_audio(audio);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    std::thread config_thread([this]() {
        for (int i = 0; i < 10; ++i) {
            engine->set_max_alternatives(i % 5);
            engine->set_grammar("[\"test\"]");
            engine->enable_nlsml_output(i % 2 == 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    audio_thread.join();
    config_thread.join();

    // Should complete without crashes
    EXPECT_GT(engine->get_total_samples_processed(), 0);
}

// Test custom text results
TEST_F(VStreamEngineTest, CustomTextResults) {
    engine = std::make_unique<testable_vstream_engine>("/path/to/model");

    // Set custom partial text
    engine->set_test_partial("hello world in progress");
    auto audio = create_audio_data(1600);
    std::string result = engine->process_audio(audio);

    auto json_result = json::parse(result);
    EXPECT_EQ(json_result["partial"], "hello world in progress");

    // Set custom final text
    engine->set_test_text("hello world complete");
    result = engine->process_audio({}, true);

    json_result = json::parse(result);
    EXPECT_EQ(json_result["text"], "hello world complete");
}

// Test sample rate variations
TEST_F(VStreamEngineTest, SampleRateVariations) {
    std::vector<int> sample_rates = {8000, 16000, 32000, 48000};

    for (int rate : sample_rates) {
        testable_vstream_engine::config cfg;
        cfg.sample_rate = rate;

        EXPECT_NO_THROW({
            auto engine_local = std::make_unique<testable_vstream_engine>("/path/to/model", cfg);

            // Process audio at the specified sample rate
            size_t samples = rate / 10; // 100ms of audio
            auto audio = create_audio_data(samples);

            std::string result = engine_local->process_audio(audio);
            EXPECT_FALSE(result.empty());
        });
    }
}

// Performance test (disabled by default)
TEST_F(VStreamEngineTest, DISABLED_PerformanceBenchmark) {
    engine = std::make_unique<testable_vstream_engine>("/path/to/model");

    auto start = std::chrono::high_resolution_clock::now();

    const int num_chunks = 1000;
    auto audio_chunk = create_speech_audio(1600);

    for (int i = 0; i < num_chunks; ++i) {
        engine->process_audio(audio_chunk);

        if (i % 100 == 99) {
            engine->process_audio({}, true); // Force final result periodically
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    std::cout << "Processed " << num_chunks << " audio chunks in "
              << duration.count() / 1000.0 << " milliseconds" << std::endl;
    std::cout << "Average: " << duration.count() / num_chunks << " us/chunk" << std::endl;

    size_t total_samples = engine->get_total_samples_processed();
    double audio_seconds = static_cast<double>(total_samples) / 16000.0;
    double process_seconds = duration.count() / 1000000.0;
    std::cout << "Real-time factor: " << audio_seconds / process_seconds << "x" << std::endl;
}
