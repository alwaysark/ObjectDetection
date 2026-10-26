#include <iostream>
#include <QApplication>
#include "MainWindow.h"
#include <windows.h>

int main(int argc, char *argv[]) {

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);   // 让控制台按 UTF-8 解释输出
#endif

    QApplication app(argc, argv);
    std::cout << "Hello World! 你好" << std::endl;
    MainWindow w;
    w.show();

    return QApplication::exec();
}
