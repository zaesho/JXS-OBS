#pragma once

#include <QDockWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QFormLayout>
#include <QComboBox>

class JpegXSDock : public QDockWidget {
    Q_OBJECT

public:
    explicit JpegXSDock(QWidget *parent = nullptr);
    ~JpegXSDock();

private slots:
    void onStartClicked();
    void onStopClicked();
    void onTransportModeChanged(int index);

private:
    // Transport Mode
    QComboBox *transportModeCombo;
    
    // SRT Settings
    QLineEdit *srtUrlEdit;
    QSpinBox *latencySpinBox;
    QLineEdit *passphraseEdit;
    
    // UDP / ST 2110 Settings
    QLineEdit *st2110DestIpEdit;
    QSpinBox *st2110DestPortSpin;
    QLineEdit *st2110SourceIpEdit;
    
    // Common
    QDoubleSpinBox *compressionRatioSpinBox;
    QLineEdit *profileEdit;
    
    // Controls
    QPushButton *startButton;
    QPushButton *stopButton;
    QLabel *statusLabel;
    
    // Layout helpers for visibility toggling
    QWidget *srtWidget;
    QWidget *st2110Widget;
    
    void updateStatus(const QString &status);
};
