// main.cpp
//
// Entry point for the Photo‑Triage C++ application. It simply
// instantiates QApplication and the main window.
#include <QIcon>
#include "phototriagewindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QApplication::setWindowIcon(QIcon(":/app.ico"));

    PhotoTriageWindow window;
    window.show();
    return app.exec();
}
