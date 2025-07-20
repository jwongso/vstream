/**
 * @file main.cpp
 * @brief Main entry point for the vstream Qt6 client application
 *
 * Simple Qt6-based client demonstrating real-time speech recognition
 * using the vstream WebSocket API with PortAudio microphone capture.
 */

#include "mainwindow.h"
#include <QApplication>
#include <QStyleFactory>
#include <QDir>
#include <iostream>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Set application properties
    app.setApplicationName("vstream Qt Client");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("vstream");
    app.setApplicationDisplayName("vstream Speech Recognition Client");

    // Set a modern style if available
    QStringList styles = QStyleFactory::keys();
    if (styles.contains("Fusion")) {
        app.setStyle("Fusion");
    }

    // Apply dark theme
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, QColor(25, 25, 25));
    darkPalette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);
    app.setPalette(darkPalette);

    // Create and show main window
    MainWindow window;
    window.show();

    std::cout << "vstream Qt6 Client started\n";
    std::cout << "Connect to your vstream server and start recording!\n";

    return app.exec();
}
