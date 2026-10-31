# Sprint corretiva InteractiveSession A1–A4 — 2026-10-30

## Resultado

A1–A4 corrigidos com 198/198 testes aprovados em GCC Debug e Release. Os 185 testes anteriores continuam na suite; tres testes da API tiveram expectativas ajustadas aos contratos corrigidos, sem remocao de cobertura. Foram adicionadas 13 regressoes. MSVC continua pendente; nao se declara a API aprovada para Qt.

## Correcoes

- A1: stop admitido em slot reservado, fora da fila comum e dos orcamentos ordinarios. Confirmacao de aceitação/aplicacao preservada. Na proxima fronteira, cancela fila/lote/inputs pendentes e aplica o stop existente do nucleo. Reserva logica de ate 2*output_count+2 eventos conserva o reset mesmo com historico ordinario cheio. Stop aceito antes de close e aplicado; close admitido antes rejeita stop com closed. Operacao ja selecionada pode terminar antes da parada.
- A2: advance(ticks) executa no maximo um tick entre verificacoes de comandos. pause suspende o lote; resume continua; stop/close cancelam o restante. O recibo informa advanced_ticks. Limites separados para fila, controles, inputs e solicitacoes de avanco. Testado horizonte de 100000 ticks com apenas uma admissao de controle (start) e uma de avanco.
- A3: SessionReplay separado dos recibos de diagnostico. Contem configuracao inicial, limites e acoes efetivamente aplicadas na ordem real, incluindo somente ticks concluidos de um lote parcial. Inputs cancelados nao entram. replay_session valida e reproduz sem identidade de sessao; a reproducao em nova sessao com ID 999 tambem foi testada. Close preserva o ultimo resultado sem inventar stop ou aplicar entradas pendentes.
- A4: InteractiveSession possui std::jthread dedicada e controlada; usa o ConveyorSession existente como unico executor funcional de seu estado. Runner nao recebe mais tarefas longas de sessao e preserva suas tarefas batch. Sobrecarga antiga com Runner permanece compativel, sem ocupa-lo ou rete-lo. request_close retorna futuro consultavel sem bloquear; close/destrutor fazem o teardown com join fora do caminho da futura UI.

O contrato detalhado e atualizado esta em [interactive-session.md](interactive-session.md).

## Arquivos

Modificados:
- include/sim/runtime/interactive_session.hpp
- src/runtime/interactive_session.cpp
- include/sim/runtime/runner.hpp
- src/runtime/runner.cpp
- tests/interactive_session_test.cpp
- CMakeLists.txt
- docs/interactive-session.md

Criados:
- tests/interactive_fixes_test.cpp
- docs/interactive-a1-a4.md (este relatorio)

Nenhum arquivo de schemas/JSON, nucleo Ladder, TON, CTU, comparadores, mecanica, cache ou compare_runs foi alterado nesta sprint.

## Regressoes novas

1. Stop com orcamentos de controle/input esgotados.
2. Stop com fila ordinaria saturada.
3. Eventos de reset preservados alem do orcamento ordinario.
4. Avanco ate 100000 ticks e completed.
5. Inputs intercalados em fronteiras exatas de um lote.
6. Pause de lote ativo e interrupcao por stop.
7. Replay apos close com input cancelado.
8. Replay em nova identidade de sessao, pela ordem aplicada.
9. Close durante avanco: replay somente de ticks concluidos.
10. Duas sessoes pausadas, lotes Runner e fechamento independente simultaneo.
11. Stop aceito antes de close, com step/pause enfileirados.
12. Trinta corridas entre step, stop e close, verificando todos os recibos e replay.
13. Avanco invalido e envelope de replay malformado.

Os testes antigos de fila cheia, orcamento de comandos e segunda sessao foram ajustados, respectivamente, para admissao atomica submit_batch, independencia dos passos em relacao ao orcamento de controle e execucao dedicada. Os outros contratos e testes permanecem.

## Compilacao e testes efetivamente executados

GCC 16.2.0, Windows x64, C++20, CMake/Ninja, ferramentas e dependencias locais existentes. Comandos na raiz do projeto:

```powershell
cmake --build build/conveyor-debug -j 4
ctest --test-dir build/conveyor-debug --output-on-failure --output-junit a1-a4-results.xml
cmake --build build/conveyor-release -j 4
ctest --test-dir build/conveyor-release --output-on-failure --output-junit a1-a4-results.xml
```

| Configuracao | Compilacao | CTest | Tempo da suite |
| --- | --- | --- | --- |
| GCC Debug | codigo 0 | 198/198; codigo 0 | 11,14 s |
| GCC Release | codigo 0 | 198/198; codigo 0 | 7,49 s |
| MSVC Debug/Release | Nao executada | Pendente | — |

Evidencias: build/a1-a4-debug-build.txt, build/a1-a4-debug-tests.txt, build/a1-a4-release-build.txt, build/a1-a4-release-tests.txt e a1-a4-results.xml em cada diretorio de configuracao. Todos os executaveis iniciaram nas suites finais; nenhuma politica de seguranca foi alterada. A consulta cl no PATH nao encontrou compilador; vswhere nao existe no local padrao verificado. Uma instalacao personalizada nao detectada nao pode ser descartada.

## Limitacoes e orientacoes para Claude

- Revisar a linearizacao da admissao e selecao de stop/close: prioridade definida na fronteira, sem preempcao dentro de tick.
- Revisar a margem de eventos de parada e os limites independentes. Falha global de alocacao/SO nao e coberta pela garantia de sobrecarga logica.
- Validar replay de lotes parcialmente cancelados e inputs diferidos: commands e diagnostico; somente replay.applied deve ser reproduzido.
- Um unico avanco ativo por sessao; avanco concorrente processado enquanto outro esta ativo e rejeitado explicitamente. O tamanho do lote e limitado pelo horizonte, nao por tempo real.
- Executor dedicado significa uma thread por sessao; a aplicacao deve limitar o numero de sessoes. Nao foi criado pool global.
- history() copia configuracao/journal/resultados; publication() permanece a consulta usual. Janelas grandes e muitos inputs futuros podem exigir otimizacao posterior, sem alterar os contratos.
- Validar MSVC real e, quando disponivel, ferramentas de deteccao de races. Os testes concorrentes nao constituem prova formal de ausencia de corridas.
- API limitada ao perfil ConveyorSession. Nao ha novo formato JSON, checkpoint, UI ou aprovacao para Qt Widgets.

Nenhum comando Git, ZIP, instalador, dependencia nova, alteracao de seguranca ou funcionalidade industrial foi realizado/adicionado.
