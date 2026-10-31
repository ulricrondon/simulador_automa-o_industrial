# Sprint Qt 1.1 — diagnosticos estruturados (headless)

## API

CommandCode preserva todos os valores e significados anteriores. CommandReply ganhou diagnostic (shared_ptr<const SessionDiagnostic>), vazio para accepted/applied. A rejeicao imediata compartilha o mesmo diagnostico entre acceptance e completion; falhas na aplicacao ficam em completion e no historico de recibos.

SessionDiagnostic contem:

- code: CommandCode existente, sem substitui-lo.
- reason: DiagnosticReason, subcodigo tipado para apresentacao e tratamento.
- message: texto objetivo, limitado a 512 bytes nos detalhes de excecao; sem stack, RTTI ou objeto de excecao.
- context: session_id, command_id/comando quando disponiveis, etapa (admission/application/execution), tick/estado observados e valores opcionais used/limit/requested.

Na admissao, tick e estado ficam ausentes para nao apresentar uma fronteira de aplicacao ficticia. O tick antigo de CommandReply continua com seu contrato anterior. Em falhas durante um lote, diagnostic.context.tick aponta a fronteira em que ocorreu a falha; reply.tick continua representando a fronteira inicial do pedido.

InteractiveSession::diagnostic() consulta o ultimo diagnostico publicado; retorna nullptr antes da primeira rejeicao/falha. Um sucesso posterior nao apaga automaticamente a ultima mensagem. Ela pode ser substituida por outra rejeicao; nao e um historico ilimitado de erros. Os tickets/historico conservam os diagnosticos individuais.

SessionPublication::diagnostic conserva a falha causadora de failed/execution_error e continua acessivel apos close. Cancelamentos consequentes nao ocultam essa causa. Rejeicoes comuns nao alteram SessionPublication.error ou o estado operacional. O ultimo diagnostico da sessao pode ser de uma rejeicao mais recente; a causa de encerramento continua na publicacao.

## Subcodigos e limites

| Familia | reason | Contexto |
| --- | --- | --- |
| Fila | queue_capacity | quantidade em fila, capacidade, pedido adicional |
| Admissao | control_budget / input_budget / advance_budget | quantidade admitida, limite correspondente, adicional |
| Historico | snapshot_budget | snapshots retidos, limite integral, proximo snapshot |
| Eventos | event_budget | eventos retidos, limite, reserva necessaria para a operacao |
| Tempo simulado | horizon | horizonte configurado, ticks ja executados quando conhecidos, quantidade/fronteira solicitada |
| Identidade | wrong_session / stale_id | sessao esperada e ID solicitado/ultimo aceito quando aplicavel |
| Comando | invalid_command / invalid_value / invalid_target | ID e tipo de comando |
| Estado | invalid_state / advance_active / stop_pending / session_closed | estado e tick somente se observados na thread funcional |
| Fronteira | stale_boundary | tick atual e fronteira solicitada |
| Cancelamento | cancelled | comando e fronteira, sem inventar aplicacao |
| Excecoes | known_exception / unknown_exception / allocation_failure | contexto disponivel na execucao |

A reserva de eventos e conservadora; requested pode exceder os registros que seriam realmente emitidos. O diagnostico nao modifica esse calculo. O horizonte permanece limitado a 100000 ticks, ou ao horizonte menor do projeto carregado. Nenhum limite foi ampliado.

## Excecoes e contratos preservados

O catch generico agora usa um tradutor interno. Mensagens conhecidas de validacao/estado (logic_error, exceto future_error/length_error) e overflow_error sao preservadas, com comprimento limitado e caracteres de controle substituidos por espacos. Mensagens de future_error, length_error, runtime_error desconhecido e excecoes nao padrao nao sao expostas: recebem texto generico. bad_alloc e identificado sem expor what().

O objeto exception_ptr e os tipos internos ficam no runtime; nao fazem parte de SessionDiagnostic. A classificacao nao altera a politica funcional: uma excecao durante a execucao ainda causa execution_error e encerramento; uma rejeicao normal continua permitindo os controles validos seguintes. Os testes do tradutor cobrem a conversao de excecoes conhecidas/recuperaveis e a ocultacao de detalhes desconhecidos, sem introduzir hooks de falha na API de producao.

Construcao invalida, replay invalido, tamanho de lote >1024 e pagina de snapshots fora do intervalo mantem os contratos de excecao ja existentes; nao foram convertidos silenciosamente em tickets. O adaptador desktop deve tratar esses erros esperados nas suas fronteiras. Exaustao global de memoria durante a criacao de um diagnostico/encerramento permanece fora da garantia; nao se declara a API noexcept.

## Concorrencia e custo

Os diagnosticos sao imutaveis e podem sobreviver a sessao. diagnostic() usa armazenamento com mutex separado e apenas copia um shared_ptr. Nao adquire o mutex dos registros, nao espera um tick terminar e nao materializa historico. Formatacao de mensagens e construcao ocorrem antes da troca de ponteiro; leitura/apresentacao do texto ocorre depois de liberar o mutex.

Isso evita que a apresentacao da mensagem bloqueie a execucao por uma copia ou renderizacao longa. Nao e uma promessa de operacao lock-free ou de tempo real: ha uma secao critica curta de troca/copia de ponteiro. O ultimo diagnostico ocupa uma referencia; recibos persistidos continuam limitados pelos orcamentos existentes. Consumidores sao responsaveis pelas referencias antigas que conservam.

Snapshot, logs segmentados da revisao A5, configuracao JSON, journal aplicado e compare_runs permanecem sem alteracao semantica. Uma pequena ordenacao de publicacao foi ajustada: em falha de orcamento, o estado failed e seu diagnostico sao publicados antes de confirmar history_limit ao solicitante.

## Orientacao para a futura interface Qt

- Consultar publication() para estado operacional e diagnostic() para a ultima mensagem; nao copiar history() a cada frame.
- Tratar acceptance imediatamente. Para completion, consultar wait_for(0) e obter o recibo somente quando pronto; nunca bloquear o event loop esperando get().
- Usar code/reason para selecionar traducao e comportamento; nao analisar strings para decidir a logica. message e uma descricao padrao em ingles, nao um protocolo textual.
- Mostrar mensagem como texto simples (por exemplo Qt::PlainText), com comando, tick e limite somente quando presentes. Nao interpretar HTML nas mensagens conhecidas.
- Diferenciar uma rejeicao recuperavel (fila cheia, estado invalido) de uma sessao failed/closed. Uma rejeicao nao implica que a simulacao foi interrompida.
- Distinguir budget de snapshots/eventos de capacidade da fila e do horizonte. Nao sugerir ampliar o horizonte alem de 100000.
- Manter a causa fatal visivel mesmo quando o estado final for closed. Cancelamentos de outros tickets podem ser mostrados de forma secundaria.
- Liberar referencias antigas e evitar dialogs repetidos para o mesmo ponteiro de diagnostico; guardar os tickets necessarios quando todas as rejeicoes precisarem ser apresentadas.

Nao ha codigo Qt/VTK nesta entrega nem declaracao de aprovacao da API para Qt.

## Arquivos e testes

Modificados: include/sim/runtime/interactive_session.hpp; src/runtime/interactive_session.cpp; CMakeLists.txt.
Criados: src/runtime/session_diagnostics.hpp; src/runtime/session_diagnostics.cpp; tests/session_diagnostics_test.cpp; docs/session-diagnostics.md.

Onze testes novos cobrem fila, tres orcamentos de admissao, snapshots/eventos, horizonte, comando/identidade/valor/target invalidos, estado/fronteira, encerramento/cancelamento, excecoes conhecidas, detalhes desconhecidos, limites de mensagem e consultas concorrentes com replay/snapshots preservados. Os 209 testes anteriores nao foram editados; total esperado: 220.

Comandos executados com GCC 16.2.0 / Windows x64 e ferramentas locais existentes:

```powershell
cmake --build build/conveyor-debug -j 4
ctest --test-dir build/conveyor-debug --output-on-failure --output-junit diagnostics-results.xml
cmake --build build/conveyor-release -j 4
ctest --test-dir build/conveyor-release --output-on-failure --output-junit diagnostics-results.xml
```

MSVC nao foi encontrado por cl no PATH nem por vswhere no local padrao. Seus presets nao foram executados; nenhuma compatibilidade MSVC e declarada. Nao foram instaladas ferramentas, executados comandos Git, criados pacotes ou alteradas politicas de seguranca.

## Resultado final — 2026-10-31

| Configuracao | Compilacao | Testes | Tempo |
| --- | --- | --- | --- |
| GCC Debug | codigo 0 | 220/220; codigo 0 | 9,56 s |
| GCC Release | codigo 0 | 220/220; codigo 0 | 6,37 s |
| MSVC Debug/Release | Nao executada | Pendente | — |

Evidencias: build/diagnostics-debug-build.txt, build/diagnostics-debug-tests.txt, build/diagnostics-release-build.txt, build/diagnostics-release-tests.txt e diagnostics-results.xml em cada diretorio de build. Nenhum teste foi ignorado nas suites finais.

Limites restantes: consulta usa mutex curto, sem promessa lock-free; falha global de alocacao pode impedir a criacao do proprio diagnostico; nao houve ThreadSanitizer nem injecao de falhas na thread funcional; mensagens padrao ainda nao possuem catalogo de traducao. A integracao e aprovacao Qt permanecem para uma etapa posterior.
