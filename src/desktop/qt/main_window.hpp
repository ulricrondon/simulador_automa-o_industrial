#pragma once
#include "../presentation/session_controller.hpp"
#include <QMainWindow>
#include <QString>
#include <QTimer>
#include <vector>

class QAction;
class QCloseEvent;
class QLabel;
class QPlainTextEdit;
class QSpinBox;
class QTableWidget;

namespace sim::desktop {
// Connects widgets to SessionController through lambdas, so it needs no Q_OBJECT or moc.
class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
    void open_project(const QString& path);
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    void build_toolbar();
    void build_panels();
    void choose_project();
    void on_poll();
    // Qt does not support exceptions crossing the event loop: every callback body runs in here.
    template<class F> void guarded(F&& action) noexcept;
    void report_callback_failure(const char* what) noexcept;
    void show_project();
    void show_state();
    void show_signals();
    void show_events(const PollResult& result);
    void show_failure();
    void apply_controls();
    void alert(const FeedEntry& entry);
    SessionController controller_;
    QTimer timer_;
    QString last_callback_failure_;
    QAction* open_{}, *start_{}, *step_{}, *advance_{}, *pause_{}, *resume_{}, *stop_{}, *close_{};
    QSpinBox* ticks_{};
    QLabel* project_label_{}, *state_label_{}, *tick_label_{}, *image_label_{}, *position_label_{}, *motor_label_{}, *discarded_label_{}, *lost_label_{}, *failure_label_{};
    QTableWidget* signals_{};
    QPlainTextEdit* events_{}, *diagnostics_{};
};
}
