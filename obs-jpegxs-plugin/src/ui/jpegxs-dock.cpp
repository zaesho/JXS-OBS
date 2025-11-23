#include "jpegxs-dock.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QTimer>
#include <obs-module.h>
#include <obs-frontend-api.h>

// Global output instance to manage state
static obs_output_t *jpegxs_output = nullptr;

JpegXSDock::JpegXSDock(QWidget *parent) : QDockWidget(parent) {
    setWindowTitle("JPEG XS Manager");
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);

    QWidget *content = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(content);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    mainTabs = new QTabWidget(content);
    mainTabs->addTab(setupTransmitterTab(), "Transmitter (Output)");
    mainTabs->addTab(setupReceiverTab(), "Receiver (Input)");
    
    mainLayout->addWidget(mainTabs);
    setWidget(content);
    
    // Initial State
    onTransportModeChanged(0);
    
    // Auto-refresh source list periodically or on tab change?
    connect(mainTabs, &QTabWidget::currentChanged, [this](int index) {
        // if (index == 1) refreshSourceList();
    });
}

JpegXSDock::~JpegXSDock() {
    if (jpegxs_output) {
        obs_output_stop(jpegxs_output);
        obs_output_release(jpegxs_output);
        jpegxs_output = nullptr;
    }
}

// ==============================================================================================
// TRANSMITTER TAB (Output)
// ==============================================================================================

QWidget* JpegXSDock::setupTransmitterTab() {
    QWidget *tab = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(tab);
    
    // Transport Selection
    QGroupBox *transportGroup = new QGroupBox("Transport Protocol", tab);
    QVBoxLayout *transportLayout = new QVBoxLayout(transportGroup);
    
    transportModeCombo = new QComboBox(transportGroup);
    transportModeCombo->addItem("SRT (Reliable Internet)", "SRT");
    transportModeCombo->addItem("ST 2110-22 (UDP/Multicast)", "ST 2110-22 (UDP/Multicast)");
    transportLayout->addWidget(transportModeCombo);
    
    layout->addWidget(transportGroup);

    // SRT Settings Area
    srtWidget = new QGroupBox("SRT Configuration", tab);
    QFormLayout *srtLayout = new QFormLayout(srtWidget);
    
    srtUrlEdit = new QLineEdit("srt://127.0.0.1:9000?mode=caller", srtWidget);
    passphraseEdit = new QLineEdit(srtWidget);
    passphraseEdit->setEchoMode(QLineEdit::Password);
    latencySpinBox = new QSpinBox(srtWidget);
    latencySpinBox->setRange(10, 8000);
    latencySpinBox->setValue(20);
    latencySpinBox->setSuffix(" ms");
    
    srtLayout->addRow("Destination URL:", srtUrlEdit);
    srtLayout->addRow("Passphrase:", passphraseEdit);
    srtLayout->addRow("Latency:", latencySpinBox);
    layout->addWidget(srtWidget);
    
    // ST 2110 Container
    st2110Widget = new QGroupBox("ST 2110-22 Configuration", tab);
    QFormLayout *st2110L = new QFormLayout(st2110Widget);
    
    st2110DestIpEdit = new QLineEdit("239.1.1.1", st2110Widget);
    st2110DestPortSpin = new QSpinBox(st2110Widget);
    st2110DestPortSpin->setRange(1024, 65535);
    st2110DestPortSpin->setValue(5000);
    
    st2110AudioPortSpin = new QSpinBox(st2110Widget);
    st2110AudioPortSpin->setRange(1024, 65535);
    st2110AudioPortSpin->setValue(5002);
    
    st2110SourceIpEdit = new QLineEdit(st2110Widget);
    st2110SourceIpEdit->setPlaceholderText("0.0.0.0 (Optional)");
    
    disablePacingCheckbox = new QCheckBox("Disable Pacing (Burst Mode) - Low Latency", st2110Widget);
    disablePacingCheckbox->setChecked(true);
    
    awsCompatCheckbox = new QCheckBox("AWS MediaConnect Compatibility (jxsv)", st2110Widget);
    awsCompatCheckbox->setChecked(false);
    
    enableAudioCheckbox = new QCheckBox("Enable ST 2110-30 Audio", st2110Widget);
    enableAudioCheckbox->setChecked(true);
    
    st2110L->addRow("Multicast IP:", st2110DestIpEdit);
    st2110L->addRow("Video Port:", st2110DestPortSpin);
    st2110L->addRow("Audio Port:", st2110AudioPortSpin);
    st2110L->addRow("Interface IP:", st2110SourceIpEdit);
    st2110L->addRow("", disablePacingCheckbox);
    st2110L->addRow("", awsCompatCheckbox);
    st2110L->addRow("", enableAudioCheckbox);
    layout->addWidget(st2110Widget);
    
    // Encoder Settings
    QGroupBox *encGroup = new QGroupBox("Encoder Settings", tab);
    QFormLayout *encLayout = new QFormLayout(encGroup);

    compressionRatioSpinBox = new QDoubleSpinBox(encGroup);
    compressionRatioSpinBox->setRange(2.0, 100.0);
    compressionRatioSpinBox->setValue(10.0);
    compressionRatioSpinBox->setSuffix(":1");
    compressionRatioSpinBox->setSingleStep(0.5);

    profileCombo = new QComboBox(encGroup);
    profileCombo->addItem("Main 4:2:0 8-bit", "Main420.8");
    profileCombo->addItem("Main 4:2:0 10-bit", "Main420.10");
    profileCombo->addItem("High 4:2:2 8-bit", "High422.8");
    profileCombo->addItem("High 4:2:2 10-bit", "High422.10");
    profileCombo->addItem("High 4:4:4 8-bit", "High444.8");
    profileCombo->addItem("High 4:4:4 10-bit", "High444.10");

    encLayout->addRow("Compression Ratio:", compressionRatioSpinBox);
    encLayout->addRow("Profile:", profileCombo);
    layout->addWidget(encGroup);

    // Controls
    QHBoxLayout *btnLayout = new QHBoxLayout();
    startButton = new QPushButton("START STREAM", tab);
    startButton->setStyleSheet("background-color: #2ea043; color: white; font-weight: bold; padding: 8px;");
    stopButton = new QPushButton("STOP STREAM", tab);
    stopButton->setStyleSheet("background-color: #da3633; color: white; font-weight: bold; padding: 8px;");
    stopButton->setEnabled(false);

    btnLayout->addWidget(startButton);
    btnLayout->addWidget(stopButton);
    layout->addLayout(btnLayout);

    // Status
    statusLabel = new QLabel("Ready", tab);
    statusLabel->setAlignment(Qt::AlignCenter);
    statusLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #888;");
    layout->addWidget(statusLabel);

    layout->addStretch();
    
    // Connect signals
    connect(startButton, &QPushButton::clicked, this, &JpegXSDock::onStartClicked);
    connect(stopButton, &QPushButton::clicked, this, &JpegXSDock::onStopClicked);
    connect(transportModeCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(onTransportModeChanged(int)));
    
    return tab;
}

void JpegXSDock::onTransportModeChanged(int index) {
    bool isSRT = (index == 0);
    srtWidget->setVisible(isSRT);
    st2110Widget->setVisible(!isSRT);
}

void JpegXSDock::onStartClicked() {
    blog(LOG_INFO, "[JPEG XS UI] Start button clicked");
    if (jpegxs_output) return;

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
    obs_data_set_int(settings, "st2110_audio_port", st2110AudioPortSpin->value());
    obs_data_set_string(settings, "st2110_source_ip", st2110SourceIpEdit->text().toUtf8().constData());
    obs_data_set_bool(settings, "disable_pacing", disablePacingCheckbox->isChecked());
    obs_data_set_bool(settings, "st2110_aws_compat", awsCompatCheckbox->isChecked());
    obs_data_set_bool(settings, "st2110_audio_enabled", enableAudioCheckbox->isChecked());
    
    // Common
    obs_data_set_double(settings, "compression_ratio", compressionRatioSpinBox->value());
    obs_data_set_string(settings, "profile", profileCombo->currentData().toString().toUtf8().constData());

    // Create output
    jpegxs_output = obs_output_create("jpegxs_output", "JPEG XS Stream", settings, nullptr);
    
    obs_data_release(settings);
    
    if (!jpegxs_output) {
        updateStatus("Failed to create output");
        return;
    }
    
    obs_output_set_media(jpegxs_output, obs_get_video(), obs_get_audio());
    
    if (obs_output_start(jpegxs_output)) {
        startButton->setEnabled(false);
        stopButton->setEnabled(true);
        updateStatus("STREAMING");
        statusLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #2ea043;");
    } else {
        updateStatus("Failed to start");
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
        updateStatus("STOPPED");
        statusLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #da3633;");
    }
}

void JpegXSDock::updateStatus(const QString &status) {
    statusLabel->setText(status);
}

// ==============================================================================================
// RECEIVER TAB (Input)
// ==============================================================================================

QWidget* JpegXSDock::setupReceiverTab() {
    QWidget *tab = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(tab);
    
    QLabel *placeholder = new QLabel("Receiver management temporarily disabled due to stability issues.", tab);
    placeholder->setAlignment(Qt::AlignCenter);
    layout->addWidget(placeholder);
    
    /*
    // Top Bar: Add / Refresh
    QHBoxLayout *topBar = new QHBoxLayout();
    addSourceButton = new QPushButton("Add New Source", tab);
    refreshButton = new QPushButton("Refresh List", tab);
    topBar->addWidget(addSourceButton);
    topBar->addWidget(refreshButton);
    layout->addLayout(topBar);
    
    // Source List
    sourceListTable = new QTableWidget(tab);
    sourceListTable->setColumnCount(2);
    sourceListTable->setHorizontalHeaderLabels({"Source Name", "Type"});
    sourceListTable->horizontalHeader()->setStretchLastSection(true);
    sourceListTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    sourceListTable->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(sourceListTable);
    
    // Inspector (Edit selected source)
    inspectorGroup = new QGroupBox("Source Settings", tab);
    QFormLayout *inspLayout = new QFormLayout(inspectorGroup);
    
    inspSourceName = new QLineEdit(inspectorGroup);
    inspSourceName->setReadOnly(true);
    
    inspTransportMode = new QComboBox(inspectorGroup);
    inspTransportMode->addItem("SRT", "SRT");
    inspTransportMode->addItem("ST 2110", "ST 2110-22 (UDP/Multicast)");
    
    inspSrtUrl = new QLineEdit(inspectorGroup);
    inspLatency = new QSpinBox(inspectorGroup);
    inspLatency->setRange(0, 10000);
    
    inspMulticastIp = new QLineEdit(inspectorGroup);
    inspPort = new QSpinBox(inspectorGroup);
    inspPort->setRange(1024, 65535);
    
    applySourceBtn = new QPushButton("Apply Changes", inspectorGroup);
    
    inspLayout->addRow("Name:", inspSourceName);
    inspLayout->addRow("Transport:", inspTransportMode);
    inspLayout->addRow("SRT URL:", inspSrtUrl);
    inspLayout->addRow("Latency:", inspLatency);
    inspLayout->addRow("Multicast IP:", inspMulticastIp);
    inspLayout->addRow("Port:", inspPort);
    inspLayout->addRow(applySourceBtn);
    
    inspectorGroup->setEnabled(false);
    layout->addWidget(inspectorGroup);
    
    // Connects
    connect(refreshButton, &QPushButton::clicked, this, &JpegXSDock::onRefreshSources);
    connect(addSourceButton, &QPushButton::clicked, this, &JpegXSDock::onAddSourceClicked);
    connect(sourceListTable, &QTableWidget::itemSelectionChanged, this, &JpegXSDock::onSourceSelectionChanged);
    connect(applySourceBtn, &QPushButton::clicked, this, &JpegXSDock::onApplySourceSettings);
    */
    
    return tab;
}

void JpegXSDock::onRefreshSources() {
    sourceListTable->setRowCount(0);
    inspectorGroup->setEnabled(false);
    
    struct obs_frontend_source_list sources = {};
    obs_frontend_get_scenes(&sources);
    
    // We need to iterate all sources to find jpegxs_source inputs
    // obs_frontend_get_scenes only returns scenes.
    // Using obs_enum_sources is better.
    
    auto cb = [](void *param, obs_source_t *source) {
        QTableWidget *table = static_cast<QTableWidget*>(param);
        const char *id = obs_source_get_id(source);
        if (strcmp(id, "jpegxs_source") == 0) {
            const char *name = obs_source_get_name(source);
            int row = table->rowCount();
            table->insertRow(row);
            table->setItem(row, 0, new QTableWidgetItem(name));
            table->setItem(row, 1, new QTableWidgetItem("JPEG XS Input"));
        }
        return true;
    };
    
    obs_enum_sources(cb, sourceListTable);
    
    obs_frontend_source_list_free(&sources);
}

void JpegXSDock::onAddSourceClicked() {
    // Create a new source in the current scene
    obs_source_t *current_scene_source = obs_frontend_get_current_scene();
    if (!current_scene_source) {
        QMessageBox::warning(this, "Error", "No active scene found.");
        return;
    }
    
    obs_scene_t *scene = obs_scene_from_source(current_scene_source);
    if (!scene) {
        obs_source_release(current_scene_source);
        return;
    }
    
    // Create source
    obs_data_t *settings = obs_data_create();
    obs_source_t *new_source = obs_source_create("jpegxs_source", "JPEG XS Input", settings, nullptr);
    obs_data_release(settings);
    
    if (new_source) {
        obs_scene_add(scene, new_source);
        obs_source_release(new_source);
        onRefreshSources();
    }
    
    obs_source_release(current_scene_source);
}

void JpegXSDock::onSourceSelectionChanged() {
    QList<QTableWidgetItem*> selected = sourceListTable->selectedItems();
    if (selected.isEmpty()) {
        inspectorGroup->setEnabled(false);
        return;
    }
    
    QString sourceName = sourceListTable->item(selected[0]->row(), 0)->text();
    loadSourceSettings(sourceName);
}

void JpegXSDock::loadSourceSettings(const QString &sourceName) {
    obs_source_t *source = obs_get_source_by_name(sourceName.toUtf8().constData());
    if (!source) return;
    
    obs_data_t *settings = obs_source_get_settings(source);
    
    inspSourceName->setText(sourceName);
    
    const char *mode = obs_data_get_string(settings, "transport_mode");
    int modeIdx = (strcmp(mode, "SRT") == 0) ? 0 : 1;
    inspTransportMode->setCurrentIndex(modeIdx);
    
    inspSrtUrl->setText(obs_data_get_string(settings, "srt_url"));
    inspLatency->setValue(obs_data_get_int(settings, "srt_latency"));
    
    inspMulticastIp->setText(obs_data_get_string(settings, "st2110_multicast_ip"));
    inspPort->setValue(obs_data_get_int(settings, "st2110_port"));
    
    obs_data_release(settings);
    obs_source_release(source);
    
    inspectorGroup->setEnabled(true);
}

void JpegXSDock::onApplySourceSettings() {
    QString sourceName = inspSourceName->text();
    obs_source_t *source = obs_get_source_by_name(sourceName.toUtf8().constData());
    if (!source) return;
    
    obs_data_t *settings = obs_data_create();
    
    QString mode = inspTransportMode->currentText();
    if (mode.contains("SRT")) mode = "SRT";
    else mode = "ST 2110-22 (UDP/Multicast)";
    
    obs_data_set_string(settings, "transport_mode", mode.toUtf8().constData());
    obs_data_set_string(settings, "srt_url", inspSrtUrl->text().toUtf8().constData());
    obs_data_set_int(settings, "srt_latency", inspLatency->value());
    
    obs_data_set_string(settings, "st2110_multicast_ip", inspMulticastIp->text().toUtf8().constData());
    obs_data_set_int(settings, "st2110_port", inspPort->value());
    
    obs_source_update(source, settings);
    
    obs_data_release(settings);
    obs_source_release(source);
}
