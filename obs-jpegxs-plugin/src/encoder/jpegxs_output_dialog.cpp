/*
 * JPEG XS Output Dialog - Frontend UI Component
 * Provides a Tools menu entry with a configuration dialog
 */

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/platform.h>
#include <QMainWindow>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QMessageBox>
#include <QTimer>

class JpegXSOutputDialog : public QDialog {
private:
    // Output instance
    obs_output_t *output = nullptr;
    bool output_active = false;
    
    // UI Elements
    QLineEdit *srt_url_edit;
    QLineEdit *passphrase_edit;
    QSpinBox *latency_spin;
    QDoubleSpinBox *bitrate_spin;
    QPushButton *start_button;
    QPushButton *stop_button;
    QLabel *status_label;
    QLabel *stats_label;
    
    // Status update timer
    QTimer *status_timer;
    
public:
    JpegXSOutputDialog(QWidget *parent = nullptr) : QDialog(parent) {
        setWindowTitle("JPEG XS Output - RTP/SRT Streaming");
        setMinimumWidth(500);
        
        // Main layout
        QVBoxLayout *main_layout = new QVBoxLayout(this);
        
        // Configuration group
        QGroupBox *config_group = new QGroupBox("Stream Configuration");
        QFormLayout *config_layout = new QFormLayout(config_group);
        
        // SRT URL
        srt_url_edit = new QLineEdit("srt://127.0.0.1:9000");
        srt_url_edit->setPlaceholderText("srt://host:port or srt://:port (listener mode)");
        config_layout->addRow("SRT URL:", srt_url_edit);
        
        // Passphrase
        passphrase_edit = new QLineEdit();
        passphrase_edit->setEchoMode(QLineEdit::Password);
        passphrase_edit->setPlaceholderText("Optional encryption passphrase");
        config_layout->addRow("Passphrase:", passphrase_edit);
        
        // Latency
        latency_spin = new QSpinBox();
        latency_spin->setRange(20, 8000);
        latency_spin->setValue(120);
        latency_spin->setSuffix(" ms");
        config_layout->addRow("SRT Latency:", latency_spin);
        
        // Bitrate
        bitrate_spin = new QDoubleSpinBox();
        bitrate_spin->setRange(10.0, 1000.0);
        bitrate_spin->setValue(200.0);
        bitrate_spin->setSuffix(" Mbps");
        bitrate_spin->setDecimals(1);
        config_layout->addRow("Target Bitrate:", bitrate_spin);
        
        main_layout->addWidget(config_group);
        
        // Status group
        QGroupBox *status_group = new QGroupBox("Status");
        QVBoxLayout *status_layout = new QVBoxLayout(status_group);
        
        status_label = new QLabel("Not streaming");
        status_label->setStyleSheet("font-weight: bold;");
        status_layout->addWidget(status_label);
        
        stats_label = new QLabel("Frames: 0 | Dropped: 0");
        status_layout->addWidget(stats_label);
        
        main_layout->addWidget(status_group);
        
        // Control buttons
        QHBoxLayout *button_layout = new QHBoxLayout();
        
        start_button = new QPushButton("Start Streaming");
        start_button->setStyleSheet("background-color: #0e8420; color: white; font-weight: bold; padding: 8px;");
        QObject::connect(start_button, &QPushButton::clicked, [this]() { onStartClicked(); });
        button_layout->addWidget(start_button);
        
        stop_button = new QPushButton("Stop Streaming");
        stop_button->setStyleSheet("background-color: #c92a2a; color: white; font-weight: bold; padding: 8px;");
        stop_button->setEnabled(false);
        QObject::connect(stop_button, &QPushButton::clicked, [this]() { onStopClicked(); });
        button_layout->addWidget(stop_button);
        
        main_layout->addLayout(button_layout);
        
        // Status update timer
        status_timer = new QTimer(this);
        QObject::connect(status_timer, &QTimer::timeout, [this]() { updateStatus(); });
        
        setLayout(main_layout);
    }
    
    ~JpegXSOutputDialog() {
        if (output) {
            if (output_active) {
                obs_output_stop(output);
            }
            obs_output_release(output);
        }
    }
    
private:
    void onStartClicked() {
        blog(LOG_INFO, "[JPEG XS UI] Start button clicked");
        
        // Get video output from OBS
        obs_video_info ovi;
        if (!obs_get_video_info(&ovi)) {
            QMessageBox::critical(this, "Error", "Failed to get video info from OBS");
            return;
        }
        
        // Create output if needed
        if (!output) {
            obs_data_t *settings = obs_data_create();
            output = obs_output_create("jpegxs_output", "JPEG XS Stream", settings, nullptr);
            obs_data_release(settings);
            
            if (!output) {
                QMessageBox::critical(this, "Error", "Failed to create JPEG XS output");
                return;
            }
        }
        
        // Update settings
        obs_data_t *settings = obs_output_get_settings(output);
        obs_data_set_string(settings, "srt_url", srt_url_edit->text().toUtf8().constData());
        obs_data_set_string(settings, "srt_passphrase", passphrase_edit->text().toUtf8().constData());
        obs_data_set_int(settings, "srt_latency_ms", latency_spin->value());
        obs_data_set_double(settings, "bitrate_mbps", bitrate_spin->value());
        obs_output_update(output, settings);
        obs_data_release(settings);
        
        // Connect to video output
        video_t *video = obs_get_video();
        obs_output_set_media(output, video, obs_get_audio());
        
        // Start output
        if (obs_output_start(output)) {
            output_active = true;
            status_label->setText("🔴 Streaming Active");
            status_label->setStyleSheet("color: #0e8420; font-weight: bold;");
            start_button->setEnabled(false);
            stop_button->setEnabled(true);
            srt_url_edit->setEnabled(false);
            passphrase_edit->setEnabled(false);
            latency_spin->setEnabled(false);
            bitrate_spin->setEnabled(false);
            status_timer->start(1000);  // Update stats every second
            
            blog(LOG_INFO, "[JPEG XS UI] Streaming started successfully");
        } else {
            QMessageBox::critical(this, "Error", "Failed to start JPEG XS output");
            blog(LOG_ERROR, "[JPEG XS UI] Failed to start streaming");
        }
    }
    
    void onStopClicked() {
        blog(LOG_INFO, "[JPEG XS UI] Stop button clicked");
        
        if (output && output_active) {
            obs_output_stop(output);
            output_active = false;
            status_label->setText("Not streaming");
            status_label->setStyleSheet("color: gray; font-weight: bold;");
            start_button->setEnabled(true);
            stop_button->setEnabled(false);
            srt_url_edit->setEnabled(true);
            passphrase_edit->setEnabled(true);
            latency_spin->setEnabled(true);
            bitrate_spin->setEnabled(true);
            status_timer->stop();
            
            blog(LOG_INFO, "[JPEG XS UI] Streaming stopped");
        }
    }
    
    void updateStatus() {
        if (output && output_active) {
            uint64_t total_frames = obs_output_get_total_frames(output);
            uint64_t dropped_frames = obs_output_get_frames_dropped(output);
            
            stats_label->setText(QString("Frames: %1 | Dropped: %2")
                .arg(total_frames)
                .arg(dropped_frames));
        }
    }
    
    void closeEvent(QCloseEvent *event) override {
        if (output_active) {
            auto reply = QMessageBox::question(this, "Confirm", 
                "Streaming is active. Stop streaming and close?",
                QMessageBox::Yes | QMessageBox::No);
            
            if (reply == QMessageBox::Yes) {
                onStopClicked();
                event->accept();
            } else {
                event->ignore();
            }
        } else {
            event->accept();
        }
    }
};

// Global dialog instance
static JpegXSOutputDialog *dialog = nullptr;

// Menu action callback
static void show_jpegxs_dialog(void) {
    if (!dialog) {
        QMainWindow *main_window = static_cast<QMainWindow*>(obs_frontend_get_main_window());
        dialog = new JpegXSOutputDialog(main_window);
    }
    
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

// Module load callback
void register_jpegxs_output_ui(void) {
    // Add menu item to Tools menu
    obs_frontend_add_tools_menu_item("JPEG XS Output...", show_jpegxs_dialog);
    
    blog(LOG_INFO, "[JPEG XS UI] Frontend UI registered in Tools menu");
}

// Module unload callback
void unregister_jpegxs_output_ui(void) {
    if (dialog) {
        dialog->close();
        dialog->deleteLater();
        dialog = nullptr;
    }
}
