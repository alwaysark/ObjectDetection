#include <QApplication>
#include "MainWindow.h"

// 程序入口：Qt GUI 程序固定三步——建 QApplication、显示主窗口、进事件循环
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    MainWindow w;
    w.show();

    return QApplication::exec();
}
