#include "main_window.hpp"
#include <QAction>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QToolBar>
#include <QVBoxLayout>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>

namespace sim::desktop {
namespace {
constexpr int poll_interval_ms = 50;
QString qt_text(std::string_view text) { return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size())); }
// Titles and messages can carry document text: never let a label auto-detect rich text.
QLabel* plain_label() {
    auto* label = new QLabel;
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}
QString entry_text(const FeedEntry& entry) {
    QString text = QStringLiteral("[%1] %2").arg(qt_text(severity_label(entry.severity)), qt_text(entry.title));
    if (!entry.message.empty()) { text += QStringLiteral(": ") + qt_text(entry.message); }
    if (!entry.context.empty()) { text += QStringLiteral(" (") + qt_text(entry.context) + QStringLiteral(")"); }
    return text;
}
void set_cell(QTableWidget* table, int row, int column, const QString& text) {
    auto* item = table->item(row, column);
    if (!item) {
        item = new QTableWidgetItem;
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        table->setItem(row, column, item);
    }
    if (item->text() != text) { item->setText(text); }
}
}
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), timer_(this) {
    setWindowTitle(QStringLiteral("Simulador Industrial"));
    resize(1100, 720);
    build_toolbar();
    build_panels();
    statusBar()->showMessage(QStringLiteral("Abra um projeto JSON para começar"));
    connect(&timer_, &QTimer::timeout, this, [this] { guarded([this] { on_poll(); }); });
    timer_.start(poll_interval_ms);
    guarded([this] { on_poll(); });
}
template<class F> void MainWindow::guarded(F&& action) noexcept {
    try {
        action();
    } catch (const std::exception& error) {
        report_callback_failure(error.what());
    } catch (...) {
        report_callback_failure("erro desconhecido");
    }
}
void MainWindow::report_callback_failure(const char* what) noexcept {
    try {
        // The timer can hit the same failure 20 times per second: show it once.
        const auto text = QStringLiteral("Falha na interface: ") + QString::fromUtf8(what);
        if (text == last_callback_failure_) { return; }
        last_callback_failure_ = text;
        diagnostics_->appendPlainText(text);
        statusBar()->showMessage(text);
    } catch (...) {
        // Out of options; the next timer tick tries again.
    }
}
void MainWindow::build_toolbar() {
    auto* bar = addToolBar(QStringLiteral("Sessão"));
    bar->setMovable(false);
    const auto add = [this, bar](const QString& text, auto handler) {
        auto* action = bar->addAction(text);
        connect(action, &QAction::triggered, this, [this, handler] { guarded([this, &handler] { handler(); on_poll(); }); });
        return action;
    };
    open_ = add(QStringLiteral("Abrir…"), [this] { choose_project(); });
    bar->addSeparator();
    start_ = add(QStringLiteral("Iniciar"), [this] { controller_.start(); });
    step_ = add(QStringLiteral("Avançar 1 tick"), [this] { controller_.step(); });
    ticks_ = new QSpinBox(bar);
    ticks_->setRange(1, 100000); // API horizon; replaced by the project's on open.
    ticks_->setValue(100);
    ticks_->setToolTip(QStringLiteral("Quantidade de ticks para avançar"));
    bar->addWidget(ticks_);
    advance_ = add(QStringLiteral("Avançar N ticks"), [this] { controller_.advance(static_cast<sim::core::Tick>(ticks_->value())); });
    bar->addSeparator();
    pause_ = add(QStringLiteral("Pausar"), [this] { controller_.pause(); });
    resume_ = add(QStringLiteral("Continuar"), [this] { controller_.resume(); });
    stop_ = add(QStringLiteral("Parar"), [this] { controller_.stop(); });
    bar->addSeparator();
    close_ = add(QStringLiteral("Encerrar sessão"), [this] { controller_.close_session(); });
}
void MainWindow::build_panels() {
    auto* state_box = new QGroupBox(QStringLiteral("Estado da simulação"));
    auto* form = new QFormLayout(state_box);
    project_label_ = plain_label();
    state_label_ = plain_label();
    tick_label_ = plain_label();
    image_label_ = plain_label();
    position_label_ = plain_label();
    motor_label_ = plain_label();
    discarded_label_ = plain_label();
    lost_label_ = plain_label();
    form->addRow(QStringLiteral("Projeto"), project_label_);
    form->addRow(QStringLiteral("Estado operacional"), state_label_);
    form->addRow(QStringLiteral("Próximo tick / horizonte"), tick_label_);
    form->addRow(QStringLiteral("Imagem de fim do tick"), image_label_);
    form->addRow(QStringLiteral("Posição da peça"), position_label_);
    form->addRow(QStringLiteral("Motor após controles"), motor_label_);
    form->addRow(QStringLiteral("Snapshots descartados"), discarded_label_);
    form->addRow(QStringLiteral("Eventos não exibidos (perdidos)"), lost_label_);
    failure_label_ = plain_label();
    failure_label_->setStyleSheet(QStringLiteral("color: white; background: #a4262c; padding: 6px;"));
    failure_label_->hide();
    form->addRow(failure_label_);

    auto* signal_box = new QGroupBox(QStringLiteral("Sinais e blocos funcionais"));
    auto* signal_layout = new QVBoxLayout(signal_box);
    signals_ = new QTableWidget(0, 3);
    signals_->setHorizontalHeaderLabels({QStringLiteral("Grupo"), QStringLiteral("Nome"), QStringLiteral("Valor")});
    signals_->verticalHeader()->hide();
    signals_->horizontalHeader()->setStretchLastSection(true);
    signal_layout->addWidget(signals_);

    auto* event_box = new QGroupBox(QStringLiteral("Eventos recentes"));
    auto* event_layout = new QVBoxLayout(event_box);
    events_ = new QPlainTextEdit;
    events_->setReadOnly(true);
    events_->setMaximumBlockCount(static_cast<int>(SessionController::max_event_rows));
    event_layout->addWidget(events_);

    auto* diagnostic_box = new QGroupBox(QStringLiteral("Diagnósticos"));
    auto* diagnostic_layout = new QVBoxLayout(diagnostic_box);
    diagnostics_ = new QPlainTextEdit;
    diagnostics_->setReadOnly(true);
    diagnostics_->setMaximumBlockCount(static_cast<int>(SessionController::max_feed_entries));
    diagnostic_layout->addWidget(diagnostics_);

    auto* top = new QSplitter(Qt::Horizontal);
    top->addWidget(state_box);
    top->addWidget(signal_box);
    auto* bottom = new QSplitter(Qt::Horizontal);
    bottom->addWidget(event_box);
    bottom->addWidget(diagnostic_box);
    auto* root = new QSplitter(Qt::Vertical);
    root->addWidget(top);
    root->addWidget(bottom);
    setCentralWidget(root);
}
void MainWindow::choose_project() {
    const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Abrir projeto"), QString(), QStringLiteral("Projeto JSON (*.json)"));
    if (!path.isEmpty()) { open_project(path); }
}
void MainWindow::open_project(const QString& path) {
    controller_.open(std::filesystem::path(path.toStdU16String()));
    statusBar()->showMessage(QStringLiteral("Validando projeto…"));
}
void MainWindow::on_poll() {
    const auto result = controller_.poll();
    if (result.project_changed) { show_project(); }
    if (result.state_changed) { show_state(); }
    if (result.signals_changed) { show_signals(); }
    if (result.events_changed) { show_events(result); }
    if (result.controls_changed) { apply_controls(); }
    for (const auto& entry : result.new_entries) { diagnostics_->appendPlainText(entry_text(entry)); }
    show_failure();
    for (const auto& entry : result.alerts) { alert(entry); }
    // Only from the timer or a button, never from closeEvent: Qt ignores a nested close().
    if (controller_.shutting_down() && controller_.idle()) { close(); }
}
void MainWindow::show_project() {
    const auto& project = controller_.project();
    if (!project) { return; }
    project_label_->setText(qt_text(project->title));
    project_label_->setToolTip(QString::fromStdU16String(project->path.u16string()));
    if (project->horizon > 0) { ticks_->setMaximum(static_cast<int>(project->horizon)); }
    statusBar()->showMessage(project->interactive_supported
        ? QStringLiteral("Projeto válido. Pronto para iniciar.")
        : QStringLiteral("Projeto válido, mas sem esteira: a API interativa não o executa."));
}
void MainWindow::show_state() {
    const auto& panel = controller_.state_panel();
    state_label_->setText(qt_text(panel.state));
    tick_label_->setText(qt_text(panel.tick));
    image_label_->setText(qt_text(panel.image_tick));
    position_label_->setText(qt_text(panel.position));
    motor_label_->setText(qt_text(panel.control_motor));
    discarded_label_->setText(qt_text(panel.discarded));
    lost_label_->setText(qt_text(panel.lost_events));
    statusBar()->showMessage(qt_text(panel.state) + QStringLiteral(" | tick ") + qt_text(panel.tick));
}
void MainWindow::show_signals() {
    const auto& panel = controller_.signal_panel();
    const struct { const char* group; const std::vector<Row>& rows; } groups[]{
        {"Entradas PLC", panel.inputs}, {"Saídas PLC", panel.outputs}, {"Motor e sensor", panel.plant}, {"Blocos", panel.blocks}};
    int row = 0;
    for (const auto& group : groups) { row += static_cast<int>(group.rows.size()); }
    if (signals_->rowCount() != row) { signals_->setRowCount(row); }
    row = 0;
    for (const auto& group : groups) {
        const auto name = QString::fromUtf8(group.group);
        for (const auto& item : group.rows) {
            set_cell(signals_, row, 0, name);
            set_cell(signals_, row, 1, qt_text(item.name));
            set_cell(signals_, row, 2, qt_text(item.value));
            ++row;
        }
    }
}
void MainWindow::show_events(const PollResult& result) {
    if (result.events_reset) { events_->clear(); }
    // Appending keeps the reader's scroll position and costs only the new rows.
    const auto& rows = controller_.events();
    for (auto i = rows.size() - result.events_added; i < rows.size(); ++i) { events_->appendPlainText(qt_text(rows[i].text)); }
}
void MainWindow::show_failure() {
    const auto& failure = controller_.failure();
    if (failure) {
        const auto text = QStringLiteral("Causa da falha: ") + entry_text(*failure);
        if (failure_label_->text() != text) { failure_label_->setText(text); }
    }
    failure_label_->setVisible(failure.has_value());
}
void MainWindow::apply_controls() {
    const auto& c = controller_.controls();
    open_->setEnabled(c.open);
    start_->setEnabled(c.start);
    step_->setEnabled(c.step);
    advance_->setEnabled(c.advance);
    ticks_->setEnabled(c.advance);
    pause_->setEnabled(c.pause);
    resume_->setEnabled(c.resume);
    stop_->setEnabled(c.stop);
    close_->setEnabled(c.close);
}
void MainWindow::alert(const FeedEntry& entry) {
    // Non-modal on purpose: the timer keeps polling and the session keeps being observed.
    auto* box = new QMessageBox(this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setIcon(QMessageBox::Critical);
    box->setTextFormat(Qt::PlainText);
    box->setWindowTitle(QStringLiteral("Falha"));
    box->setText(qt_text(entry.title) + QStringLiteral("\n\n") + qt_text(entry.message));
    if (!entry.context.empty()) { box->setDetailedText(qt_text(entry.context)); }
    box->setModal(false);
    box->show();
}
void MainWindow::closeEvent(QCloseEvent* event) {
    // The controller owns the session and the running tasks; the window closes only after they end.
    event->ignore();
    guarded([this, event] {
        if (controller_.idle()) {
            timer_.stop();
            event->accept();
            return;
        }
        controller_.request_shutdown();
        statusBar()->showMessage(QStringLiteral("Encerrando…"));
    });
}
}
