# Cenários integrados e replay determinístico
Data: 30/10/2026.
Diretório: C:\Projetos\simulador_industrial\simulador_automa-o_industrial.

## Cenários de referência
### reference-conveyor.json
Cenário físico com uma peça, movimento positivo e um sensor geométrico. Reutiliza PLC, contatos NO/NC, AND/OR, TON, motor, CTU, comparadores e bobinas.

O acionamento tem duas alternativas: habilitação com sensor livre e CV<1, ou habilitação com selo da saída anterior e CV<1. O selo mantém o acionamento durante a leitura do sensor até que o contador atualizado seja visível na imagem anterior. TON=2000 µs antes de energizar. CTU conta a única borda válida do sensor; CV<1 desliga o motor no scan seguinte. Não há reinserção da peça nem passagens fictícias.

Referência independente usada nos testes:
| Tick | Posição final µm | ET µs | Motor | CV | Sensor publicado |
|---|---:|---:|---|---:|---|
| 0 | 0 | 0 | LOW | 0 | LOW |
| 1 | 0 | 0 | LOW | 0 | LOW |
| 2 | 0 | 1000 | LOW | 0 | LOW |
| 3 | 10 | 2000 | HIGH | 0 | LOW |
| 4 | 20 | 2000 | HIGH | 0 | HIGH |
| 5 | 30 | 2000 | HIGH | 1 | HIGH |
| 6 | 30 | 0 | LOW | 1 | LOW |
| 7–11 | 30 | 0 | LOW | 1 | LOW |

O sensor publica HIGH ao final do tick 4, em 5000 µs. O PLC amostra essa publicação no tick 5, também em 5000 µs, e CTU passa a CV=1. Comparadores ainda leem CV=0 nesse scan; CV=1 é lido no tick 6, em 6000 µs. TON reseta e o motor desliga. A amostra do tick 6 tem timestamp de fim do passo, 7000 µs, conforme o contrato existente.

### reference-pulses.json
Bancada de entradas controladas; não representa várias passagens de uma peça física. I1 fornece pulsos, I2 é reset e I0 habilita. A condição de acionamento combina CV==3 OR CV>3, com habilitação e reset inativo em ambas as alternativas. TON=1000 µs. CTU tem PV=99, demonstrando que a ação depende da comparação direta de CV e não de Q.

CV por tick 0–14: 0,1,1,2,2,3,3,3,4,4,0,0,0,1,1.
A saída O0 fica HIGH nos ticks 7, 8 e 9. A condição fica verdadeira no tick 6, mas TON começa com ET=0 e só ativa no tick 7. Reset no tick 10 limpa CTU, bloqueia a ação e reseta TON. O eixo linear termina em 30 µm.

## Replay e divergências
O novo compare_runs recebe um resultado esperado e um resultado obtido. Compara todos os snapshots armazenados, tamanhos/ordem das sequências, eventos funcionais, eventos de contadores e resumo de controle. Mantém no máximo um diagnóstico, com tick, entidade funcional, caminho da variável e valores esperado/obtido. Seleciona o menor tick entre as divergências dos fluxos; em empate, prioriza snapshots, eventos funcionais, eventos de contador e controle.

As entidades são identificadas por função e índice (por exemplo axis, motor, sensor, plc.output[0], ctu[1]). O caminho inclui o índice do registro, como trace[3].position_um. A inspeção imprime também o mapa de UUIDs dos rungs, eixo, motor e sensor.

O comando replay carrega/valida o mesmo JSON, envia duas execuções novas ao Runner e compara seus resultados completos. Cada execução reconstrói os estados transitórios iniciais. O resultado de referência permanece imutável. Não é checkpoint nem restauração de estados intermediários.

Duas execuções iguais comprovam repetibilidade, mas não bastam para demonstrar correção funcional: os testes de aceitação acrescentam tabelas numéricas independentes e verificações de fases, sensores e acionamentos. compare_runs compara resultados, não identidade de configuração; seu uso pressupõe uma referência apropriada para o cenário.

Há proteção contra omissão de novos campos em futuras projeções de diagnóstico: se a igualdade estrutural detectar diferença não localizada, é emitido unmapped_state, nunca sucesso falso.

## Rastreabilidade somente de leitura
snapshot_values expõe tick, instante, imagens PLC, posição, estados dos timers (ET/ativo/Q), contadores (CV/Q/armamento), motor, alimentação, velocidade, resto fracionário e sensor amostrado/publicado. Os slots de estado são indexados por bobina; slots sem bloco mantêm seus valores padrão.

comparison_values deriva os resultados usando explicitamente a imagem anterior de contadores. Não avança a simulação nem modifica CTU. inspect imprime STATE com instante de fim do passo e EVAL com instante do início da varredura. Mostra operandos, operador e resultado das comparações. A saída é limitada aos primeiros 64 snapshots; os dois cenários ficam integralmente dentro desse limite. replay imprime apenas um resumo.

O histórico permanece em RunResult transitório. Não foram criados formatos persistentes de gravação, vídeo ou dependências externas de comparação. Os schemas e o parser de projetos não mudaram: ambos os exemplos usam o formato 1.4 já existente.

## Contratos exercitados
- Ordem das fases e publicação conjunta das bobinas.
- Sensor calculado após mecânica e amostrado no scan seguinte.
- Leitura de CV da imagem anterior, independente da ordem dos rungs.
- TON: início com ET=0, ativação após prazo e reset.
- CTU: bordas válidas, HIGH contínuo, contagem acima do limiar e reset.
- LOW/HIGH/UNDEFINED/FAULT com a latência documentada.
- Limite físico da peça e parada por falha do motor.
- Pause/resume sem avanço enquanto pausado; stop terminal com limpeza dos estados.
- Replay em jobs consecutivos, após stop e após recarregar JSON.
- Execução com atrasos reais entre passos produzindo os mesmos resultados.

Apenas o teste de independência temporal usa sleep, por 1 ms entre passos. O código funcional não lê relógio nem recebe esse atraso como parâmetro. Não foi alterada a semântica do escalonador, motor, TON, CTU ou comparadores.

## Arquivos
Criados:
- include/sim/core/replay.hpp
- src/core/replay.cpp
- examples/reference-conveyor.json
- examples/reference-pulses.json
- tests/replay_test.cpp
- tests/reference_acceptance_test.cpp
- tests/replay_cli.cmake
- docs/reference-replay.md

Modificados:
- apps/project/main.cpp
- CMakeLists.txt
- README.md

## Comandos de uso
~~~powershell
.\build\conveyor-release\sim_project.exe validate examples/reference-conveyor.json
.\build\conveyor-release\sim_project.exe replay examples/reference-conveyor.json
.\build\conveyor-release\sim_project.exe replay examples/reference-pulses.json
.\build\conveyor-release\sim_project.exe inspect examples/reference-conveyor.json
.\build\conveyor-release\sim_project.exe inspect examples/reference-pulses.json
~~~

run e control permanecem disponíveis. inspect não grava arquivo; replay devolve código 0 quando os resultados coincidem e 3 quando encontra divergência. Erros de carga/execução mantêm código 1. Uma divergência informa, por exemplo: tick=3 entity=axis variable=trace[3].position_um expected=10 actual=123.

## Compilação e validação
GCC 16.2.0 no Windows x64, C++20, CMake/Ninja e dependências locais existentes. MSVC não foi encontrado por cl no PATH nem pelo local padrão de vswhere com o componente C++; não foi testado.

Comandos executados na raiz atual:
~~~powershell
$toolRoot = 'C:/Users/ulric/Documents/Codex/2026-10-24/referenced-chatgpt-conversation-this-is-an/work/tools'
$env:PATH = (Join-Path $toolRoot 'w64devkit/bin') + ';' + $env:PATH
$env:TEMP = (Resolve-Path build/temp).Path
$env:TMP = $env:TEMP
cmake --build build/conveyor-debug -j 4
ctest --test-dir build/conveyor-debug --output-on-failure --output-junit test-results.xml
cmake --build build/conveyor-release -j 4
ctest --test-dir build/conveyor-release --output-on-failure --output-junit test-results.xml
~~~
Foram adicionados 20 testes: 8 de diagnóstico/projeção de replay, 11 de integração/aceitação e 1 de CLI. Os 145 anteriores permanecem na suíte, totalizando 165 por configuração.

## Limitações e preparação para Qt Widgets
Sem gravação persistente do histórico ou retomada a partir de checkpoint; replay começa do estado inicial. A referência física continua com uma peça e uma passagem. Os ensaios temporais não são uma prova de equivalência entre todos os compiladores e arquiteturas.

Antes de iniciar Qt Widgets:
1. Concluir validação MSVC x64 em Debug/Release e resolver bloqueios locais de execução pela política do Windows.
2. Usar estes cenários e tabelas como aceitação da futura interface.
3. Definir um adaptador de apresentação que consuma resultados imutáveis e encaminhe comandos ao Runner; widgets e relógio da UI não poderão avançar o núcleo.
4. Mapear índices funcionais aos UUIDs e decidir atualização incremental de estado sem mover Qt para o núcleo.
5. Testar abrir/validar/executar/pausar/retomar/parar pela interface com os mesmos resultados headless.

Nenhum comando Git, arquivo compactado, Qt/VTK, nova instrução Ladder ou funcionalidade educacional foi introduzido.

## Resultado final verificado em 2026-10-30

| Configuracao | Compilador | Aprovados | Pendencias |
| --- | --- | --- | --- |
| Release | GCC 16.2.0 / Windows x64 | 165 de 165 | Nenhuma falha na suite |
| Debug | GCC 16.2.0 / Windows x64 | 155 de 165 | 10 testes de console impedidos de executar |
| Debug / Release | MSVC | Nao executados | Compilador oficial indisponivel neste ambiente |

Dos 20 testes novos, os 20 passaram em Release; em Debug, os 19 de nucleo/integracao passaram e replay_cli ficou impedido. Os 145 testes anteriores passaram em Release. A validacao completa de Debug permanece pendente: nao considerar esta configuracao aprovada.

O Windows informou bloqueio de sim_project.exe Debug pelo Controle de Aplicativo. A repeticao da suite, sem modificar o executavel, manteve os dez impedimentos: conveyor_validate, conveyor_run, conveyor_cli, project_validate, project_run, project_cli_roundtrip, ton_cli, ctu_cli, comparison_cli e replay_cli. Nenhuma politica de seguranca foi alterada.

Em Release, compilacao e link terminaram, mas a primeira descoberta automatica dos testes falhou ao iniciar os executaveis. Quando os mesmos binarios passaram a iniciar, a descoberta foi retomada, sem recompilar ou alterar os binarios, com:

~~~powershell
cmake -P build/conveyor-release/sim_tests_94c1d3b2_discovery.cmake
cmake -P build/conveyor-release/sim_project_tests_92a5c48c_discovery.cmake
ctest --test-dir build/conveyor-release --output-on-failure --output-junit test-results.xml
~~~

A suite Release terminou com 165 aprovados em 10,32 segundos. O log inicial de build conserva a falha de descoberta; nao representa uma aprovacao inicial do comando Ninja.

Replays Release confirmados novamente ao finalizar:

~~~text
reference-conveyor.json: REPLAY identical ticks=12 events=16 counter_events=3
reference-pulses.json: REPLAY identical ticks=15 events=0 counter_events=11
~~~

O segundo cenario nao tem eventos mecanicos de esteira: usa entradas controladas, snapshots e eventos de contador. A igualdade compara os resultados completos produzidos, independentemente do limite de 64 snapshots exibidos por inspect.

Evidencias locais: build/replay-debug-build.txt, build/replay-debug-first-attempt.txt, build/replay-debug-tests.txt, build/replay-release-build.txt, build/replay-release-tests.txt e test-results.xml em cada diretorio de configuracao.

Implementacao e verificacao Release concluidas. Para encerrar a validacao de plataforma antes da interface, resta liberar legitimamente a execucao do console Debug no ambiente Windows, repetir seus testes e executar a suite com MSVC x64. Nenhuma aprovacao MSVC e declarada.
