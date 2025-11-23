#pragma once

#include <QDockWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QFormLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QListWidget>
#include <QGroupBox>

class JpegXSDock : public QDockWidget {
    Q_OBJECT

public:
    explicit JpegXSDock(QWidget *parent = nullptr);
    ~JpegXSDock();

private slots:
    // Transmitter Slots
    void onStartClicked();
    void onStopClicked();
    void onTransportModeChanged(int index);
    
    // Receiver Slots
    void onRefreshSources();
    void onAddSourceClicked();
    void onSourceSelectionChanged();
    void onApplySourceSettings();

private:
    QTabWidget *mainTabs;
    
    // --- Transmitter (Output) UI ---
    QWidget *setupTransmitterTab();
    
    QComboBox *transportModeCombo;
    QLineEdit *srtUrlEdit;
    QSpinBox *latencySpinBox;
    QLineEdit *passphraseEdit;
    
    QLineEdit *st2110DestIpEdit;
    QSpinBox *st2110DestPortSpin;
    QSpinBox *st2110AudioPortSpin;
    QLineEdit *st2110SourceIpEdit;
    QCheckBox *disablePacingCheckbox;
    QCheckBox *awsCompatCheckbox;
    QCheckBox *enableAudioCheckbox;
    
    QDoubleSpinBox *compressionRatioSpinBox;
    QComboBox *profileCombo;
    
    QPushButton *startButton;
    QPushButton *stopButton;
    QLabel *statusLabel;
    
    QWidget *srtWidget;
    QWidget *st2110Widget;
    
    void updateStatus(const QString &status);

    // --- Receiver (Input) UI ---
    QWidget *setupReceiverTab();
    
    QTableWidget *sourceListTable;
    QPushButton *refreshButton;
    QPushButton *addSourceButton;
    
    // Selected Source Inspector
    QGroupBox *inspectorGroup;
    QLineEdit *inspSourceName;
    QComboBox *inspTransportMode;
    QLineEdit *inspSrtUrl;
    QSpinBox *inspLatency;
    QLineEdit *inspMulticastIp;
    QSpinBox *inspPort;
    QPushButton *applySourceBtn;
    
    // Helper to populate source list
    void refreshSourceList();
    // Helper to load settings from OBS source to UI
    void loadSourceSettings(const QString &sourceName);
};
