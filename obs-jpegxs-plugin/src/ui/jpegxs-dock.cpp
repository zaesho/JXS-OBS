#include "jpegxs-dock.h"
#include <QVBoxLayout>
#include <QGroupBox>
#include <obs-module.h>
#include <obs-frontend-api.h>

// Global output instance to manage state
static obs_output_t *jpegxs_output = nullptr;

JpegXSDock::JpegXSDock(QWidget *parent) : QDockWidget(parent) {
    setWindowTitle("JPEG XS Control");
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);

    QWidget *content = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(content);

    // Transport Selection
    QGroupBox *transportGroup = new QGroupBox("Transport", content);
    QVBoxLayout *transportLayout = new QVBoxLayout(transportGroup);
    
    transportModeCombo = new QComboBox(transportGroup);
    transportModeCombo->addItem("SRT (Reliable Internet)", "SRT");
    transportModeCombo->addItem("ST 2110-22 (UDP/Multicast)", "ST 2110-22 (UDP/Multicast)");
    transportLayout->addWidget(transportModeCombo);
    
    mainLayout->addWidget(transportGroup);

    // SRT Settings Area
    srtWidget = new QWidget(content);
    QFormLayout *srtLayout = new QFormLayout(srtWidget);
    srtLayout->setContentsMargins(0, 0, 0, 0);
    
    srtUrlEdit = new QLineEdit("srt://127.0.0.1:9000?mode=caller", srtWidget);
    passphraseEdit = new QLineEdit(srtWidget);
    passphraseEdit->setEchoMode(QLineEdit::Password);
    latencySpinBox = new QSpinBox(srtWidget);
    latencySpinBox->setRange(10, 2000);
    latencySpinBox->setValue(20);
    latencySpinBox->setSuffix(" ms");
    
    srtLayout->addRow("SRT URL:", srtUrlEdit);
    srtLayout->addRow("Passphrase:", passphraseEdit);
    srtLayout->addRow("Latency:", latencySpinBox);
    
    QGroupBox *srtGroup = new QGroupBox("SRT Settings", content);
    srtGroup->setLayout(srtLayout);
    mainLayout->addWidget(srtGroup);
    
    // Store reference to group box instead of widget for visibility toggling?
    // Actually let's just toggle the group box.
    // We need to reconstruct properly.
    // Let's wrap srtLayout in srtWidget, then put srtWidget inside the GroupBox? 
    // No, layout applies to widget.
    // Let's assign srtGroup to a member variable if we want to hide it.
    // Re-doing layout logic slightly for easier toggling.
    
    // Clean up previous attempts in memory (not needed in C++, just re-assign pointers locally)
    // Let's use member variables for the containers we want to hide/show.
    
    // SRT Container
    srtWidget = new QGroupBox("SRT Settings", content);
    QFormLayout *srtL = new QFormLayout(srtWidget);
    srtUrlEdit->setParent(srtWidget);
    passphraseEdit->setParent(srtWidget);
    latencySpinBox->setParent(srtWidget);
    srtL->addRow("SRT URL:", srtUrlEdit);
    srtL->addRow("Passphrase:", passphraseEdit);
    srtL->addRow("Latency:", latencySpinBox);
    mainLayout->addWidget(srtWidget);
    
    // ST 2110 Container
    st2110Widget = new QGroupBox("ST 2110-22 Settings", content);
    QFormLayout *st2110L = new QFormLayout(st2110Widget);
    
    st2110DestIpEdit = new QLineEdit("239.1.1.1", st2110Widget);
    st2110DestPortSpin = new QSpinBox(st2110Widget);
    st2110DestPortSpin->setRange(1024, 65535);
    st2110DestPortSpin->setValue(5000);
    st2110SourceIpEdit = new QLineEdit(st2110Widget);
    st2110SourceIpEdit->setPlaceholderText("0.0.0.0 (Optional)");
    
    st2110L->addRow("Dest IP/Multicast:", st2110DestIpEdit);
    st2110L->addRow("Dest Port:", st2110DestPortSpin);
    st2110L->addRow("Source IP:", st2110SourceIpEdit);
    mainLayout->addWidget(st2110Widget);
    
    // Encoder Settings
    QGroupBox *encGroup = new QGroupBox("Encoder", content);
    QFormLayout *encLayout = new QFormLayout(encGroup);

    compressionRatioSpinBox = new QDoubleSpinBox(encGroup);
    compressionRatioSpinBox->setRange(2.0, 100.0);
    compressionRatioSpinBox->setValue(10.0);
    compressionRatioSpinBox->setSuffix(":1");
    compressionRatioSpinBox->setSingleStep(0.5);

    profileEdit = new QLineEdit("Main420.10", encGroup);

    encLayout->addRow("Compression Ratio:", compressionRatioSpinBox);
    encLayout->addRow("Profile:", profileEdit);
    mainLayout->addWidget(encGroup);

    // Controls
    QHBoxLayout *btnLayout = new QHBoxLayout();
    startButton = new QPushButton("Start Stream", content);
    stopButton = new QPushButton("Stop Stream", content);
    stopButton->setEnabled(false);

    btnLayout->addWidget(startButton);
    btnLayout->addWidget(stopButton);
    mainLayout->addLayout(btnLayout);

    // Status
    statusLabel = new QLabel("Ready", content);
    statusLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(statusLabel);

    mainLayout->addStretch();
    setWidget(content);

    // Connect signals
    connect(startButton, &QPushButton::clicked, this, &JpegXSDock::onStartClicked);
    connect(stopButton, &QPushButton::clicked, this, &JpegXSDock::onStopClicked);
    connect(transportModeCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(onTransportModeChanged(int)));
    
    // Initialize visibility
    onTransportModeChanged(0);
}

JpegXSDock::~JpegXSDock() {
    if (jpegxs_output) {
        obs_output_stop(jpegxs_output);
        obs_output_release(jpegxs_output);
        jpegxs_output = nullptr;
    }
}

void JpegXSDock::onTransportModeChanged(int index) {
    bool isSRT = (index == 0);
    srtWidget->setVisible(isSRT);
    st2110Widget->setVisible(!isSRT);
}

void JpegXSDock::onStartClicked() {
    if (jpegxs_output) {
        return;
    }

    obs_data_t *settings = obs_data_create();
    
    QString modeData = transportModeCombo->currentData().toString();
    obs_data_set_string(settings, "transport_mode", modeData.toUtf8().constData());
    
    // SRT
    obs_data_set_string(settings, "srt_url", srtUrlEdit->text().toUtf8().constData());
    obs_data_set_int(settings, "srt_latency", latencySpinBox->value());
    obs_data_set_string(settings, "srt_passphrase", passphraseEdit->text().toUtf8().constData());
    
    // ST 2110
    obs_data_set_string(settings, "st2110_dest_ip", st2110DestIpEdit->text().toUtf8().constData());
    obs_data_set_int(settings, "st2110_dest_port", st2110DestPortSpin->value());
    obs_data_set_string(settings, "st2110_source_ip", st2110SourceIpEdit->text().toUtf8().constData());
    
    // Common
    obs_data_set_double(settings, "compression_ratio", compressionRatioSpinBox->value());
    obs_data_set_string(settings, "profile", profileEdit->text().toUtf8().constData());

    // Create output
    jpegxs_output = obs_output_create("jpegxs_output", "JPEG XS Stream", settings, nullptr);
    
    obs_data_release(settings);
    
    if (!jpegxs_output) {
        blog(LOG_ERROR, "[JPEG XS UI] Failed to create output");
        updateStatus("Failed to create output");
        return;
    }
    
    // Link to OBS video/audio
    obs_output_set_media(jpegxs_output, obs_get_video(), obs_get_audio());
    blog(LOG_INFO, "[JPEG XS UI] Media set to main video/audio");
    
    if (obs_output_start(jpegxs_output)) {
        startButton->setEnabled(false);
        stopButton->setEnabled(true);
        updateStatus("Streaming...");
        blog(LOG_INFO, "[JPEG XS UI] Stream started");
    } else {
        updateStatus("Failed to start");
        blog(LOG_ERROR, "[JPEG XS UI] Failed to start output");
        obs_output_release(jpegxs_output);
        jpegxs_output = nullptr;
    }
}

void JpegXSDock::onStopClicked() {
    if (jpegxs_output) {
        obs_output_stop(jpegxs_output);
        obs_output_release(jpegxs_output);
        jpegxs_output = nullptr;
        
        startButton->setEnabled(true);
        stopButton->setEnabled(false);
        updateStatus("Stopped");
        blog(LOG_INFO, "[JPEG XS UI] Stream stopped");
    }
}

void JpegXSDock::updateStatus(const QString &status) {
    statusLabel->setText(status);
}
