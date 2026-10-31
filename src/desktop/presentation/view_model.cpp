#include "view_model.hpp"
#include <algorithm>
#include <tuple>

namespace sim::desktop {
namespace {
using runtime::CommandCode;
using runtime::CommandKind;
using runtime::DiagnosticReason;
using runtime::DiagnosticStage;
using runtime::InteractiveState;
std::string number(std::uint64_t value) { return std::to_string(value); }
std::string text(core::DigitalState state) { return std::string(core::name(state)); }
std::string neutral() { return std::string(neutral_value); }
std::string_view phase_label(core::Phase phase) {
    switch (phase) {
    case core::Phase::external: return "externa";
    case core::Phase::sample: return "amostragem";
    case core::Phase::ladder: return "ladder";
    case core::Phase::drive: return "acionamento";
    case core::Phase::mechanics: return "mecânica";
    case core::Phase::sensor: return "sensor";
    case core::Phase::control: return "controle";
    }
    return "?";
}
std::string_view transition_label(core::TransitionKind kind) {
    switch (kind) {
    case core::TransitionKind::command: return "comando";
    case core::TransitionKind::motor_power: return "alimentação do motor";
    case core::TransitionKind::sensor_power: return "alimentação do sensor";
    case core::TransitionKind::plc_input: return "entrada PLC";
    case core::TransitionKind::plc_output: return "saída PLC";
    case core::TransitionKind::motor: return "motor";
    case core::TransitionKind::sensor: return "sensor";
    case core::TransitionKind::piece: return "peça";
    case core::TransitionKind::paused: return "pausa";
    case core::TransitionKind::resumed: return "continuação";
    case core::TransitionKind::stopped: return "parada";
    }
    return "?";
}
}
std::string_view state_label(InteractiveState state) {
    switch (state) {
    case InteractiveState::loaded: return "Carregada";
    case InteractiveState::running: return "Em execução";
    case InteractiveState::paused: return "Pausada";
    case InteractiveState::completed: return "Concluída";
    case InteractiveState::stopped: return "Parada";
    case InteractiveState::closed: return "Encerrada";
    case InteractiveState::failed: return "Falha";
    }
    return "?";
}
std::string_view command_label(CommandKind kind) {
    switch (kind) {
    case CommandKind::start: return "iniciar";
    case CommandKind::step: return "avançar um tick";
    case CommandKind::pause: return "pausar";
    case CommandKind::resume: return "continuar";
    case CommandKind::stop: return "parar";
    case CommandKind::input: return "entrada";
    case CommandKind::advance: return "avançar N ticks";
    }
    return "?";
}
std::string_view code_label(CommandCode code) {
    switch (code) {
    case CommandCode::accepted: return "aceito";
    case CommandCode::applied: return "aplicado";
    case CommandCode::invalid: return "inválido";
    case CommandCode::stale: return "obsoleto";
    case CommandCode::queue_full: return "fila cheia";
    case CommandCode::closed: return "sessão encerrada";
    case CommandCode::history_limit: return "limite atingido";
    case CommandCode::cancelled: return "cancelado";
    case CommandCode::execution_error: return "erro de execução";
    }
    return "?";
}
std::string_view reason_label(DiagnosticReason reason) {
    switch (reason) {
    case DiagnosticReason::session_closed: return "Sessão encerrada";
    case DiagnosticReason::wrong_session: return "Comando de outra sessão";
    case DiagnosticReason::stale_id: return "Identificador de comando obsoleto";
    case DiagnosticReason::invalid_command: return "Comando inválido";
    case DiagnosticReason::invalid_value: return "Valor inválido";
    case DiagnosticReason::invalid_target: return "Destino inválido";
    case DiagnosticReason::horizon: return "Horizonte da simulação";
    case DiagnosticReason::stop_pending: return "Parada já solicitada";
    case DiagnosticReason::queue_capacity: return "Fila de comandos cheia";
    case DiagnosticReason::control_budget: return "Orçamento de controles esgotado";
    case DiagnosticReason::input_budget: return "Orçamento de entradas esgotado";
    case DiagnosticReason::advance_budget: return "Orçamento de avanços esgotado";
    case DiagnosticReason::snapshot_budget: return "Limite de snapshots do histórico";
    case DiagnosticReason::event_budget: return "Limite de eventos do histórico";
    case DiagnosticReason::invalid_state: return "Operação inválida no estado atual";
    case DiagnosticReason::stale_boundary: return "Fronteira de tick já passada";
    case DiagnosticReason::advance_active: return "Avanço já em andamento";
    case DiagnosticReason::cancelled: return "Comando cancelado";
    case DiagnosticReason::known_exception: return "Erro de execução";
    case DiagnosticReason::unknown_exception: return "Erro interno de execução";
    case DiagnosticReason::allocation_failure: return "Memória insuficiente";
    }
    return "?";
}
std::string_view stage_label(DiagnosticStage stage) {
    switch (stage) {
    case DiagnosticStage::admission: return "admissão";
    case DiagnosticStage::application: return "aplicação";
    case DiagnosticStage::execution: return "execução";
    }
    return "?";
}
std::string_view project_error_label(project::ErrorCode code) {
    switch (code) {
    case project::ErrorCode::invalid_json: return "JSON inválido";
    case project::ErrorCode::schema: return "Documento fora do schema";
    case project::ErrorCode::unsupported_version: return "Versão não suportada";
    case project::ErrorCode::semantic: return "Projeto inconsistente";
    case project::ErrorCode::integrity: return "Falha de integridade";
    case project::ErrorCode::io: return "Falha ao ler o arquivo";
    case project::ErrorCode::limit: return "Limite do documento excedido";
    }
    return "?";
}
std::string_view severity_label(Severity severity) {
    switch (severity) {
    case Severity::fatal: return "FALHA";
    case Severity::recoverable: return "Rejeição";
    case Severity::secondary: return "Informação";
    }
    return "?";
}
Severity classify(const runtime::SessionDiagnostic& diagnostic, bool causes_failure) {
    if (causes_failure) { return Severity::fatal; }
    switch (diagnostic.reason) {
    case DiagnosticReason::snapshot_budget:
    case DiagnosticReason::event_budget:
    case DiagnosticReason::known_exception:
    case DiagnosticReason::unknown_exception:
    case DiagnosticReason::allocation_failure:
        return Severity::fatal;
    case DiagnosticReason::cancelled:
        return Severity::secondary;
    default:
        return Severity::recoverable;
    }
}
std::string context_text(const runtime::DiagnosticContext& context) {
    std::string out = "etapa=";
    out += stage_label(context.stage);
    const auto add = [&out](std::string_view name, std::uint64_t value) {
        out += ' '; out += name; out += '='; out += number(value);
    };
    if (context.command) { out += " comando="; out += command_label(*context.command); }
    if (context.command_id) { add("id", *context.command_id); }
    if (context.tick) { add("tick", *context.tick); }
    if (context.state) { out += " estado="; out += state_label(*context.state); }
    if (context.used) { add("usado", *context.used); }
    if (context.limit) { add("limite", *context.limit); }
    if (context.requested) { add("solicitado", *context.requested); }
    return out;
}
ControlState compute_controls(const ControlInput& in) {
    using S = InteractiveState;
    ControlState c;
    const bool idle = !in.session_active && !in.loading && !in.shutting_down;
    c.open = idle;
    c.start = idle && in.project_loaded && in.interactive_supported;
    c.close = in.session_active && !in.closing;
    if (!in.session_active || in.closing || in.shutting_down || !in.state) { return c; }
    const bool running = *in.state == S::running;
    const bool can_advance = running && !in.advance_in_flight && !in.stop_in_flight && in.next_tick < in.horizon;
    c.step = can_advance;
    c.advance = can_advance;
    c.pause = running && !in.stop_in_flight;
    c.resume = *in.state == S::paused && !in.stop_in_flight;
    // A failed session keeps its functional state until stop applies the real reset.
    c.stop = !in.stop_in_flight && (running || *in.state == S::paused || *in.state == S::failed || *in.state == S::loaded);
    return c;
}
StatePanel make_state_panel(const runtime::SessionPublication* publication, core::Tick horizon,
                            const std::optional<core::SessionStatus>& control, const core::RunSpec* configuration,
                            std::uint64_t lost_events) {
    StatePanel panel;
    if (configuration) { panel.tick = "0 / " + number(horizon); }
    if (!publication) { return panel; }
    panel.state = state_label(publication->state);
    panel.tick = number(publication->next_tick) + " / " + number(horizon);
    panel.sequence = publication->sequence;
    panel.discarded = number(publication->discarded_snapshots);
    panel.lost_events = number(lost_events);
    if (publication->latest) {
        panel.image_tick = number(publication->latest->tick);
        const auto position = publication->latest->position_um;
        panel.position = std::to_string(position) + " µm";
        if (configuration && configuration->conveyor) {
            panel.position += " (ocupa até " + std::to_string(position + configuration->conveyor->piece_length_um) + " µm)";
        }
    }
    if (control) { panel.control_motor = text(control->motor); }
    return panel;
}
SignalPanel make_signal_panel(const core::Snapshot* snapshot, const core::RunSpec* configuration) {
    SignalPanel panel;
    if (!configuration) { return panel; }
    const auto& program = configuration->program;
    const auto* plant = snapshot && snapshot->conveyor ? &*snapshot->conveyor : nullptr;
    for (std::uint32_t i = 0; i < program.input_count; ++i) {
        panel.inputs.push_back({"I" + number(i), plant && i < plant->plc_inputs.size() ? text(plant->plc_inputs[i].state) : neutral()});
    }
    for (std::uint32_t i = 0; i < program.output_count; ++i) {
        panel.outputs.push_back({"O" + number(i), plant && i < plant->plc_outputs.size() ? text(plant->plc_outputs[i].state) : neutral()});
    }
    if (configuration->conveyor) {
        const auto add = [&panel, plant](std::string name, auto read) {
            panel.plant.push_back({std::move(name), plant ? read(*plant) : neutral()});
        };
        add("Comando do operador", [](const core::ConveyorSnapshot& p) { return text(p.command.state); });
        add("Alimentação do motor", [](const core::ConveyorSnapshot& p) { return text(p.motor_power.state); });
        add("Comando do motor", [](const core::ConveyorSnapshot& p) { return text(p.motor_command.state); });
        add("Motor", [](const core::ConveyorSnapshot& p) { return text(p.motor_state); });
        add("Velocidade", [](const core::ConveyorSnapshot& p) { return std::to_string(p.velocity_um_per_s) + " µm/s"; });
        add("Alimentação do sensor", [](const core::ConveyorSnapshot& p) { return text(p.sensor_power.state); });
        add("Sensor amostrado", [](const core::ConveyorSnapshot& p) { return text(p.sampled_sensor); });
        add("Sensor publicado", [](const core::ConveyorSnapshot& p) { return text(p.sensor_state); });
    }
    for (const auto& rung : program.rungs) {
        const auto coil = number(rung.coil);
        if (rung.ton_us) {
            const auto* timer = snapshot && rung.coil < snapshot->timers.size() ? &snapshot->timers[rung.coil] : nullptr;
            panel.blocks.push_back({"TON O" + coil, (timer ? "ET=" + number(timer->elapsed_us) + " µs / " : "ET=" + neutral() + " / ") +
                number(*rung.ton_us) + " µs Q=" + (timer ? text(timer->q) : neutral())});
        }
        if (rung.ctu) {
            const auto* counter = snapshot && rung.coil < snapshot->counters.size() ? &snapshot->counters[rung.coil] : nullptr;
            panel.blocks.push_back({"CTU O" + coil, (counter ? "CV=" + std::to_string(counter->cv) : "CV=" + neutral()) +
                " PV=" + std::to_string(rung.ctu->pv) + " Q=" + (counter ? text(counter->q) : neutral())});
        }
    }
    // Pure evaluation over the stored counter image: what the next scan reads, not a new state.
    for (const auto& rung : program.rungs) {
        for (std::size_t b = 0; b < rung.branches.size(); ++b) {
            for (const auto& contact : rung.branches[b]) {
                if (!contact.comparison) { continue; }
                const auto& comparison = *contact.comparison;
                const auto coil = comparison.counter.coil;
                std::string value = "CTU O" + number(coil) + ".CV " + std::string(core::comparison_name(comparison.op)) + " " + std::to_string(comparison.constant);
                if (snapshot && coil < snapshot->counters.size()) {
                    value += " → " + text(core::evaluate_comparison(snapshot->counters[coil], comparison)) + " (próxima varredura)";
                } else {
                    value += " → " + neutral();
                }
                panel.blocks.push_back({"Comparador O" + number(rung.coil) + " ramo " + number(b), std::move(value)});
            }
        }
    }
    return panel;
}
EventRow event_row(const core::Transition& event) {
    std::string out = "tick " + number(event.tick) + " " + number(event.time_us) + " µs [" + std::string(phase_label(event.phase)) + "] " +
                      std::string(transition_label(event.kind));
    if (event.kind == core::TransitionKind::plc_input || event.kind == core::TransitionKind::plc_output) { out += " " + number(event.signal); }
    if (event.kind == core::TransitionKind::piece) { out += " posição=" + std::to_string(event.position_um) + " µm"; }
    else if (event.kind != core::TransitionKind::paused && event.kind != core::TransitionKind::resumed && event.kind != core::TransitionKind::stopped) {
        out += " " + text(event.state);
    }
    return {event.tick, std::move(out), false};
}
EventRow event_row(const core::CounterEvent& event) {
    return {event.tick, "tick " + number(event.tick) + " " + number(event.time_us) + " µs [contador] CTU O" + number(event.coil) +
                            " CV=" + std::to_string(event.state.cv) + " Q=" + text(event.state.q) + (event.stopped ? " (parada)" : ""), false};
}
EventRow event_row(const runtime::ObservationEvent& event) {
    return std::visit([](const auto& value) { return event_row(value); }, event.value);
}
EventRow lost_row(std::uint64_t count, core::Tick next_tick) {
    return {next_tick, "⚠ " + number(count) + " evento(s) perdido(s): saíram da janela de observação antes de serem exibidos. O histórico da simulação não foi afetado.", true};
}
void order_events(std::vector<runtime::ObservationEvent>& events) {
    std::sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
        return std::tie(a.publication_sequence, a.time_us, a.tick, a.phase, a.phase_order, a.sequence) <
               std::tie(b.publication_sequence, b.time_us, b.tick, b.phase, b.phase_order, b.sequence);
    });
}
}
