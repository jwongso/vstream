/**
 * @file websocket_client.h
 * @brief WebSocket client for communicating with vstream server
 *
 * Provides bidirectional WebSocket communication for sending audio data
 * and receiving transcription results from vstream server.
 */

#pragma once

#include <QObject>
#include <QWebSocket>
#include <QTimer>
#include <QJsonObject>
#include <QJsonDocument>
#include <vector>
#include <atomic>

/**
 * @class WebSocketClient
 * @brief High-level WebSocket client for vstream communication
 *
 * The WebSocketClient class provides a Qt-based interface for communicating
 * with vstream servers via WebSocket protocol. Features include:
 *
 * - **Bidirectional Communication**: Send audio data, receive transcriptions
 * - **Automatic Reconnection**: Handles connection drops with exponential backoff
 * - **JSON Protocol**: Structured message format matching vstream expectations
 * - **Thread Safety**: Safe operation with Qt's signal-slot system
 * - **Error Handling**: Comprehensive error detection and reporting
 * - **Session Management**: Unique session IDs for multi-client scenarios
 *
 * @par Message Protocol:
 *
 * **Outgoing Audio Message:**
 * ```json
 * {
 *   "type": "audio",
 *   "audio": [1234, -567, 890, ...],
 *   "sample_rate": 16000,
 *   "channels": 1,
 *   "session_id": "qt_client_12345",
 *   "timestamp": 1640995200000
 * }
 * ```
 *
 * **Incoming Transcription Message:**
 * ```json
 * {
 *   "type": "transcribe",
 *   "content": "transcribed text here",
 *   "session_id": "qt_client_12345",
 *   "confidence": 0.95
 * }
 * ```
 *
 * @par Connection Management:
 * - Automatic reconnection with exponential backoff (1s, 2s, 4s, 8s, max 30s)
 * - Connection state monitoring and reporting
 * - Graceful handling of network interruptions
 * - Ping/pong keepalive mechanism
 *
 * @par Thread Safety:
 * All methods are thread-safe and can be called from any thread.
 * Signals are emitted on the main thread for UI updates.
 */
class WebSocketClient : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs WebSocket client
     * @param parent Parent QObject for Qt memory management
     */
    explicit WebSocketClient(QObject *parent = nullptr);

    /**
     * @brief Destructor - ensures clean disconnection
     */
    ~WebSocketClient();

    /**
     * @brief Connects to vstream server
     * @param host Server hostname or IP address
     * @param port Server port number
     *
     * Initiates WebSocket connection to the specified vstream server.
     * Connection status is reported via connectionChanged signal.
     */
    void connectToServer(const QString& host, int port);

    /**
     * @brief Disconnects from vstream server
     *
     * Gracefully closes the WebSocket connection and disables
     * automatic reconnection.
     */
    void disconnectFromServer();

    /**
     * @brief Checks if currently connected to server
     * @return true if connected, false otherwise
     */
    bool isConnected() const;

    /**
     * @brief Gets current server URL
     * @return WebSocket URL or empty string if not connected
     */
    QString getServerUrl() const;

    /**
     * @brief Sends audio data to vstream server
     * @param samples Vector of 16-bit PCM audio samples
     * @param sample_rate Sample rate of the audio data
     *
     * Packages audio data into JSON format and sends to server.
     * Automatically includes session ID and timestamp.
     */
    void sendAudioData(const std::vector<int16_t>& samples, int sample_rate);

    /**
     * @brief Sends command to vstream server
     * @param command Command name (e.g., "reset", "set_grammar")
     * @param params Command parameters as JSON object
     *
     * Sends structured command messages to the server for
     * configuration and control operations.
     */
    void sendCommand(const QString& command, const QJsonObject& params = QJsonObject());

signals:
    /**
     * @brief Emitted when connection state changes
     * @param connected true if connected, false if disconnected
     */
    void connectionChanged(bool connected);

    /**
     * @brief Emitted when transcription result is received
     * @param text Transcribed text from vstream
     * @param confidence Confidence score (0.0 to 1.0)
     * @param is_final true for final results, false for partial
     */
    void transcriptionReceived(const QString& text, float confidence, bool is_final);

    /**
     * @brief Emitted for status updates and informational messages
     * @param message Status message for user display
     */
    void statusUpdate(const QString& message);

    /**
     * @brief Emitted when WebSocket errors occur
     * @param error_message Description of the error
     */
    void errorOccurred(const QString& error_message);

    /**
     * @brief Emitted when command response is received
     * @param command Original command name
     * @param response Server response as JSON object
     */
    void commandResponse(const QString& command, const QJsonObject& response);

private slots:
    /**
     * @brief Handles WebSocket connection establishment
     */
    void onConnected();

    /**
     * @brief Handles WebSocket disconnection
     */
    void onDisconnected();

    /**
     * @brief Processes incoming text messages
     * @param message JSON message from server
     */
    void onTextMessageReceived(const QString& message);

    /**
     * @brief Handles WebSocket errors
     * @param error Qt WebSocket error code
     */
    void onSocketError(QAbstractSocket::SocketError error);

    /**
     * @brief Handles SSL errors (for secure connections)
     * @param errors List of SSL errors
     */
    void onSslErrors(const QList<QSslError>& errors);

    /**
     * @brief Attempts automatic reconnection
     */
    void attemptReconnection();

    /**
     * @brief Sends periodic ping messages
     */
    void sendPing();

private:
    /**
     * @brief Processes incoming JSON messages
     * @param json_obj Parsed JSON message from server
     */
    void processMessage(const QJsonObject& json_obj);

    /**
     * @brief Generates unique session ID
     * @return Unique session identifier string
     */
    QString generateSessionId();

    /**
     * @brief Resets reconnection state
     */
    void resetReconnectionState();

    /**
     * @brief Starts automatic reconnection timer
     */
    void startReconnectionTimer();

    // Core WebSocket
    QWebSocket* m_websocket;               ///< Qt WebSocket instance
    QString m_server_host;                 ///< Server hostname
    int m_server_port;                     ///< Server port
    QString m_session_id;                  ///< Unique session identifier

    // Connection management
    std::atomic<bool> m_is_connected;      ///< Connection state
    std::atomic<bool> m_reconnect_enabled; ///< Auto-reconnect flag
    QTimer* m_reconnect_timer;             ///< Reconnection timer
    QTimer* m_ping_timer;                  ///< Keepalive ping timer

    // Reconnection logic
    int m_reconnect_attempts;              ///< Current reconnection attempt count
    int m_reconnect_delay_ms;              ///< Current reconnection delay
    static constexpr int MAX_RECONNECT_DELAY_MS = 30000; ///< Maximum reconnection delay
    static constexpr int INITIAL_RECONNECT_DELAY_MS = 1000; ///< Initial reconnection delay
    static constexpr int PING_INTERVAL_MS = 30000; ///< Ping interval

    // Statistics
    std::atomic<size_t> m_messages_sent;   ///< Total messages sent
    std::atomic<size_t> m_messages_received; ///< Total messages received
};
