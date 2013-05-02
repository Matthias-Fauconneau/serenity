#include "core.h"
#include "vector.h"
#include "image.h"

#include <QFile>
#include <QDir>
#include <QApplication>
#include <QSettings>
#include <QTime>
#include <QTimer>
#include <QImage>
#include <QMouseEvent>
#include <QMainWindow>
#include <QToolBar>
#include <QAction>
#include <QFileDialog>
#include <QSpinBox>
#include <QSlider>
#include <QLabel>
#include <QComboBox>
#include <QVBoxLayout>

/// Browses volume data sets
class App : QLabel {
    Q_OBJECT
public:
    App();
    enum Pass { Source, Smooth, Threshold, Distance, Maximum };
public slots:
    void setFolder(QString path);
    void setPath(QString path);
    bool setSlice(int index);
    void setPass(int pass);
    void clear();
    void mapSourceVolume();
    bool mapTargetVolume();
    void updateView();\

protected:
    void mousePressEvent(QMouseEvent *ev);
    void mouseMoveEvent(QMouseEvent* ev);

private:
    void swapPass();

private:
    QString path, name;
    QFile* sourceFile; QString sourcePath; Volume source;
    QFile* targetFile; QString targetPath; Volume target;
    Volume renderVolume;

    int previousPass;
    int currentPass;
    int currentSlice;
    float densityThreshold;

    QPoint lastPos;
    vec2 rotation; // Current view angles (yaw,pitch)

    QSettings settings;
    QFileDialog volumeSelector;
    QToolBar* toolbar;
    QSpinBox* spinbox;
    QSlider* slider;
    QComboBox* passSelector;
    QMainWindow window;
};
