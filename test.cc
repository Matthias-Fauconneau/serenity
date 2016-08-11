#include <QtCore/QCoreApplication>
#include <QtWidgets/QAction> // Qt5Core Qt5Gui Qt5Widgets
#include <KF5/KGlobalAccel/KGlobalAccel> // KF5GlobalAccel

#include "thread.h"

int main(int argc, char** argv) {
 QCoreApplication app(argc, argv);
 QAction action("Increase Screen Brightness", &app);
 action.setObjectName("serenity");
 QObject::connect(&action, &QAction::triggered, [](bool){log("Triggered");});
 KGlobalAccel::self()->setShortcut(&action, QList<QKeySequence>() << Qt::Key_F8, KGlobalAccel::NoAutoloading);
 return app.exec();
}
