#include "main_window.hpp"
#include <QApplication>
#include <QMessageBox>
#include <QStringList>
#include <exception>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Simulador Industrial"));
    try {
        sim::desktop::MainWindow window;
        window.show();
        const auto arguments = QApplication::arguments();
        if (arguments.size() > 1) { window.open_project(arguments.at(1)); }
        return QApplication::exec();
    } catch (const std::exception& error) {
        QMessageBox::critical(nullptr, QStringLiteral("Simulador Industrial"), QString::fromUtf8(error.what()));
    } catch (...) {
        QMessageBox::critical(nullptr, QStringLiteral("Simulador Industrial"), QStringLiteral("Erro desconhecido ao iniciar."));
    }
    return 1;
}
