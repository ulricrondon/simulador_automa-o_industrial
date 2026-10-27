# Esteira transportadora — entrega funcional headless

Data: 24/10/2026.

## Funcionalidades
Integração do PLC/Ladder existente com acionamento ideal, eixo linear reutilizado, uma peça e sensor fotoelétrico. O console carrega JSON, executa na thread dedicada e apresenta snapshots e eventos. Comandos step/pause/resume/stop são enviados como roteiro ao mesmo Runner, com fila limitada e resultados imutáveis. Não há relógio de parede na evolução funcional.

Exemplo: comando I0 inicia o motor; contato normalmente fechado I1 representa o sensor; bobina O0 comanda o acionamento. Com passo de 1000 µs e velocidade 10000 µm/s, as posições são 0,10,20,20,20,20,20,20 µm. O sensor fica HIGH no final do tick 2 (3000 µs); o PLC o amostra no tick 3, desligando o motor.

## Decisões e contrato funcional
1. Fases: eventos externos → imagem de entradas → Ladder → publicação das saídas/acionamento → integração mecânica → sensor. As saídas lidas por contatos Ladder são da varredura anterior. Bobinas são publicadas juntas.
2. Estados LOW/HIGH/UNDEFINED/FAULT têm tipos funcionais distintos para alimentação, comando, entrada PLC, saída PLC e comando do motor. FAULT domina AND/OR; em AND, LOW domina UNDEFINED; em OR, HIGH domina UNDEFINED. Inversão preserva UNDEFINED/FAULT. Somente acionamento HIGH move a peça.
3. Motor ideal: alimentação AND comando, sem inércia ou cálculo analógico. Deslocamento inteiro em µm com resto em milionésimos de µm, calculado por velocidade × período / 1000000. Limites e produtos são validados contra overflow.
4. Geometria: peça ocupa [posição, posição + comprimento); sensor ocupa [início, fim). O sensor considera o intervalo varrido pela peça no passo, [posição anterior, posição nova + comprimento), evitando perder travessias completas. É uma detecção por intervalo, não um cálculo de instante de cruzamento. Sem movimento usa a sobreposição estática. Sensor sem alimentação HIGH publica UNDEFINED; alimentação FAULT publica FAULT.
5. Sensor inicial é calculado antes da primeira varredura. Depois, sua publicação (inclusive alterações de alimentação) só afeta o PLC no passo seguinte. Há três ligações tipadas obrigatórias; escritores duplicados, referências incompatíveis e realimentação sensor→PLC no mesmo passo são rejeitados. Não há solucionador de ciclos algébricos.
6. Pause congela tempo, posição, imagens e eventos futuros, preservando o estado lógico do acionamento; resume continua na mesma fronteira. Stop força saídas/comando LOW, zera velocidade e termina a sessão sem avançar tempo; FAULT da alimentação permanece visível. Um horizonte concluído preserva a última amostra, não acrescenta uma parada artificial. Resultados anteriores permanecem imutáveis.
7. JSON existente 1.0/modelo 1/discrete-v1 continua suportado sem alteração de schema. Esteira usa perfil explícito 1.1/modelo 2/conveyor-v1, com Draft-07, UUIDv4 únicos e referências verificadas por função. Serialização preserva configuração, identidades, conexões e eventos. Não é checkpoint de uma sessão em andamento; não há migração automática.
8. Reutilizados o scanner Ladder, avanço linear saturado, Runner/std::jthread, parser/validador JSON e fábricas de identidade. Núcleo só usa C++20 padrão. Não foram adicionadas dependências, interface, VTK, arquivos compactados ou operações Git.

## Arquivos
Criados:
- include/sim/core/electrical.hpp
- include/sim/core/conveyor_model.hpp
- include/sim/core/conveyor.hpp
- src/core/electrical.cpp
- src/core/conveyor.cpp
- src/project/conveyor_codec.hpp
- src/project/conveyor_codec.cpp
- schemas/project-1.1.schema.json
- examples/conveyor.json
- tests/conveyor_test.cpp
- tests/conveyor_project_test.cpp
- tests/conveyor_cli.cmake
- docs/conveyor.md (este relatório)

Modificados:
- CMakeLists.txt
- include/sim/core/simulation.hpp
- src/core/simulation.cpp
- include/sim/runtime/runner.hpp
- src/runtime/runner.cpp
- include/sim/project/document.hpp
- src/project/document.cpp
- src/project/schema.hpp.in
- apps/project/main.cpp
- README.md

Artefatos locais de validação ficam em build/conveyor-debug e build/conveyor-release, incluindo test-results.xml, saída do CTest e documentos temporários dos testes. Não são pacotes de distribuição.

## Compilação e testes
Executados localmente no Windows x64 com GCC 16.2.0, CMake 4.4.2 e Ninja, usando fontes de dependências já disponíveis. MSVC não está instalado neste ambiente; seus presets e o manifesto vcpkg foram preservados, mas esta entrega não foi validada com MSVC.

Comandos de configuração e compilação utilizados (a partir da raiz industrial-sim):
~~~powershell
$toolRoot = 'C:/Users/ulric/Documents/Codex/2026-10-24/referenced-chatgpt-conversation-this-is-an/work/tools'
$env:PATH = (Join-Path $toolRoot 'w64devkit/bin') + ';' + $env:PATH
New-Item -ItemType Directory -Force -Path build/temp | Out-Null
$env:TEMP = (Resolve-Path build/temp).Path
$env:TMP = $env:TEMP

cmake -S . -B build/conveyor-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++ "-DSIM_GTEST_SOURCE_DIR=$toolRoot/googletest-1.18.0" "-DSIM_JSON_SOURCE_DIR=$toolRoot/json-3.12.0" "-DSIM_JSON_SCHEMA_SOURCE_DIR=$toolRoot/json-schema-validator-2.4.0"
cmake --build build/conveyor-debug -j 4
ctest --test-dir build/conveyor-debug --output-on-failure --output-junit test-results.xml

cmake -S . -B build/conveyor-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ "-DSIM_GTEST_SOURCE_DIR=$toolRoot/googletest-1.18.0" "-DSIM_JSON_SOURCE_DIR=$toolRoot/json-3.12.0" "-DSIM_JSON_SCHEMA_SOURCE_DIR=$toolRoot/json-schema-validator-2.4.0"
cmake --build build/conveyor-release -j 4
ctest --test-dir build/conveyor-release --output-on-failure --output-junit test-results.xml
~~~

Os testes cobrem acionamento, desligamento, movimento, limites geométricos e passagem rápida, sinais de quatro estados, falhas de alimentação, resposta Ladder, ordem temporal, integração fracionária, eventos embaralhados com replay idêntico, pause/resume/stop, execução dedicada, persistência e rejeição de parâmetros/conexões/referências inválidos. Os 51 testes anteriores permanecem na suíte.

## Resultado da validação

| Configuração | Aprovados | Falhas | Tempo CTest |
|---|---:|---:|---:|
| Debug | 86 | 0 | 7 s |
| Release | 86 | 0 | 5 s |

Total: 172 execuções aprovadas (86 testes por configuração), incluindo os 51 anteriores e 35 novos. Evidências: build/conveyor-debug/test-results.xml e build/conveyor-release/test-results.xml. Saída do cenário: build/conveyor-release/conveyor-trace.txt.

## Uso
~~~powershell
./build/conveyor-debug/sim_project.exe validate examples/conveyor.json
./build/conveyor-debug/sim_project.exe run examples/conveyor.json
./build/conveyor-debug/sim_project.exe control examples/conveyor.json step step pause step resume step stop step
~~~
O comando new-conveyor emite uma configuração JSON com novos UUIDs. O teste de CLI salva esse documento em arquivo, recarrega, valida e executa.

## Limitações e próximo avanço
Uma esteira, uma peça, movimento positivo e um sensor. Acionamento ideal, sem aceleração, colisões, múltiplas peças ou elétrica analógica. As unidades pequenas do exemplo facilitam verificar números; os parâmetros podem ser ajustados. A detecção por intervalo pode produzir um pulso de um passo após uma travessia; não existe interpolação do instante de atuação nem garantia de parada dentro da zona em velocidades elevadas.

Controle por roteiro determinístico, sem terminal interativo concorrente ou interface gráfica. O subconjunto Ladder ainda não inclui temporizadores. Persistência salva configuração, não estado de execução. A conclusão do horizonte e stop são operações distintas.

Próximo incremento recomendado: temporizador TON no Ladder, exclusivamente em tempo simulado, com testes de pausa, reset e replay, para permitir sequências industriais temporizadas mantendo o cenário atual como regressão.


Nota de localiza??o (27/10/2026): entrega integrada em C:\Projetos\simulador_industrial\simulador_automa-o_industrial. Os resultados acima documentam as entregas originais; a revalida??o neste diret?rio consta em integracao-local.md.
