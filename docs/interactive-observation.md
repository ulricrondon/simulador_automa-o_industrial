# Qt 1.3A - Observacao coerente do runtime

## API para o consumidor

`InteractiveSession::observation()` retorna `shared_ptr<const SessionObservation>`.
Uma unica aquisicao entrega `publication` (identidade, estado, revisao, next_tick,
ultimo snapshot e diagnostico fatal), `control` (estado apos controles) e `events`.
Tudo corresponde a mesma fronteira funcional. Nao combinar estes campos com uma
segunda consulta a publication(), history_view() ou diagnostic(). As APIs antigas
continuam disponiveis, com seus contratos anteriores.

A consulta adquire apenas o mutex de publicacao para copiar um shared_ptr. Nao toma
o mutex funcional, nao copia historico, nao aloca e nao espera um tick terminar.
Nao e uma garantia lock-free ou de latencia de tempo real: pode haver contencao
curta na troca de ponteiro e escalonamento pelo sistema operacional. Construcao e
liberacao da publicacao substituida ocorrem fora desse mutex.

## Fronteiras sem novo tick

A revisao `publication.sequence` avanca em cada publicacao, inclusive start,
pause, resume, stop, falha e close. `next_tick` continua sendo o proximo passo.
`latest` continua sendo a ultima imagem COMPLETA de fim de passo; controles nao
fabricam uma nova imagem. Antes do primeiro passo, latest e nulo. Apos stop, o
motor em control pode estar LOW enquanto a imagem anterior ainda mostra HIGH.
Close preserva a imagem, o ultimo control conhecido e a causa fatal; nao implica
um reset funcional. Rejeicoes de admissao nao criam uma fronteira nem substituem
a causa fatal: continuam sendo obtidas nos recibos/diagnostic() separadamente.

## Eventos e cursor

`observation_events(*observation, {session_id, next_sequence}, max_count)` e uma
funcao pura sobre a observacao retida. Retorna eventos, cursor seguinte e `lost`.
O cursor inicial e `{id, 0}`. max_count aceita 1..8192. Identidade diferente ou
cursor alem do fim desta observacao causam invalid_argument. Ao trocar de sessao,
reinicializar o cursor; o dono deve atribuir IDs distintos as sessoes que distingue.
Nao aplicar um cursor mais recente a uma observacao antiga.

A chave completa de ordenacao e:
`(publication_sequence, time_us, tick, phase, phase_order, sequence)`.
A fronteira vem primeiro: pause/resume e um passo subsequente podem ter o mesmo
tempo simulado, mas devem preservar a ordem de aplicacao. Dentro da fronteira,
eventos sao intercalados pelo tempo e fase. Atualizacoes CTU precedem publicacao
das bobinas na fase Ladder. No stop, reset de bobinas precede reset CTU, seguido
de motor/stopped. Empates preservam a ordem original de cada fluxo; sequence e
o desempate final unico e contiguo. Nenhum timestamp usa relogio real.

A janela retida tem no maximo 32 blocos imutaveis de ate 256 registros (8192
registros no maximo, podendo reter menos devido a blocos parciais). O intervalo
[first_sequence, next_sequence) e explicito; first_sequence tambem contabiliza
o descarte acumulado. `lost` informa exatamente o trecho solicitado ja removido.
O consumidor deve sinalizar a lacuna; nunca tratar uma janela incompleta como
historico integral. Pode recuperar o historico duravel em um worker pelas APIs
existentes. A janela NAO remove eventos do historico funcional ou do replay.

## Custo e propriedade

Cada evento novo tem uma copia na janela limitada. Uma coleta de m eventos novos
ordena apenas esse lote (O(m log m)); nao ordena o historico. Os blocos antigos
sao compartilhados. Copiam-se ate 33 ponteiros para atualizar o indice da janela;
passos sem novos eventos reutilizam a mesma janela. Ao preencher a cauda ou
aparar a frente, copiam-se somente as extremidades parciais (ate 255 registros
antigos por extremidade), nunca os blocos completos compartilhados. A publicacao acrescenta um
objeto pequeno por fronteira, sem ampliar Snapshot. O pico temporario tambem
inclui o lote corrente, sujeito aos limites existentes de eventos/configuracao.

Paging copia ate max_count registros e examina no maximo 8192 registros, sempre
fora dos locks da sessao. Liberar observacoes apos uso. Consumidores que guardam
muitas observacoes podem prolongar a vida de blocos e snapshots descartados; esse
armazenamento externo nao faz parte do teto da janela do runtime.

## Integracao posterior pelo Claude

1. Em poll(), adquirir UMA observation e usar seus campos para todos os paineis.
2. Comparar publication.sequence para mudancas de estado, mesmo sem novo snapshot.
3. Identificar a imagem por latest/tick; nao confundir control com imagem do scan.
4. Paginar os eventos dessa mesma observacao, atualizando o cursor e mostrando lost.
5. Manter recibos/rejeicoes separados do diagnostico fatal em publication.diagnostic.
6. Reinicializar cursor e paineis ao trocar a identidade da sessao.

SessionController, view_model e Qt nao foram alterados nesta entrega. Portanto a
janela existente ainda precisa migrar para esta API em uma tarefa posterior.

## Validacao

Oito testes novos cobrem leitores concorrentes, estado sem novo tick, ordem
CTU/bobinas, consumidores lentos, janela limitada, lote maior que a janela,
retencao apos destruicao, troca de identidade, cursores invalidos e causa fatal.
O teste de consulta mede 100000 aquisicoes ociosas e imprime ns/consulta; e uma
medida descritiva local, sem limiar de aprovacao nem garantia de tempo real.

Resultado local: GCC 16.2.0, Debug 247/247 e Release 247/247 via CTest.
Comandos: cmake --build build/conveyor-debug -j 4;
ctest --test-dir build/conveyor-debug --output-on-failure -j 4;
mesmos comandos para build/conveyor-release. O diretorio bin do w64devkit ja
instalado foi acrescentado somente ao PATH dos processos de compilacao.
Media observada no teste ocioso: Debug 36 ns/consulta; Release 18 ns/consulta.
Nao foram medidos percentis sob contencao nem o custo incremental por tick.
MSVC e Qt nao foram compilados ou executados nesta entrega.

## Correcao Qt 1.3C

A implementacao anterior alocava um bloco por publicacao, mesmo com poucos eventos.
Agora o ultimo bloco parcial e preenchido em uma nova copia imutavel. O prefixo
excedente e removido por quantidade exata de registros, com copia limitada da
primeira extremidade quando necessario. Blocos completos sao compartilhados.
Lotes maiores que a janela pulam diretamente os registros novos ja fora dela.
A atualizacao nao copia o historico nem tem custo quadratico no total acumulado:
ha no maximo 33 ponteiros e duas copias limitadas a um bloco, alem do lote novo.
Visoes antigas fixam as versoes anteriores; esse custo externo depende da politica
de retencao do consumidor e nao e um teto global de memoria do processo.

Medicao Release, mesmo cenario de 10000 ticks, tres execucoes sequenciais por versao:
- Total emitido: 20015 eventos nas duas versoes.
- Antes: 64 retidos, 32 blocos, payload dos vetores 6144 bytes;
  1602, 1461, 1462 ns/tick (mediana 1462).
- Depois: 8192 retidos, 33 blocos, payload dos vetores 786432 bytes;
  2912, 2233, 2522 ns/tick (mediana 2522).
A medida inclui simulacao, coleta e publicacao, nao isola observe_events(). Bytes
sao soma de capacity()*sizeof(ObservationEvent), sem alocador, metadados, historico,
visoes antigas ou temporarios. Nao e RSS. A memoria maior corresponde a capacidade
agora efetivamente utilizada. Nao ha alegacao de otimo global entre algoritmos.

Quatro testes adicionais (incluindo medicao) e ajuste do teste de consumidor lento
para exceder a capacidade real. Verificados preenchimento parcial, compartilhamento,
8192 eventos sem perda, perda exata de um no evento 8193, consumidores concorrentes,
ordem temporal, coerencia e replay. Suites completas: GCC Debug e Release 265/265.
Qt, SessionController, view_model, JSON e semantica industrial nao foram alterados.
MSVC/Qt continuam sem compilacao real nesta sprint.
