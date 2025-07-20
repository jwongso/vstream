/**
 * @file websocket_client.cpp
 * @brief Implementation of WebSocket client for vstream communication
 */

#include "websocket_client.h"
#include <QJsonArray>
#include <QDateTime>
#include <QUuid>
#include <QDebug>
#include <QNetworkRequest>
#include <QSslConfiguration>

WebSocketClient::WebSocketClient(QObject *parent)
    : QObject(parent)
    , m_websocket(nullptr)
    , m_server_port(8080)
    , m_is_connected(false)
    , m_reconnect_enabled(false)
    , m_reconnect_attempts(0)
    , m_reconnect_delay_ms(INITIAL_RECONNECT_DELAY_MS)
    , m_messages_sent(0)
    , m_messages_received(0)
{
    // Generate unique session ID
    m_session_id = generateSessionId();

    // Create WebSocket
    m_websocket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);

    // Connect signals
    connect(m_websocket, &QWebSocket::connected, this, &WebSocketClient::onConnected);
    connect(m_websocket, &QWebSocket::disconnected, this, &WebSocketClient::onDisconnected);
    connect(m_websocket, &QWebSocket::textMessageReceived, this, &WebSocketClient::onTextMessageReceived);
    connect(m_websocket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
            this, &WebSocketClient::onSocketError);
    connect(m_websocket, &QWebSocket::sslErrors, this, &WebSocketClient::onSslErrors);

    // Setup reconnection timer
    m_reconnect_timer = new QTimer(this);
    m_reconnect_timer->setSingleShot(true);
    connect(m_reconnect_timer, &QTimer::timeout, this, &WebSocketClient::attemptReconnection);

    // Setup ping timer
    m_ping_timer = new QTimer(this);
    connect(m_ping_timer, &QTimer::timeout, this, &WebSocketClient::sendPing);

    qDebug() << "WebSocketClient created with session ID:" << m_session_id;
}

WebSocketClient::~WebSocketClient()
{
    disconnectFromServer();
}

void WebSocketClient::connectToServer(const QString& host, int port)
{
    if (m_is_connected) {
        qWarning() << "Already connected to server";
        return;
    }

    m_server_host = host;
    m_server_port = port;
    m_reconnect_enabled = true;

    QString url = QString("ws://%1:%2").arg(host).arg(port);

    emit statusUpdate(QString("Connecting to %1...").arg(url));
    qDebug() << "Connecting to:" << url;

    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setRawHeader("User-Agent", "vstream-qt-client/0.1.0");

    m_websocket->open(request);
}

void WebSocketClient::disconnectFromServer()
{
    m_reconnect_enabled = false;
    m_reconnect_timer->stop();
    m_ping_timer->stop();

    if (m_websocket && m_websocket->state() == QAbstractSocket::ConnectedState) {
        m_websocket->close();
    }

    m_is_connected = false;
    resetReconnectionState();
}

bool WebSocketClient::isConnected() const
{
    return m_is_connected.load();
}

QString WebSocketClient::getServerUrl() const
{
    if (m_is_connected) {
        return QString("ws://%1:%2").arg(m_server_host).arg(m_server_port);
    }
    return QString();
}

void WebSocketClient::sendAudioData(const std::vector<int16_t>& samples, int sample_rate)
{
    if (!m_is_connected || samples.empty()) {
        return;
    }

    // Create JSON message
    QJsonObject message;
    message["type"] = "audio";
    message["sample_rate"] = sample_rate;
    message["channels"] = 1;
    message["session_id"] = m_session_id;
    message["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    // Convert samples to JSON array
    QJsonArray audio_array;
    for (int16_t sample : samples) {
        audio_array.append(sample);
    }
    message["audio"] = audio_array;

    // Send message
    QJsonDocument doc(message);
    QString json_string = doc.toJson(QJsonDocument::Compact);

    if (m_websocket->state() == QAbstractSocket::ConnectedState) {
        m_websocket->sendTextMessage(json_string);
        m_messages_sent++;
    }
}

void WebSocketClient::sendCommand(const QString& command, const QJsonObject& params)
{
    if (!m_is_connected) {
        emit errorOccurred("Not connected to server");
        return;
    }

    QJsonObject message;
    message["type"] = "command";
    message["command"] = command;
    message["session_id"] = m_session_id;
    message["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    if (!params.isEmpty()) {
        message["params"] = params;
    }

    QJsonDocument doc(message);
    QString json_string = doc.toJson(QJsonDocument::Compact);

    if (m_websocket->state() == QAbstractSocket::ConnectedState) {
        m_websocket->sendTextMessage(json_string);
        m_messages_sent++;
        qDebug() << "Sent command:" << command;
    }
}

void WebSocketClient::onConnected()
{
    m_is_connected = true;
    resetReconnectionState();

    // Start ping timer
    m_ping_timer->start(PING_INTERVAL_MS);

    emit connectionChanged(true);
    emit statusUpdate("Connected to server");

    qDebug() << "Connected to vstream server:" << getServerUrl();
}

void WebSocketClient::onDisconnected()
{
    bool was_connected = m_is_connected.exchange(false);
    m_ping_timer->stop();

    if (was_connected) {
        emit connectionChanged(false);
        emit statusUpdate("Disconnected from server");
        qDebug() << "Disconnected from server";

        // Attempt reconnection if enabled
        if (m_reconnect_enabled) {
            startReconnectionTimer();
        }
    }
}

void WebSocketClient::onTextMessageReceived(const QString& message)
{
    m_messages_received++;

    // Parse JSON message
    QJsonParseError parse_error;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &parse_error);

    if (parse_error.error != QJsonParseError::NoError) {
        emit errorOccurred(QString("Invalid JSON received: %1").arg(parse_error.errorString()));
        return;
    }

    if (!doc.isObject()) {
        emit errorOccurred("Received JSON is not an object");
        return;
    }

    processMessage(doc.object());
}

void WebSocketClient::onSocketError(QAbstractSocket::SocketError error)
{
    QString error_string = m_websocket->errorString();
    emit errorOccurred(QString("WebSocket error: %1").arg(error_string));
    qWarning() << "WebSocket error:" << error << error_string;

    // Trigger reconnection for network errors
    if (m_reconnect_enabled && !m_is_connected) {
        startReconnectionTimer();
    }
}

void WebSocketClient::onSslErrors(const QList<QSslError>& errors)
{
    for (const auto& error : errors) {
        qWarning() << "SSL error:" << error.errorString();
    }

    // For development, we might want to ignore SSL errors
    // In production, handle these appropriately
    m_websocket->ignoreSslErrors();
}

void WebSocketClient::attemptReconnection()
{
    if (!m_reconnect_enabled || m_is_connected) {
        return;
    }

    m_reconnect_attempts++;
    emit statusUpdate(QString("Reconnecting... (attempt %1)").arg(m_reconnect_attempts));

    QString url = QString("ws://%1:%2").arg(m_server_host).arg(m_server_port);
    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setRawHeader("User-Agent", "vstream-qt-client/0.1.0");

    m_websocket->open(request);

    qDebug() << "Reconnection attempt" << m_reconnect_attempts << "to" << url;
}

void WebSocketClient::sendPing()
{
    if (m_is_connected && m_websocket->state() == QAbstractSocket::ConnectedState) {
        m_websocket->ping();
    }
}

void WebSocketClient::processMessage(const QJsonObject& json_obj)
{
    QString type = json_obj["type"].toString();

    if (type == "transcribe") {
        // Transcription result
        QString content = json_obj["content"].toString();
        float confidence = json_obj["confidence"].toDouble(1.0);
        bool is_final = json_obj["is_final"].toBool(true); // Default to final

        if (!content.isEmpty()) {
            emit transcriptionReceived(content, confidence, is_final);
        }

    } else if (type == "command_response") {
        // Command response
        QString command = json_obj["command"].toString();
        emit commandResponse(command, json_obj);

    } else if (type == "status") {
        // Status message
        QString status = json_obj["message"].toString();
        if (!status.isEmpty()) {
            emit statusUpdate(status);
        }

    } else if (type == "error") {
        // Error message
        QString error_msg = json_obj["message"].toString();
        emit errorOccurred(error_msg);

    } else {
        qDebug() << "Unknown message type:" << type;
    }
}

QString WebSocketClient::generateSessionId()
{
    QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return QString("qt_client_%1").arg(uuid.left(8));
}

void WebSocketClient::resetReconnectionState()
{
    m_reconnect_attempts = 0;
    m_reconnect_delay_ms = INITIAL_RECONNECT_DELAY_MS;
    m_reconnect_timer->stop();
}

void WebSocketClient::startReconnectionTimer()
{
    if (m_reconnect_attempts > 0) {
        // Exponential backoff with jitter
        m_reconnect_delay_ms = std::min(m_reconnect_delay_ms * 2, MAX_RECONNECT_DELAY_MS);
    }

    emit statusUpdate(QString("Reconnecting in %1 seconds...").arg(m_reconnect_delay_ms / 1000));
    m_reconnect_timer->start(m_reconnect_delay_ms);

    qDebug() << "Reconnection scheduled in" << m_reconnect_delay_ms << "ms";
}
