#include "mainwindow.h"
#include <QApplication>
#include <QMessageBox>
#include <QSettings>
#include <QSplitter>
#include <QFrame>
#include <QFont>
#include <QScrollBar>
#include <QRegularExpression>
#include <QScrollArea>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QTime>
#include <QCoreApplication>
#include <iostream>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_websocket_client(std::make_unique<WebSocketClient>())
    , m_secondary_client(std::make_unique<WebSocketClient>())
    , m_audio_capture(std::make_unique<AudioCapture>())
    , m_vstream_process(nullptr)
    , m_wstream_process(nullptr)
    , m_is_connected(false)
    , m_is_secondary_connected(false)
    , m_is_recording(false)
    , m_dual_instance_enabled(false)
    , m_audio_source_mode(CLIENT_AUDIO)
    , m_total_words(0)
    , m_secondary_total_words(0)
    , m_current_confidence(0.0f)
    , m_secondary_confidence(0.0f)
{
    setupUI();
    loadSettings();
    autoDetectExecutables();
    populateAudioDevices();

    // Connect primary client signals
    connect(m_websocket_client.get(), &WebSocketClient::connectionChanged,
            this, &MainWindow::onConnectionChanged);
    connect(m_websocket_client.get(), &WebSocketClient::transcriptionReceived,
            this, &MainWindow::onTranscriptionReceived);
    connect(m_websocket_client.get(), &WebSocketClient::statusUpdate,
            this, &MainWindow::onStatusUpdate);

    // Connect secondary client signals
    connect(m_secondary_client.get(), &WebSocketClient::connectionChanged,
            this, &MainWindow::onSecondaryConnectionChanged);
    connect(m_secondary_client.get(), &WebSocketClient::transcriptionReceived,
            this, &MainWindow::onSecondaryTranscriptionReceived);
    connect(m_secondary_client.get(), &WebSocketClient::statusUpdate,
            this, &MainWindow::onSecondaryStatusUpdate);

    // Connect audio capture signals
    connect(m_audio_capture.get(), &AudioCapture::audioLevel,
            this, &MainWindow::onAudioLevel);
    connect(m_audio_capture.get(), &AudioCapture::vadStatus,
            this, &MainWindow::onVadStatus);

    // Setup update timer
    m_update_timer = new QTimer(this);
    connect(m_update_timer, &QTimer::timeout, this, &MainWindow::updateUI);
    m_update_timer->start(UPDATE_INTERVAL_MS);

    // Set window properties
    setWindowTitle("vstream/wstream Qt6 Client");
    setMinimumSize(900, 700);
    resize(1200, 800);

    updateButtonStates();
    onStatusUpdate("Ready to connect");
}

MainWindow::~MainWindow()
{
    saveSettings();
    stopManagedProcesses();

    if (m_is_recording) {
        m_audio_capture->stopRecording();
    }
    if (m_is_connected) {
        m_websocket_client->disconnectFromServer();
    }
    if (m_is_secondary_connected) {
        m_secondary_client->disconnectFromServer();
    }
}

void MainWindow::setupUI()
{
    auto* central_widget = new QWidget;
    setCentralWidget(central_widget);

    auto* main_layout = new QVBoxLayout(central_widget);
    main_layout->setSpacing(10);
    main_layout->setContentsMargins(10, 10, 10, 10);

    // Create main vertical splitter
    auto* main_splitter = new QSplitter(Qt::Vertical);
    main_layout->addWidget(main_splitter);

    // Top section: Controls
    auto* controls_widget = new QWidget;
    auto* controls_layout = new QVBoxLayout(controls_widget);
    controls_layout->setContentsMargins(0, 0, 0, 0);

    // Add process management group
    controls_layout->addWidget(createProcessGroup());

    // Primary connection group
    controls_layout->addWidget(createConnectionGroup());

    // Secondary connection group (hidden by default)
    m_secondary_group = createSecondaryConnectionGroup();
    m_secondary_group->setVisible(false);
    controls_layout->addWidget(m_secondary_group);

    // Audio settings
    controls_layout->addWidget(createAudioGroup());

    // Wrap controls in scroll area
    auto* controls_scroll = new QScrollArea;
    controls_scroll->setWidget(controls_widget);
    controls_scroll->setWidgetResizable(true);
    controls_scroll->setMaximumHeight(450);

    main_splitter->addWidget(controls_scroll);

    // Bottom section: Transcriptions
    auto* transcription_splitter = new QSplitter(Qt::Horizontal);

    // Primary transcription
    transcription_splitter->addWidget(createTranscriptionGroup());

    // Secondary transcription (hidden by default)
    m_secondary_transcription_group = createSecondaryTranscriptionGroup();
    m_secondary_transcription_group->setVisible(false);
    transcription_splitter->addWidget(m_secondary_transcription_group);

    main_splitter->addWidget(transcription_splitter);

    // Set splitter proportions
    main_splitter->setSizes({350, 400});

    // Status bar
    statusBar()->showMessage("Ready");
    m_status_label = new QLabel("Disconnected");
    statusBar()->addPermanentWidget(m_status_label);
}

QGroupBox* MainWindow::createProcessGroup()
{
    auto* group = new QGroupBox("Process Management");
    auto* layout = new QGridLayout(group);

    // vstream executable path
    layout->addWidget(new QLabel("vstream path:"), 0, 0);
    m_vstream_path_edit = new QLineEdit;
    m_vstream_path_edit->setPlaceholderText("/path/to/vstream");
    layout->addWidget(m_vstream_path_edit, 0, 1);

    m_vstream_browse_button = new QPushButton("Browse...");
    connect(m_vstream_browse_button, &QPushButton::clicked, this, &MainWindow::onBrowseVStreamClicked);
    layout->addWidget(m_vstream_browse_button, 0, 2);

    // vstream arguments
    layout->addWidget(new QLabel("vstream args:"), 1, 0);
    m_vstream_args_edit = new QLineEdit("--model /path/to/model --port 8080");
    m_vstream_args_edit->setPlaceholderText("e.g., --model /path/to/model --port 8080");
    layout->addWidget(m_vstream_args_edit, 1, 1);

    m_vstream_start_button = new QPushButton("Start vstream");
    m_vstream_start_button->setMinimumHeight(30);
    connect(m_vstream_start_button, &QPushButton::clicked, this, &MainWindow::onStartVStreamClicked);
    layout->addWidget(m_vstream_start_button, 1, 2);

    // wstream executable path
    layout->addWidget(new QLabel("wstream path:"), 2, 0);
    m_wstream_path_edit = new QLineEdit;
    m_wstream_path_edit->setPlaceholderText("/path/to/wstream");
    layout->addWidget(m_wstream_path_edit, 2, 1);

    m_wstream_browse_button = new QPushButton("Browse...");
    connect(m_wstream_browse_button, &QPushButton::clicked, this, &MainWindow::onBrowseWStreamClicked);
    layout->addWidget(m_wstream_browse_button, 2, 2);

    // wstream arguments
    layout->addWidget(new QLabel("wstream args:"), 3, 0);
    m_wstream_args_edit = new QLineEdit("--model /path/to/model --port 8081");
    m_wstream_args_edit->setPlaceholderText("e.g., --model /path/to/model --port 8081");
    layout->addWidget(m_wstream_args_edit, 3, 1);

    m_wstream_start_button = new QPushButton("Start wstream");
    m_wstream_start_button->setMinimumHeight(30);
    connect(m_wstream_start_button, &QPushButton::clicked, this, &MainWindow::onStartWStreamClicked);
    layout->addWidget(m_wstream_start_button, 3, 2);

    // Process output
    layout->addWidget(new QLabel("Process output:"), 4, 0);
    m_process_output = new QPlainTextEdit;
    m_process_output->setReadOnly(true);
    m_process_output->setMaximumHeight(100);
    m_process_output->setFont(QFont("Consolas", 9));
    layout->addWidget(m_process_output, 5, 0, 1, 3);

    layout->setColumnStretch(1, 1);
    return group;
}

QGroupBox* MainWindow::createConnectionGroup()
{
    auto* group = new QGroupBox("vstream Connection");
    auto* layout = new QGridLayout(group);

    // Server address
    layout->addWidget(new QLabel("Server:"), 0, 0);
    m_server_edit = new QLineEdit("localhost");
    m_server_edit->setPlaceholderText("Enter server address");
    layout->addWidget(m_server_edit, 0, 1);

    // Port
    layout->addWidget(new QLabel("Port:"), 0, 2);
    m_port_spinbox = new QSpinBox;
    m_port_spinbox->setRange(1, 65535);
    m_port_spinbox->setValue(8080);
    layout->addWidget(m_port_spinbox, 0, 3);

    // Connect button
    m_connect_button = new QPushButton("Connect");
    m_connect_button->setMinimumHeight(35);
    connect(m_connect_button, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    layout->addWidget(m_connect_button, 0, 4);

    layout->setColumnStretch(1, 1);
    return group;
}

QGroupBox* MainWindow::createSecondaryConnectionGroup()
{
    auto* group = new QGroupBox("wstream Connection");
    auto* layout = new QGridLayout(group);

    // Server address
    layout->addWidget(new QLabel("Server:"), 0, 0);
    m_secondary_server_edit = new QLineEdit("localhost");
    m_secondary_server_edit->setPlaceholderText("Enter server address");
    layout->addWidget(m_secondary_server_edit, 0, 1);

    // Port
    layout->addWidget(new QLabel("Port:"), 0, 2);
    m_secondary_port_spinbox = new QSpinBox;
    m_secondary_port_spinbox->setRange(1, 65535);
    m_secondary_port_spinbox->setValue(8081);
    layout->addWidget(m_secondary_port_spinbox, 0, 3);

    // Connect button
    m_secondary_connect_button = new QPushButton("Connect");
    m_secondary_connect_button->setMinimumHeight(35);
    connect(m_secondary_connect_button, &QPushButton::clicked, this, &MainWindow::onSecondaryConnectClicked);
    layout->addWidget(m_secondary_connect_button, 0, 4);

    layout->setColumnStretch(1, 1);
    return group;
}

QGroupBox* MainWindow::createAudioGroup()
{
    auto* group = new QGroupBox("Audio Settings");
    auto* main_layout = new QVBoxLayout(group);

    // Audio source selection
    auto* source_layout = new QHBoxLayout;
    source_layout->addWidget(new QLabel("Audio Source:"));

    m_audio_source_combo = new QComboBox;
    m_audio_source_combo->addItem("Client (WebSocket)", CLIENT_AUDIO);
    m_audio_source_combo->addItem("Server (vstream/wstream --mic)", SERVER_AUDIO);
    m_audio_source_combo->setToolTip("Choose whether audio comes from this UI client or from server-side microphone");
    connect(m_audio_source_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onAudioSourceModeChanged);
    source_layout->addWidget(m_audio_source_combo);

    source_layout->addStretch();

    // Dual instance checkbox
    m_dual_instance_checkbox = new QCheckBox("Enable dual instance (vstream + wstream)");
    connect(m_dual_instance_checkbox, &QCheckBox::toggled, this, &MainWindow::onDualInstanceToggled);
    source_layout->addWidget(m_dual_instance_checkbox);

    main_layout->addLayout(source_layout);

    // Separator
    auto* separator = new QFrame;
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    main_layout->addWidget(separator);

    // Client audio controls (shown when CLIENT_AUDIO is selected)
    m_client_audio_widget = new QWidget;
    auto* client_layout = new QGridLayout(m_client_audio_widget);
    client_layout->setContentsMargins(0, 0, 0, 0);

    m_client_audio_label = new QLabel("<b>Client Audio Settings:</b>");
    client_layout->addWidget(m_client_audio_label, 0, 0, 1, 5);

    // Audio device
    client_layout->addWidget(new QLabel("Device:"), 1, 0);
    m_device_combo = new QComboBox;
    m_device_combo->setMinimumWidth(200);
    connect(m_device_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onAudioDeviceChanged);
    client_layout->addWidget(m_device_combo, 1, 1);

    // Sample rate
    client_layout->addWidget(new QLabel("Sample Rate:"), 1, 2);
    m_sample_rate_combo = new QComboBox;
    m_sample_rate_combo->addItems({"8000", "16000", "32000", "48000"});
    m_sample_rate_combo->setCurrentText("16000");
    connect(m_sample_rate_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onSampleRateChanged);
    client_layout->addWidget(m_sample_rate_combo, 1, 3);

    // Record button
    m_record_button = new QPushButton("Start Recording");
    m_record_button->setMinimumHeight(35);
    m_record_button->setEnabled(false);
    connect(m_record_button, &QPushButton::clicked, this, &MainWindow::onRecordClicked);
    client_layout->addWidget(m_record_button, 1, 4);

    // Audio level
    client_layout->addWidget(new QLabel("Level:"), 2, 0);
    m_level_bar = new QProgressBar;
    m_level_bar->setRange(0, 100);
    m_level_bar->setTextVisible(false);
    m_level_bar->setMaximumHeight(20);
    client_layout->addWidget(m_level_bar, 2, 1, 1, 2);

    // VAD indicator
    client_layout->addWidget(new QLabel("VAD:"), 2, 3);
    m_vad_indicator = new QLabel("○");
    m_vad_indicator->setStyleSheet("QLabel { color: gray; font-size: 16px; font-weight: bold; }");
    m_vad_indicator->setAlignment(Qt::AlignCenter);
    client_layout->addWidget(m_vad_indicator, 2, 4);

    client_layout->setColumnStretch(1, 1);
    main_layout->addWidget(m_client_audio_widget);

    // Server audio controls (shown when SERVER_AUDIO is selected)
    m_server_audio_widget = new QWidget;
    auto* server_layout = new QGridLayout(m_server_audio_widget);
    server_layout->setContentsMargins(0, 0, 0, 0);

    m_server_audio_label = new QLabel("<b>Server Audio Settings:</b>");
    server_layout->addWidget(m_server_audio_label, 0, 0, 1, 4);

    // vstream mic device
    server_layout->addWidget(new QLabel("vstream mic:"), 1, 0);
    m_vstream_mic_combo = new QComboBox;
    m_vstream_mic_combo->addItem("Default device", -1);
    m_vstream_mic_combo->setMinimumWidth(200);
    m_vstream_mic_combo->setToolTip("Microphone device for vstream (requires --mic flag)");
    connect(m_vstream_mic_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onVStreamMicDeviceChanged);
    server_layout->addWidget(m_vstream_mic_combo, 1, 1);

    // wstream mic device
    server_layout->addWidget(new QLabel("wstream mic:"), 1, 2);
    m_wstream_mic_combo = new QComboBox;
    m_wstream_mic_combo->addItem("Default device", -1);
    m_wstream_mic_combo->setMinimumWidth(200);
    m_wstream_mic_combo->setToolTip("Microphone device for wstream (requires --mic flag)");
    connect(m_wstream_mic_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onWStreamMicDeviceChanged);
    server_layout->addWidget(m_wstream_mic_combo, 1, 3);

    auto* server_info_label = new QLabel(
        "<i>Note: Server-side audio requires --mic flag in arguments.<br>"
        "Use --mic-device N to specify device index.</i>");
    server_info_label->setWordWrap(true);
    server_info_label->setStyleSheet("QLabel { color: #888; font-size: 10pt; }");
    server_layout->addWidget(server_info_label, 2, 0, 1, 4);

    server_layout->setColumnStretch(1, 1);
    server_layout->setColumnStretch(3, 1);
    main_layout->addWidget(m_server_audio_widget);

    // Initially show client audio controls
    m_server_audio_widget->setVisible(false);
    m_audio_source_mode = CLIENT_AUDIO;

    return group;
}

QGroupBox* MainWindow::createTranscriptionGroup()
{
    auto* group = new QGroupBox("vstream Transcription");
    auto* layout = new QVBoxLayout(group);

    // Main transcription display
    m_transcription_text = new QTextEdit;
    m_transcription_text->setReadOnly(true);
    m_transcription_text->setPlaceholderText("vstream transcription will appear here...");

    // Set font
    QFont mono_font("Consolas", 11);
    if (!mono_font.exactMatch()) {
        mono_font = QFont("Monaco", 11);
        if (!mono_font.exactMatch()) {
            mono_font = QFont("Courier New", 11);
        }
    }
    m_transcription_text->setFont(mono_font);

    layout->addWidget(m_transcription_text);

    // Statistics row
    auto* stats_layout = new QHBoxLayout;

    m_confidence_label = new QLabel("Confidence: --");
    m_confidence_label->setStyleSheet("QLabel { font-weight: bold; }");
    stats_layout->addWidget(m_confidence_label);

    stats_layout->addStretch();

    m_word_count_label = new QLabel("Words: 0");
    m_word_count_label->setStyleSheet("QLabel { font-weight: bold; }");
    stats_layout->addWidget(m_word_count_label);

    layout->addLayout(stats_layout);

    return group;
}

QGroupBox* MainWindow::createSecondaryTranscriptionGroup()
{
    auto* group = new QGroupBox("wstream Transcription");
    auto* layout = new QVBoxLayout(group);

    // Secondary transcription display
    m_secondary_transcription_text = new QTextEdit;
    m_secondary_transcription_text->setReadOnly(true);
    m_secondary_transcription_text->setPlaceholderText("wstream transcription will appear here...");

    // Set font
    QFont mono_font("Consolas", 11);
    if (!mono_font.exactMatch()) {
        mono_font = QFont("Monaco", 11);
        if (!mono_font.exactMatch()) {
            mono_font = QFont("Courier New", 11);
        }
    }
    m_secondary_transcription_text->setFont(mono_font);

    layout->addWidget(m_secondary_transcription_text);

    // Statistics row
    auto* stats_layout = new QHBoxLayout;

    m_secondary_confidence_label = new QLabel("Confidence: --");
    m_secondary_confidence_label->setStyleSheet("QLabel { font-weight: bold; }");
    stats_layout->addWidget(m_secondary_confidence_label);

    stats_layout->addStretch();

    m_secondary_word_count_label = new QLabel("Words: 0");
    m_secondary_word_count_label->setStyleSheet("QLabel { font-weight: bold; }");
    stats_layout->addWidget(m_secondary_word_count_label);

    layout->addLayout(stats_layout);

    return group;
}

void MainWindow::onBrowseVStreamClicked()
{
    QString current_path = m_vstream_path_edit->text();
    QString default_dir = current_path.isEmpty() ? QDir::homePath() : QFileInfo(current_path).absolutePath();

    QString filename = QFileDialog::getOpenFileName(
        this,
        "Select vstream executable",
        default_dir,
        "Executable files (vstream vstream.exe);;All files (*)"
        );

    if (!filename.isEmpty()) {
        m_vstream_path_edit->setText(filename);

        // Check if file is executable
        QFileInfo file_info(filename);
        if (!file_info.isExecutable()) {
            QMessageBox::warning(this, "Warning",
                                 "Selected file may not be executable. You might need to set execute permissions.");
        }
    }
}

void MainWindow::onBrowseWStreamClicked()
{
    QString current_path = m_wstream_path_edit->text();
    QString default_dir = current_path.isEmpty() ? QDir::homePath() : QFileInfo(current_path).absolutePath();

    QString filename = QFileDialog::getOpenFileName(
        this,
        "Select wstream executable",
        default_dir,
        "Executable files (wstream wstream.exe);;All files (*)"
        );

    if (!filename.isEmpty()) {
        m_wstream_path_edit->setText(filename);

        // Check if file is executable
        QFileInfo file_info(filename);
        if (!file_info.isExecutable()) {
            QMessageBox::warning(this, "Warning",
                                 "Selected file may not be executable. You might need to set execute permissions.");
        }
    }
}

void MainWindow::onConnectClicked()
{
    if (m_is_connected) {
        // Disconnect
        if (m_is_recording) {
            m_audio_capture->stopRecording();
            m_is_recording = false;
        }
        m_websocket_client->disconnectFromServer();
    } else {
        // Connect
        QString server = m_server_edit->text().trimmed();
        int port = m_port_spinbox->value();

        if (server.isEmpty()) {
            QMessageBox::warning(this, "Error", "Please enter a server address");
            return;
        }

        onStatusUpdate("Connecting...");
        m_websocket_client->connectToServer(server, port);
    }
}

void MainWindow::onSecondaryConnectClicked()
{
    if (m_is_secondary_connected) {
        // Disconnect
        m_secondary_client->disconnectFromServer();
    } else {
        // Connect
        QString server = m_secondary_server_edit->text().trimmed();
        int port = m_secondary_port_spinbox->value();

        if (server.isEmpty()) {
            QMessageBox::warning(this, "Error", "Please enter a server address");
            return;
        }

        onSecondaryStatusUpdate("Connecting...");
        m_secondary_client->connectToServer(server, port);
    }
}

void MainWindow::onRecordClicked()
{
    if (m_is_recording) {
        // Stop recording
        m_audio_capture->stopRecording();
        m_is_recording = false;
    } else {
        // Start recording
        if (!m_is_connected && (!m_dual_instance_enabled || !m_is_secondary_connected)) {
            QMessageBox::warning(this, "Error", "Please connect to at least one server first");
            return;
        }

        int device_index = m_device_combo->currentData().toInt();
        int sample_rate = m_sample_rate_combo->currentText().toInt();

        // Connect audio to primary WebSocket
        if (m_is_connected) {
            connect(m_audio_capture.get(), &AudioCapture::audioData,
                    m_websocket_client.get(), &WebSocketClient::sendAudioData,
                    Qt::UniqueConnection);
        }

        // Connect audio to secondary WebSocket if dual instance is enabled
        if (m_dual_instance_enabled && m_is_secondary_connected) {
            connect(m_audio_capture.get(), &AudioCapture::audioData,
                    m_secondary_client.get(), &WebSocketClient::sendAudioData,
                    Qt::UniqueConnection);
        }

        if (m_audio_capture->startRecording(device_index, sample_rate)) {
            m_is_recording = true;
            onStatusUpdate("Recording...");
        } else {
            QMessageBox::critical(this, "Error", "Failed to start audio recording");
        }
    }
}

void MainWindow::onDualInstanceToggled(bool checked)
{
    m_dual_instance_enabled = checked;
    m_secondary_group->setVisible(checked);
    m_secondary_transcription_group->setVisible(checked);

    if (!checked && m_is_secondary_connected) {
        m_secondary_client->disconnectFromServer();
    }

    // Update server mic arguments if in server audio mode
    if (m_audio_source_mode == SERVER_AUDIO) {
        updateServerMicArguments();
    }

    updateButtonStates();
}

void MainWindow::onStartVStreamClicked()
{
    if (m_vstream_process && m_vstream_process->state() != QProcess::NotRunning) {
        // Stop process
        m_process_output->appendPlainText("Stopping vstream...");
        m_vstream_process->terminate();
        if (!m_vstream_process->waitForFinished(5000)) {
            m_vstream_process->kill();
        }
    } else {
        // Get executable path
        QString exe_path = m_vstream_path_edit->text().trimmed();
        if (exe_path.isEmpty()) {
            QMessageBox::warning(this, "Error", "Please select the vstream executable first");
            return;
        }

        // Check if file exists
        if (!QFile::exists(exe_path)) {
            QMessageBox::critical(this, "Error", QString("vstream executable not found: %1").arg(exe_path));
            return;
        }

        // Start process
        m_vstream_process = new QProcess(this);

        connect(m_vstream_process, &QProcess::readyReadStandardOutput,
                this, &MainWindow::onVStreamOutput);
        connect(m_vstream_process, &QProcess::readyReadStandardError,
                this, &MainWindow::onVStreamError);
        connect(m_vstream_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &MainWindow::onVStreamFinished);

        QString args = m_vstream_args_edit->text().trimmed();
        m_process_output->appendPlainText(QString("Starting vstream: %1 %2").arg(exe_path).arg(args));

        // Set working directory to the executable's directory
        m_vstream_process->setWorkingDirectory(QFileInfo(exe_path).absolutePath());

        m_vstream_process->start(exe_path, args.split(' ', Qt::SkipEmptyParts));

        if (!m_vstream_process->waitForStarted(3000)) {
            m_process_output->appendPlainText(QString("Failed to start vstream: %1").arg(m_vstream_process->errorString()));
            delete m_vstream_process;
            m_vstream_process = nullptr;
        }
    }

    updateButtonStates();
}

void MainWindow::onStartWStreamClicked()
{
    if (m_wstream_process && m_wstream_process->state() != QProcess::NotRunning) {
        // Stop process
        m_process_output->appendPlainText("Stopping wstream...");
        m_wstream_process->terminate();
        if (!m_wstream_process->waitForFinished(5000)) {
            m_wstream_process->kill();
        }
    } else {
        // Get executable path
        QString exe_path = m_wstream_path_edit->text().trimmed();
        if (exe_path.isEmpty()) {
            QMessageBox::warning(this, "Error", "Please select the wstream executable first");
            return;
        }

        // Check if file exists
        if (!QFile::exists(exe_path)) {
            QMessageBox::critical(this, "Error", QString("wstream executable not found: %1").arg(exe_path));
            return;
        }

        // Start process
        m_wstream_process = new QProcess(this);

        connect(m_wstream_process, &QProcess::readyReadStandardOutput,
                this, &MainWindow::onWStreamOutput);
        connect(m_wstream_process, &QProcess::readyReadStandardError,
                this, &MainWindow::onWStreamError);
        connect(m_wstream_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &MainWindow::onWStreamFinished);

        QString args = m_wstream_args_edit->text().trimmed();
        m_process_output->appendPlainText(QString("Starting wstream: %1 %2").arg(exe_path).arg(args));

        // Set working directory to the executable's directory
        m_wstream_process->setWorkingDirectory(QFileInfo(exe_path).absolutePath());

        m_wstream_process->start(exe_path, args.split(' ', Qt::SkipEmptyParts));

        if (!m_wstream_process->waitForStarted(3000)) {
            m_process_output->appendPlainText(QString("Failed to start wstream: %1").arg(m_wstream_process->errorString()));
            delete m_wstream_process;
            m_wstream_process = nullptr;
        }
    }

    updateButtonStates();
}

void MainWindow::onVStreamOutput()
{
    if (m_vstream_process) {
        QByteArray output = m_vstream_process->readAllStandardOutput();
        QString output_text = QString::fromUtf8(output).trimmed();
        if (!output_text.isEmpty()) {
            m_process_output->appendPlainText(QString("[vstream] %1").arg(output_text));

            // Limit output size
            if (m_process_output->toPlainText().length() > MAX_PROCESS_OUTPUT_LENGTH) {
                QString text = m_process_output->toPlainText();
                m_process_output->setPlainText(text.right(MAX_PROCESS_OUTPUT_LENGTH * 0.8));
            }
        }
    }
}

void MainWindow::onWStreamOutput()
{
    if (m_wstream_process) {
        QByteArray output = m_wstream_process->readAllStandardOutput();
        QString output_text = QString::fromUtf8(output).trimmed();
        if (!output_text.isEmpty()) {
            m_process_output->appendPlainText(QString("[wstream] %1").arg(output_text));

            // Limit output size
            if (m_process_output->toPlainText().length() > MAX_PROCESS_OUTPUT_LENGTH) {
                QString text = m_process_output->toPlainText();
                m_process_output->setPlainText(text.right(MAX_PROCESS_OUTPUT_LENGTH * 0.8));
            }
        }
    }
}

void MainWindow::onVStreamError()
{
    if (m_vstream_process) {
        QByteArray error = m_vstream_process->readAllStandardError();
        QString error_text = QString::fromUtf8(error).trimmed();
        if (!error_text.isEmpty()) {
            m_process_output->appendPlainText(QString("[vstream ERROR] %1").arg(error_text));
        }
    }
}

void MainWindow::onWStreamError()
{
    if (m_wstream_process) {
        QByteArray error = m_wstream_process->readAllStandardError();
        QString error_text = QString::fromUtf8(error).trimmed();
        if (!error_text.isEmpty()) {
            m_process_output->appendPlainText(QString("[wstream ERROR] %1").arg(error_text));
        }
    }
}

void MainWindow::onVStreamFinished(int exit_code, QProcess::ExitStatus exit_status)
{
    m_process_output->appendPlainText(QString("[vstream] Process finished with code %1").arg(exit_code));

    if (m_vstream_process) {
        m_vstream_process->deleteLater();
        m_vstream_process = nullptr;
    }

    updateButtonStates();
}

void MainWindow::onWStreamFinished(int exit_code, QProcess::ExitStatus exit_status)
{
    m_process_output->appendPlainText(QString("[wstream] Process finished with code %1").arg(exit_code));

    if (m_wstream_process) {
        m_wstream_process->deleteLater();
        m_wstream_process = nullptr;
    }

    updateButtonStates();
}

void MainWindow::onAudioSourceModeChanged(int index)
{
    m_audio_source_mode = static_cast<AudioSourceMode>(m_audio_source_combo->currentData().toInt());

    // Show/hide appropriate controls
    m_client_audio_widget->setVisible(m_audio_source_mode == CLIENT_AUDIO);
    m_server_audio_widget->setVisible(m_audio_source_mode == SERVER_AUDIO);

    // Update button states
    updateButtonStates();

    // Update status message
    if (m_audio_source_mode == CLIENT_AUDIO) {
        onStatusUpdate("Audio source: Client (WebSocket)");
    } else {
        onStatusUpdate("Audio source: Server (--mic)");

        // Update arguments if needed
        updateServerMicArguments();
    }
}

void MainWindow::onVStreamMicDeviceChanged(int index)
{
    updateServerMicArguments();
}

void MainWindow::onWStreamMicDeviceChanged(int index)
{
    updateServerMicArguments();
}

void MainWindow::updateServerMicArguments()
{
    if (m_audio_source_mode != SERVER_AUDIO) {
        return;
    }

    // Update vstream arguments
    QString vstream_args = m_vstream_args_edit->text();

    // Remove existing --mic and --mic-device flags
    vstream_args = vstream_args.replace(QRegularExpression("--mic-device\\s+\\d+"), "");
    vstream_args = vstream_args.replace("--mic", "").simplified();

    // Add --mic flag
    if (!vstream_args.contains("--mic")) {
        vstream_args += " --mic";
    }

    // Add --mic-device if not default
    int vstream_device = m_vstream_mic_combo->currentData().toInt();
    if (vstream_device >= 0) {
        vstream_args += QString(" --mic-device %1").arg(vstream_device);
    }

    m_vstream_args_edit->setText(vstream_args.simplified());

    // Update wstream arguments
    QString wstream_args = m_wstream_args_edit->text();

    // Remove existing --mic and --mic-device flags
    wstream_args = wstream_args.replace(QRegularExpression("--mic-device\\s+\\d+"), "");
    wstream_args = wstream_args.replace("--mic", "").simplified();

    // Add --mic flag if dual instance is enabled
    if (m_dual_instance_enabled) {
        if (!wstream_args.contains("--mic")) {
            wstream_args += " --mic";
        }

        // Add --mic-device if not default
        int wstream_device = m_wstream_mic_combo->currentData().toInt();
        if (wstream_device >= 0) {
            wstream_args += QString(" --mic-device %1").arg(wstream_device);
        }
    }

    m_wstream_args_edit->setText(wstream_args.simplified());
}

void MainWindow::onConnectionChanged(bool connected)
{
    m_is_connected = connected;

    if (connected) {
        onStatusUpdate("Connected");
        m_transcription_text->clear();
        m_total_words = 0;
    } else {
        onStatusUpdate("Disconnected");
        if (m_is_recording && !m_is_secondary_connected) {
            m_audio_capture->stopRecording();
            m_is_recording = false;
        }
    }

    updateButtonStates();
}

void MainWindow::onSecondaryConnectionChanged(bool connected)
{
    m_is_secondary_connected = connected;

    if (connected) {
        onSecondaryStatusUpdate("Connected");
        m_secondary_transcription_text->clear();
        m_secondary_total_words = 0;
    } else {
        onSecondaryStatusUpdate("Disconnected");
        if (m_is_recording && !m_is_connected) {
            m_audio_capture->stopRecording();
            m_is_recording = false;
        }
    }

    updateButtonStates();
}

void MainWindow::onTranscriptionReceived(const QString& text, float confidence, bool is_final)
{
    if (text.isEmpty()) return;

    m_current_confidence = confidence;

    if (is_final) {
        // Add final result
        m_transcription_text->setTextColor(QColor(255, 255, 255)); // White for final
        m_transcription_text->append(text);

        // Update word count
        QRegularExpression word_regex("\\b\\w+\\b");
        auto matches = word_regex.globalMatch(text);
        int word_count = 0;
        while (matches.hasNext()) {
            matches.next();
            word_count++;
        }
        m_total_words += word_count;

        // Auto-scroll to bottom
        QScrollBar* scrollbar = m_transcription_text->verticalScrollBar();
        scrollbar->setValue(scrollbar->maximum());

        // Limit text length
        if (m_transcription_text->toPlainText().length() > MAX_TRANSCRIPTION_LENGTH) {
            QString current_text = m_transcription_text->toPlainText();
            m_transcription_text->setPlainText(current_text.right(MAX_TRANSCRIPTION_LENGTH * 0.8));
        }

    } else {
        // Show partial result in status
        onStatusUpdate(QString("Partial: %1...").arg(text.left(50)));
    }
}

void MainWindow::onSecondaryTranscriptionReceived(const QString& text, float confidence, bool is_final)
{
    if (text.isEmpty()) return;

    m_secondary_confidence = confidence;

    if (is_final) {
        // Add final result
        m_secondary_transcription_text->setTextColor(QColor(255, 255, 255));
        m_secondary_transcription_text->append(text);

        // Update word count
        QRegularExpression word_regex("\\b\\w+\\b");
        auto matches = word_regex.globalMatch(text);
        int word_count = 0;
        while (matches.hasNext()) {
            matches.next();
            word_count++;
        }
        m_secondary_total_words += word_count;

        // Auto-scroll to bottom
        QScrollBar* scrollbar = m_secondary_transcription_text->verticalScrollBar();
        scrollbar->setValue(scrollbar->maximum());

        // Limit text length
        if (m_secondary_transcription_text->toPlainText().length() > MAX_TRANSCRIPTION_LENGTH) {
            QString current_text = m_secondary_transcription_text->toPlainText();
            m_secondary_transcription_text->setPlainText(current_text.right(MAX_TRANSCRIPTION_LENGTH * 0.8));
        }
    }
}

void MainWindow::onAudioLevel(float level)
{
    m_level_bar->setValue(static_cast<int>(level * 100));

    // Color coding for level bar
    if (level > 0.8) {
        m_level_bar->setStyleSheet("QProgressBar::chunk { background-color: #ff4444; }");
    } else if (level > 0.5) {
        m_level_bar->setStyleSheet("QProgressBar::chunk { background-color: #ffaa00; }");
    } else {
        m_level_bar->setStyleSheet("QProgressBar::chunk { background-color: #44ff44; }");
    }
}

void MainWindow::onVadStatus(bool active)
{
    if (active) {
        m_vad_indicator->setText("●");
        m_vad_indicator->setStyleSheet("QLabel { color: #44ff44; font-size: 16px; font-weight: bold; }");
    } else {
        m_vad_indicator->setText("○");
        m_vad_indicator->setStyleSheet("QLabel { color: gray; font-size: 16px; font-weight: bold; }");
    }
}

void MainWindow::onStatusUpdate(const QString& status)
{
    statusBar()->showMessage(status);
    m_status_label->setText(m_is_connected ? "vstream: Connected" : "vstream: Disconnected");
}

void MainWindow::onSecondaryStatusUpdate(const QString& status)
{
    if (m_dual_instance_enabled) {
        QString current = statusBar()->currentMessage();
        statusBar()->showMessage(current + " | wstream: " + status);
    }
}

void MainWindow::updateUI()
{
    updateButtonStates();

    // Update primary statistics
    m_confidence_label->setText(QString("Confidence: %1%")
                                    .arg(QString::number(m_current_confidence * 100, 'f', 1)));
    m_word_count_label->setText(QString("Words: %1").arg(m_total_words));

    // Update secondary statistics
    if (m_dual_instance_enabled) {
        m_secondary_confidence_label->setText(QString("Confidence: %1%")
                                                  .arg(QString::number(m_secondary_confidence * 100, 'f', 1)));
        m_secondary_word_count_label->setText(QString("Words: %1").arg(m_secondary_total_words));
    }
}

void MainWindow::updateButtonStates()
{
    // Primary connection button
    m_connect_button->setText(m_is_connected ? "Disconnect" : "Connect");
    m_connect_button->setEnabled(true);

    // Secondary connection button
    if (m_secondary_connect_button) {
        m_secondary_connect_button->setText(m_is_secondary_connected ? "Disconnect" : "Connect");
        m_secondary_connect_button->setEnabled(m_dual_instance_enabled);
    }

    // Record button - only enabled for client audio mode
    if (m_audio_source_mode == CLIENT_AUDIO) {
        m_record_button->setText(m_is_recording ? "Stop Recording" : "Start Recording");
        m_record_button->setEnabled(m_is_connected || (m_dual_instance_enabled && m_is_secondary_connected));
        m_record_button->setVisible(true);
    } else {
        m_record_button->setVisible(false);
        m_level_bar->setValue(0);
        m_vad_indicator->setText("○");
        m_vad_indicator->setStyleSheet("QLabel { color: gray; font-size: 16px; font-weight: bold; }");
    }

    // Process buttons
    if (m_vstream_process && m_vstream_process->state() != QProcess::NotRunning) {
        m_vstream_start_button->setText("Stop vstream");
    } else {
        m_vstream_start_button->setText("Start vstream");
    }

    if (m_wstream_process && m_wstream_process->state() != QProcess::NotRunning) {
        m_wstream_start_button->setText("Stop wstream");
    } else {
        m_wstream_start_button->setText("Start wstream");
    }

    // Enable/disable controls based on state
    m_server_edit->setEnabled(!m_is_connected);
    m_port_spinbox->setEnabled(!m_is_connected);
    m_secondary_server_edit->setEnabled(!m_is_secondary_connected && m_dual_instance_enabled);
    m_secondary_port_spinbox->setEnabled(!m_is_secondary_connected && m_dual_instance_enabled);

    // Client audio controls
    m_device_combo->setEnabled((m_is_connected || m_is_secondary_connected) && !m_is_recording && m_audio_source_mode == CLIENT_AUDIO);
    m_sample_rate_combo->setEnabled((m_is_connected || m_is_secondary_connected) && !m_is_recording && m_audio_source_mode == CLIENT_AUDIO);

    // Server audio controls
    bool vstream_running = m_vstream_process && m_vstream_process->state() != QProcess::NotRunning;
    bool wstream_running = m_wstream_process && m_wstream_process->state() != QProcess::NotRunning;
    m_vstream_mic_combo->setEnabled(!vstream_running && m_audio_source_mode == SERVER_AUDIO);
    m_wstream_mic_combo->setEnabled(!wstream_running && m_audio_source_mode == SERVER_AUDIO && m_dual_instance_enabled);
}

void MainWindow::loadSettings()
{
    QSettings settings;

    // Primary connection
    m_server_edit->setText(settings.value("server", "localhost").toString());
    m_port_spinbox->setValue(settings.value("port", 8080).toInt());

    // Secondary connection
    m_secondary_server_edit->setText(settings.value("secondary_server", "localhost").toString());
    m_secondary_port_spinbox->setValue(settings.value("secondary_port", 8081).toInt());

    // Process paths
    m_vstream_path_edit->setText(settings.value("vstream_path", "").toString());
    m_wstream_path_edit->setText(settings.value("wstream_path", "").toString());

    // Process arguments
    m_vstream_args_edit->setText(settings.value("vstream_args", "--model /path/to/model --port 8080").toString());
    m_wstream_args_edit->setText(settings.value("wstream_args", "--model /path/to/model --port 8081").toString());

    // Dual instance mode
    m_dual_instance_checkbox->setChecked(settings.value("dual_instance", false).toBool());

    // Audio source mode
    int audio_mode = settings.value("audio_source_mode", CLIENT_AUDIO).toInt();
    m_audio_source_combo->setCurrentIndex(m_audio_source_combo->findData(audio_mode));

    // Audio settings
    QString device = settings.value("audio_device").toString();
    int device_index = m_device_combo->findText(device);
    if (device_index >= 0) {
        m_device_combo->setCurrentIndex(device_index);
    }

    QString sample_rate = settings.value("sample_rate", "16000").toString();
    int rate_index = m_sample_rate_combo->findText(sample_rate);
    if (rate_index >= 0) {
        m_sample_rate_combo->setCurrentIndex(rate_index);
    }

    // Server mic devices
    int vstream_mic = settings.value("vstream_mic_device", -1).toInt();
    int vstream_mic_index = m_vstream_mic_combo->findData(vstream_mic);
    if (vstream_mic_index >= 0) {
        m_vstream_mic_combo->setCurrentIndex(vstream_mic_index);
    }

    int wstream_mic = settings.value("wstream_mic_device", -1).toInt();
    int wstream_mic_index = m_wstream_mic_combo->findData(wstream_mic);
    if (wstream_mic_index >= 0) {
        m_wstream_mic_combo->setCurrentIndex(wstream_mic_index);
    }

    // Restore window geometry
    restoreGeometry(settings.value("geometry").toByteArray());
    restoreState(settings.value("windowState").toByteArray());

    // Trigger initial audio source mode setup
    onAudioSourceModeChanged(m_audio_source_combo->currentIndex());
}

void MainWindow::saveSettings()
{
    QSettings settings;

    // Primary connection
    settings.setValue("server", m_server_edit->text());
    settings.setValue("port", m_port_spinbox->value());

    // Secondary connection
    settings.setValue("secondary_server", m_secondary_server_edit->text());
    settings.setValue("secondary_port", m_secondary_port_spinbox->value());

    // Process paths
    settings.setValue("vstream_path", m_vstream_path_edit->text());
    settings.setValue("wstream_path", m_wstream_path_edit->text());

    // Process arguments
    settings.setValue("vstream_args", m_vstream_args_edit->text());
    settings.setValue("wstream_args", m_wstream_args_edit->text());

    // Dual instance mode
    settings.setValue("dual_instance", m_dual_instance_checkbox->isChecked());

    // Audio source mode
    settings.setValue("audio_source_mode", static_cast<int>(m_audio_source_mode));

    // Audio settings
    settings.setValue("audio_device", m_device_combo->currentText());
    settings.setValue("sample_rate", m_sample_rate_combo->currentText());

    // Server mic devices
    settings.setValue("vstream_mic_device", m_vstream_mic_combo->currentData().toInt());
    settings.setValue("wstream_mic_device", m_wstream_mic_combo->currentData().toInt());

    // Window geometry
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
}

void MainWindow::populateAudioDevices()
{
    // Clear all device combos
    m_device_combo->clear();
    m_vstream_mic_combo->clear();
    m_wstream_mic_combo->clear();

    // Add default option for server combos
    m_vstream_mic_combo->addItem("Default device", -1);
    m_wstream_mic_combo->addItem("Default device", -1);

    auto devices = m_audio_capture->getInputDevices();
    for (int i = 0; i < devices.size(); ++i) {
        // Add to client combo
        m_device_combo->addItem(devices[i], i);

        // Add to server combos with device index
        QString device_text = QString("[%1] %2").arg(i).arg(devices[i]);
        m_vstream_mic_combo->addItem(device_text, i);
        m_wstream_mic_combo->addItem(device_text, i);
    }

    if (m_device_combo->count() == 0) {
        m_device_combo->addItem("No audio devices found", -1);
        m_device_combo->setEnabled(false);
    }
}

void MainWindow::stopManagedProcesses()
{
    if (m_vstream_process) {
        m_process_output->appendPlainText("Stopping vstream process...");
        m_vstream_process->terminate();
        if (!m_vstream_process->waitForFinished(5000)) {
            m_vstream_process->kill();
        }
        delete m_vstream_process;
        m_vstream_process = nullptr;
    }

    if (m_wstream_process) {
        m_process_output->appendPlainText("Stopping wstream process...");
        m_wstream_process->terminate();
        if (!m_wstream_process->waitForFinished(5000)) {
            m_wstream_process->kill();
        }
        delete m_wstream_process;
        m_wstream_process = nullptr;
    }
}

void MainWindow::autoDetectExecutables()
{
    // Common locations to search
    QStringList search_paths = {
        QCoreApplication::applicationDirPath(),  // Same directory as this app
        QDir::currentPath(),                     // Current directory
        QDir::homePath() + "/works/vstream/build",  // Your build directory
        QDir::homePath() + "/bin",               // User bin
        "/usr/local/bin",                        // System local bin
        "/opt/vstream/bin"                       // Custom installation
    };

    // Look for vstream
    if (m_vstream_path_edit->text().isEmpty()) {
        for (const QString& path : search_paths) {
            QString vstream_path = QDir(path).absoluteFilePath("vstream");
            if (QFile::exists(vstream_path) && QFileInfo(vstream_path).isExecutable()) {
                m_vstream_path_edit->setText(vstream_path);
                m_process_output->appendPlainText(QString("Auto-detected vstream: %1").arg(vstream_path));
                break;
            }
        }
    }

    // Look for wstream
    if (m_wstream_path_edit->text().isEmpty()) {
        for (const QString& path : search_paths) {
            QString wstream_path = QDir(path).absoluteFilePath("wstream");
            if (QFile::exists(wstream_path) && QFileInfo(wstream_path).isExecutable()) {
                m_wstream_path_edit->setText(wstream_path);
                m_process_output->appendPlainText(QString("Auto-detected wstream: %1").arg(wstream_path));
                break;
            }
        }
    }
}

void MainWindow::onAudioDeviceChanged()
{
    // Update audio device if currently recording
    if (m_is_recording) {
        // Restart with new device
        m_audio_capture->stopRecording();
        QTimer::singleShot(100, this, &MainWindow::onRecordClicked);
    }
}

void MainWindow::onSampleRateChanged()
{
    // Update sample rate if currently recording
    if (m_is_recording) {
        // Restart with new sample rate
        m_audio_capture->stopRecording();
        QTimer::singleShot(100, this, &MainWindow::onRecordClicked);
    }
}
