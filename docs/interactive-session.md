# InteractiveSession — contrato atualizado A1–A4

Este documento substitui o contrato da primeira sprint interativa. A API continua headless; RunSpec, JSON, Ladder, TON, CTU, comparadores e o modelo mecanico nao foram alterados.

## Executor e ciclo de vida (A4)

Cada InteractiveSession possui uma std::jthread dedicada e um unico ConveyorSession, que continua executando o escalonador funcional existente. Nao ha outro escalonador para o mesmo estado. Runner volta a executar somente suas tarefas batch existentes, sem tarefa longa de sessao na fila.

Construtor recomendado: InteractiveSession(session_id, configuration, limits). O construtor antigo com shared_ptr<Runner> permanece por compatibilidade e valida o ponteiro, mas nao ocupa nem retem esse Runner. Duas sessoes podem executar independentemente; uma sessao pausada ou completed nao impede lotes no Runner. Cada dono deve limitar quantas sessoes cria, pois cada instancia possui uma thread.

request_close() solicita cancelamento sem esperar a thread e devolve shared_future<void>. A futura UI pode consultar wait_for(0) e continuar seu loop; nao precisa chamar future.get(). close() e o destrutor aguardam o encerramento e sao destinados ao teardown controlado fora do caminho interativo da UI. Chamadas concorrentes de close sao serializadas. Destruicao do objeto exige que chamadores externos deixem de usa-lo.

## Parada reservada (A1)

stop possui uma posicao reservada independente da fila ordinaria, dos tres orcamentos de admissao e da disponibilidade de historico ordinario. Continua exigindo identidade correta e ID novo; ID errado nao e uma parada valida. A primeira parada e aceita e confirmada por ticket; repeticoes sao explicitamente rejeitadas como invalid (ou stale para ID reutilizado), pois a parada ja esta garantida.

A selecao de trabalho e serializada pelo mutex de admissao. A ordem e: stop reservado, close, fila ordinaria, proximo tick do avanco ativo. Uma operacao ja selecionada pode terminar; nao ha preempcao dentro de um scan. Na fronteira seguinte, stop cancela o restante do lote, entradas pendentes e fila ordinaria, aplica ConveyorSession::stop e publica a confirmacao. Uma parada aceita antes de request_close sempre e aplicada; se close vencer a admissao, stop recebe closed. pause/step ja aplicados permanecem no replay; os ainda pendentes sao cancelados.

A semantica de reset vem do metodo existente do nucleo. Ha uma margem logica adicional de ate 2*output_count+2 registros para os eventos de parada (bobinas, contadores, motor, stopped), alem de limits.events. Ela nao e consumida por comandos ordinarios. Se houver falha de orcamento, o estado funcional e conservado ate stop ou close, permitindo o reset real. completed preserva a semantica existente de stop sem reset adicional. A garantia nao cobre esgotamento global de memoria ou falhas do sistema operacional.

## Avanco e limites separados (A2)

step solicita um tick. advance usa SessionCommand.ticks entre 1 e RunSpec.ticks; o lote nao pode ultrapassar o horizonte restante. Somente um avanco fica ativo por sessao; outro step/advance processado durante esse avanco recebe invalid, sem ficar esperando indefinidamente. Seu recibo informa advanced_ticks, inclusive se a parte restante for cancelada ou encontrar limite de historico.

O trabalhador verifica stop, close e comandos entre cada tick. pause suspende um lote ativo; resume o continua. input pode indicar uma fronteira atual ou futura dentro do horizonte; uma fronteira ja passada recebe stale. Entradas futuras ficam pendentes e so recebem applied no tick correspondente. Eventos originais da configuracao entram primeiro; entradas interativas do tick entram depois, em ordem de admissao, sempre antes da amostragem PLC. Nao ha relogio real como fonte de tempo simulado.

submit_batch admite ate 1024 solicitacoes sob uma unica secao de admissao. Cada item recebe seu proprio resultado; o lote nao e transacional. Isso permite definir entradas futuras e controles antes do primeiro passo, inclusive testar fila cheia deterministicamente.

Limites independentes (padrao / maximo):

- queue_capacity: 32 / 1024 solicitacoes ordinarias em fila; stop tem posicao separada.
- commands: 10000 / 100000 admissoes de start/pause/resume, incluindo tentativas aceitas e depois rejeitadas pelo estado.
- functional_commands: 10000 / 100000 admissoes de input.
- advance_requests: 100000 / 100000 admissoes de step/advance; nao consomem commands.
- Horizonte funcional: RunSpec.ticks, ate 100000; um unico advance pode percorre-lo.
- snapshots: 256 / 100000. Em bounded descarta somente imagens antigas e contabiliza; em full exige espaco para cada tick.
- events: 100000 / 1000000 registros combinados, mais a margem exclusiva de parada.

Uma configuracao com controle de partida, um advance e historico bounded suficiente chega ao horizonte de 100000 ticks sem esgotar controles. Limites voluntariamente pequenos de historico/eventos ainda podem interromper a execucao com history_limit. O preflight de eventos reserva, a cada passo, somente os eventos configurados e as entradas daquele tick, mais o maximo que o passo pode emitir: entradas PLC conectadas, bobinas com rung, CTUs, motor, peca e sensor. Continua sendo um limite superior: um evento sem mudanca de valor nao gera registro, mas conta na reserva. Se o limite real nao comporta o passo, o avanco recebe history_limit, a sessao passa a failed e os registros retidos permanecem como prefixo exato da execucao; nenhum evento funcional e descartado para caber.

A quantidade de recibos armazenados e limitada pela soma das admissoes de controle, inputs, avancos e uma parada. O journal de replay e limitado a ticks executados + inputs aplicados + controles aplicados + uma parada. Os limites sao logicos, nao um teto RSS; capacidade de vetores, alocador, threads e objetos retidos pelos consumidores sao custos adicionais.

## Replay e diagnosticos (A3)

SessionHistory.commands continua sendo o diagnostico de todas as solicitacoes aceitas, com resultado applied/rejected/cancelled, IDs e advanced_ticks. Rejeicoes imediatas continuam no ticket do chamador. Esse vetor NAO e a entrada de replay: nao se deve ordenar e reaplicar todos os seus IDs.

SessionHistory.replay e um shared_ptr<const SessionReplay> separado, contendo copia da configuracao inicial, limites e journal na ordem efetiva de aplicacao. Esse journal nao possui identidade de sessao nem IDs de comandos. Registra start, pause, resume, stop, inputs efetivamente aplicados e um step por tick realmente executado. Um advance parcialmente cancelado nao reaplica seu restante. Inputs cancelados, controles rejeitados e solicitacoes apenas admitidas nao entram no journal.

replay_session(*history.replay) reproduz o RunResult de forma sincrona e sem thread/identidade de sessao. E uma ferramenta offline para testes ou workers, nao para bloquear a UI. Tambem e possivel aplicar as acoes em uma nova InteractiveSession com outro ID e novos IDs de comando. A funcao valida envelope, limites, sequencia de fronteiras e transicoes; rejeita entradas sem passo aplicado correspondente. O envelope e transitorio e nao adiciona um formato JSON persistente.

close nao inventa stop nem aplica entradas pendentes. Preserva o ultimo RunResult/control e o journal aplicado; a publicacao operacional passa a closed. Uma sessao encerrada em pausa reproduz a pausa e os ticks concluidos, sem transformar inputs cancelados em alteracoes funcionais.

## Snapshots e seguranca entre threads

Snapshot nao foi ampliado. Publication e snapshots continuam imutaveis e compartilhados com const ownership. O estado mutavel pertence exclusivamente a thread da sessao; consumidores nunca acessam ConveyorSession. Tres mutexes separam as responsabilidades: admissao (fila, stop, close, IDs); registros retidos (mantido por um passo e pela coleta de ponteiros de uma consulta, nunca por uma copia profunda); publicacao (somente a troca de um ponteiro). A ordem de aquisicao e registros, depois admissao ou publicacao.

## Consultas de historico

| Consulta | Custo para a sessao | Custo para quem chama | Uso |
|---|---|---|---|
| publication() | Troca de ponteiro; nao espera passo nem historico | Um shared_ptr | Renderizacao frequente |
| snapshots(first_tick, max_count) | Ate 4096 ponteiros | Compartilha os snapshots | Graficos e paginas de trace |
| history_view() | Copia de ponteiros e do bloco aberto de cada log | Compartilha snapshots e blocos | Visao coerente e completa |
| history() | Igual a history_view() | Copia profunda fora dos locks da sessao | Compatibilidade e testes; usar em worker |

publication() entrega a ultima publicacao coerente. latest e a ultima imagem completa de fim de scan, o mesmo objeto guardado no trace; controles nao fabricam imagem nova. snapshots() comeca em first_tick ou, se ele ja foi descartado, no primeiro tick retido; a pagina informa a faixa retida e os descartes, e max_count fora de [1, 4096] e rejeitado. materialize(view) converte uma visao em SessionHistory e nela se obtem result, commands e replay.

Trace e logs preservam a ordem. O modo bounded descarta somente snapshots antigos e contabiliza; eventos, recibos e journal nao sao descartados. Visoes e paginas continuam validas apos close e apos a destruicao da sessao, pois sao donas do que referenciam. A sessao nao guarda nem acumula visoes; o que uma visao retida fixa, inclusive snapshots ja descartados da janela, e liberado quando o consumidor a solta. O custo de retencao e do consumidor: ele deve manter poucas visoes vivas e nao reter cada publicacao.

## Limitacoes

A reserva conta eventos sem mudanca de valor. O custo de um passo cresce com a quantidade de entradas futuras pendentes, pois a lista e percorrida a cada tick. Exaustao de memoria durante o encerramento da thread da sessao nao e tratada. Nao houve ThreadSanitizer, injecao de falhas de alocacao nem compilacao MSVC.
