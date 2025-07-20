#pragma once

#include "audio_capture.h"
#include "websocket_client.h"
#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QTextEdit>
#include <QLineEdit>
#include <QLabel>
#include <QProgressBar>
#include <QGroupBox>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QStatusBar>
#include <QTimer>
#include <QSplitter>
#include <QProcess>
#include <QPlainTextEdit>
#include <QFrame>
#include <QWidget>
#include <QScrollArea>
#include <memory>

/**
 * @class MainWindow
 * @brief Main application window providing vstream client interface
 *
 * The MainWindow class provides a comprehensive interface for the vstream
 * Qt6 client application, featuring:
 *
 * - **Connection Management**: Server address/port configuration and connection control
 * - **Dual Instance Support**: Optional second WebSocket connection for wstream
 * - **Process Management**: Launch and manage vstream/wstream processes
 * - **Audio Control**: Microphone device selection and recording controls
 * - **Real-time Display**: Live transcription results with confidence indicators
 * - **Status Monitoring**: Connection status, audio levels, and performance metrics
 * - **User Experience**: Modern dark theme with intuitive controls
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    /**
     * @brief Constructs the main window
     * @param parent Parent widget (typically nullptr for main window)
     */
    explicit MainWindow(QWidget *parent = nullptr);

    /**
     * @brief Destructor - ensures clean shutdown
     */
    ~MainWindow();

private slots:
    /**
     * @brief Handles connection/disconnection button clicks for primary server
     */
    void onConnectClicked();

    /**
     * @brief Handles connection/disconnection button clicks for secondary server
     */
    void onSecondaryConnectClicked();

    /**
     * @brief Handles recording start/stop button clicks
     */
    void onRecordClicked();

    /**
     * @brief Updates the UI when primary WebSocket connection state changes
     * @param connected True if connected, false if disconnected
     */
    void onConnectionChanged(bool connected);

    /**
     * @brief Updates the UI when secondary WebSocket connection state changes
     * @param connected True if connected, false if disconnected
     */
    void onSecondaryConnectionChanged(bool connected);

    /**
     * @brief Displays received transcription results from primary server
     * @param text Transcribed text from vstream
     * @param confidence Confidence score (0.0 to 1.0)
     * @param is_final True for final results, false for partial
     */
    void onTranscriptionReceived(const QString& text, float confidence, bool is_final);

    /**
     * @brief Displays received transcription results from secondary server
     * @param text Transcribed text from wstream
     * @param confidence Confidence score (0.0 to 1.0)
     * @param is_final True for final results, false for partial
     */
    void onSecondaryTranscriptionReceived(const QString& text, float confidence, bool is_final);

    /**
     * @brief Updates audio level display
     * @param level Audio level (0.0 to 1.0)
     */
    void onAudioLevel(float level);

    /**
     * @brief Updates Voice Activity Detection status
     * @param active True if speech detected, false for silence
     */
    void onVadStatus(bool active);

    /**
     * @brief Updates connection status display
     * @param status Status message to display
     */
    void onStatusUpdate(const QString& status);

    /**
     * @brief Updates secondary connection status display
     * @param status Status message to display
     */
    void onSecondaryStatusUpdate(const QString& status);

    /**
     * @brief Handles audio device selection changes
     */
    void onAudioDeviceChanged();

    /**
     * @brief Handles sample rate selection changes
     */
    void onSampleRateChanged();

    /**
     * @brief Toggles dual instance mode
     * @param checked True to enable dual instance
     */
    void onDualInstanceToggled(bool checked);

    /**
     * @brief Browse for vstream executable
     */
    void onBrowseVStreamClicked();

    /**
     * @brief Browse for wstream executable
     */
    void onBrowseWStreamClicked();

    /**
     * @brief Starts vstream process
     */
    void onStartVStreamClicked();

    /**
     * @brief Starts wstream process
     */
    void onStartWStreamClicked();

    /**
     * @brief Handles vstream process output
     */
    void onVStreamOutput();

    /**
     * @brief Handles wstream process output
     */
    void onWStreamOutput();

    /**
     * @brief Handles vstream process errors
     */
    void onVStreamError();

    /**
     * @brief Handles wstream process errors
     */
    void onWStreamError();

    /**
     * @brief Handles vstream process finished
     * @param exit_code Process exit code
     * @param exit_status Process exit status
     */
    void onVStreamFinished(int exit_code, QProcess::ExitStatus exit_status);

    /**
     * @brief Handles wstream process finished
     * @param exit_code Process exit code
     * @param exit_status Process exit status
     */
    void onWStreamFinished(int exit_code, QProcess::ExitStatus exit_status);

    /**
     * @brief Handles audio source mode changes
     * @param index Selected index in combo box
     */
    void onAudioSourceModeChanged(int index);

    /**
     * @brief Updates server-side mic device for vstream
     * @param index Selected device index
     */
    void onVStreamMicDeviceChanged(int index);

    /**
     * @brief Updates server-side mic device for wstream
     * @param index Selected device index
     */
    void onWStreamMicDeviceChanged(int index);

    /**
     * @brief Periodic UI updates (called by timer)
     */
    void updateUI();

private:
    /**
     * @brief Audio source mode enumeration
     */
    enum AudioSourceMode {
        CLIENT_AUDIO = 0,    ///< Audio from UI client via WebSocket
        SERVER_AUDIO = 1     ///< Audio from server-side microphone
    };

    /**
     * @brief Initializes the user interface
     */
    void setupUI();

    /**
     * @brief Creates connection settings group for primary server
     * @return Configured group box widget
     */
    QGroupBox* createConnectionGroup();

    /**
     * @brief Creates connection settings group for secondary server
     * @return Configured group box widget
     */
    QGroupBox* createSecondaryConnectionGroup();

    /**
     * @brief Creates process management group
     * @return Configured group box widget
     */
    QGroupBox* createProcessGroup();

    /**
     * @brief Creates audio settings group
     * @return Configured group box widget
     */
    QGroupBox* createAudioGroup();

    /**
     * @brief Creates transcription display group
     * @return Configured group box widget
     */
    QGroupBox* createTranscriptionGroup();

    /**
     * @brief Creates secondary transcription display group
     * @return Configured group box widget
     */
    QGroupBox* createSecondaryTranscriptionGroup();

    /**
     * @brief Updates button states based on current status
     */
    void updateButtonStates();

    /**
     * @brief Loads application settings from persistent storage
     */
    void loadSettings();

    /**
     * @brief Saves application settings to persistent storage
     */
    void saveSettings();

    /**
     * @brief Enumerates available audio input devices
     */
    void populateAudioDevices();

    /**
     * @brief Stops managed processes
     */
    void stopManagedProcesses();

    /**
     * @brief Auto-detects executables in common locations
     */
    void autoDetectExecutables();

    /**
     * @brief Updates server microphone arguments based on selections
     */
    void updateServerMicArguments();

    // Core components
    std::unique_ptr<WebSocketClient> m_websocket_client;     ///< Primary WebSocket communication
    std::unique_ptr<WebSocketClient> m_secondary_client;     ///< Secondary WebSocket communication
    std::unique_ptr<AudioCapture> m_audio_capture;          ///< Microphone capture

    // Process management
    QProcess* m_vstream_process;                             ///< vstream process
    QProcess* m_wstream_process;                             ///< wstream process

    // Primary connection controls
    QLineEdit* m_server_edit;        ///< Server address input
    QSpinBox* m_port_spinbox;        ///< Port number input
    QPushButton* m_connect_button;   ///< Connect/disconnect button

    // Secondary connection controls
    QGroupBox* m_secondary_group;         ///< Secondary connection group
    QLineEdit* m_secondary_server_edit;   ///< Secondary server address
    QSpinBox* m_secondary_port_spinbox;   ///< Secondary port number
    QPushButton* m_secondary_connect_button; ///< Secondary connect button

    // Process controls
    QGroupBox* m_process_group;      ///< Process management group
    QLineEdit* m_vstream_path_edit;  ///< vstream executable path
    QPushButton* m_vstream_browse_button; ///< Browse for vstream
    QLineEdit* m_vstream_args_edit;  ///< vstream arguments
    QPushButton* m_vstream_start_button; ///< Start vstream button
    QLineEdit* m_wstream_path_edit;  ///< wstream executable path
    QPushButton* m_wstream_browse_button; ///< Browse for wstream
    QLineEdit* m_wstream_args_edit;  ///< wstream arguments
    QPushButton* m_wstream_start_button; ///< Start wstream button
    QPlainTextEdit* m_process_output; ///< Process output display

    // Audio source controls
    QComboBox* m_audio_source_combo;     ///< Audio source selection (Client/Server)
    QLabel* m_client_audio_label;        ///< Label for client audio section
    QLabel* m_server_audio_label;        ///< Label for server audio section
    QWidget* m_client_audio_widget;      ///< Container for client audio controls
    QWidget* m_server_audio_widget;      ///< Container for server audio controls

    // Client audio controls
    QComboBox* m_device_combo;       ///< Audio device selector
    QComboBox* m_sample_rate_combo;  ///< Sample rate selector
    QPushButton* m_record_button;    ///< Start/stop recording button
    QProgressBar* m_level_bar;       ///< Audio level indicator
    QLabel* m_vad_indicator;         ///< Voice activity indicator

    // Server-side mic controls
    QComboBox* m_vstream_mic_combo;      ///< vstream mic device selector
    QComboBox* m_wstream_mic_combo;      ///< wstream mic device selector

    // Dual instance controls
    QCheckBox* m_dual_instance_checkbox; ///< Enable dual instance mode

    // Primary transcription display
    QTextEdit* m_transcription_text; ///< Main transcription display
    QLabel* m_confidence_label;      ///< Current confidence score
    QLabel* m_word_count_label;      ///< Word count statistics
    QLabel* m_status_label;          ///< Connection status

    // Secondary transcription display
    QGroupBox* m_secondary_transcription_group; ///< Secondary transcription group
    QTextEdit* m_secondary_transcription_text;  ///< Secondary transcription display
    QLabel* m_secondary_confidence_label;       ///< Secondary confidence score
    QLabel* m_secondary_word_count_label;       ///< Secondary word count
    QLabel* m_secondary_status_label;           ///< Secondary connection status

    // UI state
    QTimer* m_update_timer;          ///< Periodic UI update timer
    bool m_is_connected;             ///< Primary WebSocket connection state
    bool m_is_secondary_connected;   ///< Secondary WebSocket connection state
    bool m_is_recording;             ///< Audio recording state
    bool m_dual_instance_enabled;    ///< Dual instance mode enabled
    AudioSourceMode m_audio_source_mode; ///< Current audio source mode
    int m_total_words;               ///< Total word count (primary)
    int m_secondary_total_words;     ///< Total word count (secondary)
    float m_current_confidence;      ///< Current confidence score (primary)
    float m_secondary_confidence;    ///< Current confidence score (secondary)

    // Constants
    static constexpr int UPDATE_INTERVAL_MS = 100;  ///< UI update frequency
    static constexpr int MAX_TRANSCRIPTION_LENGTH = 10000; ///< Max text display length
    static constexpr int MAX_PROCESS_OUTPUT_LENGTH = 5000; ///< Max process output length
};
