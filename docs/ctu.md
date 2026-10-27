# Contador CTU — entrega funcional
Data: 27/10/2026.
Diretório: C:\Projetos\simulador_industrial\simulador_automa-o_industrial.

## Implementação
CTU integrado ao scanner Ladder existente, sem outro escalonador. CU é o resultado AND/OR dos contatos NO/NC do rung; R é um contato explícito de entrada ou saída, opcionalmente invertido. PV é um inteiro de configuração; CV é int64_t, exposto por CounterState em snapshots e eventos, nunca convertido em booleano. A bobina do rung publica Q. Contatos de saída leem Q da varredura anterior, preservando a publicação simultânea existente.

Cada instância pertence ao UUID persistente do rung. A bobina única identifica seu slot durante a execução. IDs duplicados, bobinas com mais de um escritor, referências fora das imagens e TON+CTU no mesmo rung são rejeitados. O scanner também verifica escritores duplicados antes de modificar estados quando chamado diretamente. A API primitiva update_ctu representa uma amostra; seu chamador de produção é o scanner, exatamente uma vez por rung por varredura.

TON e CTU podem coexistir em rungs diferentes e se conectar por contatos de saída. Não houve alteração da mecânica da esteira, introdução de GUI, Qt/VTK, recursos educacionais ou compactação.

## Semântica dos casos especiais
| Situação | CV | Q e memória de borda |
|---|---|---|
| Antes da primeira varredura | 0 | LOW, desarmado; um HIGH inicial não conta |
| R=LOW e CU=LOW | Mantém | Q=(CV>=PV); arma a próxima subida |
| R=LOW e CU=HIGH após LOW válido | Incrementa uma vez | Q=(CV>=PV); desarma |
| CU permanece HIGH | Mantém | Não há nova borda |
| CV atinge ou ultrapassa PV | Continua contando | Q fica HIGH com entradas válidas |
| R=HIGH | Zera com prioridade absoluta | Q=LOW, desarma, inclusive se CU inválido |
| CU inválido ou R inválido | Mantém | Desarma; Q=FAULT se algum for FAULT, senão UNDEFINED |
| Recuperação dos sinais | Mantém até nova borda | Exige CU=LOW com R=LOW para rearmar; HIGH direto não conta |
| PV=0 | Inicialmente 0 | Antes do primeiro scan Q=LOW; com entradas válidas e R=LOW, Q=HIGH; durante reset Q=LOW |
| Saturação | Máximo INT64_MAX (9223372036854775807) | Não ocorre overflow; Q permanece conforme comparação |

Um reset desconhecido nunca é tratado como desligado. A contagem é preservada durante diagnóstico, mas nenhuma borda é inferida do intervalo desconhecido. Ao recuperar entradas válidas, Q volta a representar CV>=PV mesmo que não haja nova borda. FAULT e UNDEFINED são diagnósticos observáveis na saída; somente HIGH aciona movimento.

Pause/resume preserva CV, Q e armamento, sem varreduras enquanto pausado. Stop limpa o estado transitório, força saídas LOW e é terminal; snapshots anteriores permanecem imutáveis. O término normal do horizonte preserva a última amostra, como antes. Uma nova execução cria estados zerados.

O CTU não usa duração, tempo real ou recursos carregados. Eventos externos no mesmo tick são resolvidos pela sequência existente antes da amostragem; não representam várias bordas dentro de uma única varredura. Os eventos de contador incluem tick, tempo simulado, bobina, CV, Q, armamento e indicador de stop; sua ordem é por bobina, independente da ordem dos rungs.

## Exemplos
- examples/ctu-pulses.json: bancada de pulsos controlados que representam a entrada de um sensor fotoelétrico. Usa o eixo linear já existente, três entradas (habilitação, pulsos e reset), TON no acionamento e CTU com PV=3. Conta nos ticks 1, 3 e 5, inibe o acionamento no tick 6 via contato NC de Q, continua até CV=4 no tick 8, reseta no tick 10 e volta a CV=1 no tick 13. Não afirma que uma única peça física fez quatro passagens.
- examples/conveyor-ctu.json: integração real com o sensor geométrico da esteira existente, PV=1. Detecta uma passagem, ativa Q no tick 3 e desliga o motor no tick 4, usando Q da imagem anterior. A peça termina em 30 µm; a latência de uma varredura é intencional. Reset ligado ao contato NC do comando de habilitação.

## Persistência e compatibilidade
Novo perfil estrito 1.3/modelo 4/ladder-ctu-v1, exigindo ao menos um CTU. O rung recebe somente o objeto ctu com pv e reset (bank, signal, normally_closed). Reutiliza UUIDs, contatos e referências por banco/índice; não cria uma identidade concorrente para o mesmo bloco. Configurações suportam o eixo linear ou a esteira existente. CV, Q e armamento não entram no documento e campos transitórios são rejeitados pelo schema.

Schemas 1.0, 1.1 e 1.2 permanecem inalterados. Projetos anteriores continuam carregando e serializando nos respectivos perfis. Perfis antigos rejeitam instruções CTU, sem migração silenciosa. PV inteiro de 0 a INT64_MAX é validado por Draft-07 e pela semântica; números fracionários e valores maiores são rejeitados. A identidade é verificada globalmente no documento, e reset/CU usam referências de sinal validadas contra a imagem PLC.

## Arquivos criados
- src/core/counter.cpp
- schemas/project-1.3.schema.json
- examples/ctu-pulses.json
- examples/conveyor-ctu.json
- tests/ctu_test.cpp
- tests/ctu_project_test.cpp
- tests/ctu_cli.cmake
- docs/ctu.md

## Arquivos modificados
- include/sim/core/simulation.hpp
- include/sim/core/conveyor.hpp
- src/core/electrical.cpp
- src/core/simulation.cpp
- src/core/conveyor.cpp
- src/project/document.cpp
- src/project/schema.hpp.in
- apps/project/main.cpp
- CMakeLists.txt
- README.md

## Compilação e testes
Compilador disponível: GCC 16.2.0 no Windows x64, CMake/Ninja e dependências locais já configuradas. Não foi encontrado MSVC por cl no PATH nem pelo local padrão do vswhere com o componente C++; MSVC não foi testado. C++20, os presets MSVC e o manifesto vcpkg foram preservados.

Comandos executados a partir da raiz atual:
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

A regeneração CMake inclui o schema novo. Os testes cobrem bordas e todas as combinações de transição de CU, HIGH contínuo, reset prioritário, PV zero, além de PV, saturação, CU/R inválidos, rearmamento, pausa, stop, rejeição de atualização duplicada, contatos NO/NC e AND/OR, integração TON, sensor, eventos, identidade, referências, persistência, fixtures e CLI. A primeira execução apontou um timestamp esperado incorreto em um teste novo: o sensor rearma em 5000 µs, não em 7000 µs. A expectativa foi corrigida conforme a sequência geométrica, sem alterar a implementação para acomodar o teste.

## Executar
~~~powershell
.\build\conveyor-debug\sim_project.exe validate examples/ctu-pulses.json
.\build\conveyor-debug\sim_project.exe run examples/ctu-pulses.json
.\build\conveyor-debug\sim_project.exe run examples/conveyor-ctu.json
.\build\conveyor-debug\sim_project.exe control examples/conveyor-ctu.json step step pause step resume step step stop
~~~
As linhas COUNTER apresentam transições, inclusive CV e diagnósticos, com timestamps do início da varredura. Snapshots continuam marcando o fim do passo. A API de controle permanece disponível para sessões da esteira; a bancada de pulsos é executada em lote pelo Runner existente.

## Limitações e próximo avanço
Um CTU ou um TON por rung; reset por um único contato. CV pode ser inspecionado por API/console, mas ainda não é operando de comparações aritméticas Ladder. Sem CTD, contagem retentiva entre execuções, checkpoint ou eventos subscan. O limite prático do número de bordas também depende do horizonte de até 100000 ticks. A mecânica continua com uma peça unidirecional.

Próximo avanço recomendado: comparação inteira de CV com constante no Ladder, mantendo o acesso tipado, limites explícitos e imagem coerente da varredura anterior.

Nenhum comando Git foi executado. Nenhum ZIP, instalador ou pacote foi criado.

## Resultado confirmado

| Build | Aprovados | Falhas | Tempo CTest |
|---|---:|---:|---:|
| debug | 123 | 0 | 4 s |
| release | 123 | 0 | 9 s |

123 testes por build: 97 anteriores e 26 novos. Total de 246 execucoes aprovadas, sem testes ignorados. Evidencias: build/conveyor-debug/test-results.xml e build/conveyor-release/test-results.xml; logs build/ctu-debug-tests.txt e build/ctu-release-tests.txt.
