# TON no Ladder ? incremento funcional
Data: 27/10/2026.

## Entrega
Temporizador de retardo na ativa??o, n?o retentivo, integrado ao scanner Ladder existente. Cada rung pode ter ton_us; sua bobina recebe Q do TON aplicado ao resultado dos contatos. O temporizador pertence ao UUID do rung, sem entidade ou refer?ncia duplicada. H? no m?ximo um TON por rung. N?o ? uma implementa??o completa de blocos IEC 61131-3.

A primeira amostra HIGH inicia ET em zero. Cada nova varredura consecutiva HIGH acumula o per?odo simulado, limitado ao preset. Q fica HIGH quando ET atinge o preset. Preset zero ativa imediatamente. Preset que n?o ? m?ltiplo do per?odo ativa na primeira fronteira posterior ao prazo; n?o s?o introduzidos subpassos.

LOW zera ET e Q. UNDEFINED e FAULT zeram ET, desativam a contagem e s?o propagados para Q; ao recuperar HIGH a contagem reinicia. Somente HIGH permite movimento. A prioridade de falhas continua sendo a do modelo el?trico existente.

Pause preserva ET e n?o avan?a tempo. Resume continua da fronteira anterior. Stop limpa os temporizadores internos e termina a sess?o; os snapshots hist?ricos permanecem imut?veis. Uma nova execu??o sempre come?a sem estado residual. Atingir o horizonte continua distinto de stop.

Os contatos de sa?da continuam lendo a imagem anterior. Estados dos timers s?o indexados pela bobina, para que reordenar os rungs n?o altere o resultado. O scanner sem estado rejeita programas temporizados; execu??o normal e controlada mant?m o estado na mesma thread dedicada existente.

## Exemplo e observabilidade
examples/conveyor-ton.json configura atraso de 2000 ?s. A entrada sobe em 1000 ?s; ET passa por 0 e 1000 e chega a 2000 em 3000 ?s. O motor ent?o movimenta a pe?a. Posi??es ao final dos oito passos: 0,0,0,10,20,20,20,20 ?m. O sensor detecta a pe?a no final do tick 4 e desliga o motor na varredura seguinte.

Snapshots armazenam ET, estado ativo e Q. O console adiciona linhas TIMER,tick,coil,elapsed_us,Q para os slots de bobina de programas temporizados; slots sem TON ficam no estado inicial. O tick identifica a avalia??o do Ladder no in?cio do passo; o time_us do snapshot continua marcando o final do passo. Eventos existentes registram as altera??es de bobina, motor e sensor.

## Persist?ncia
Novo perfil estrito: formato 1.2, modelo 3, engine conveyor-ton-v1, com ao menos um rung temporizado. Preset inteiro entre zero e INT64_MAX em microssegundos, validado antes da execu??o. Os schemas 1.0 e 1.1 n?o foram alterados. Perfis anteriores rejeitam ton_us, evitando interpreta??o silenciosa. UUIDs e conex?es s?o preservados no round-trip.

Persist?ncia de TON nesta etapa est? limitada ao cen?rio conveyor; o n?cleo tamb?m executa TON com o eixo linear legado em mem?ria. Salva-se configura??o, n?o checkpoint/ET de uma sess?o ativa. Remover todos os TON volta ? serializa??o conveyor-v1; nenhum documento antigo ? migrado automaticamente.

## Arquivos
Criados: schemas/project-1.2.schema.json, examples/conveyor-ton.json, tests/ton_test.cpp, tests/ton_project_test.cpp, tests/ton_cli.cmake e docs/ton.md.

Modificados: CMakeLists.txt; include/sim/core/simulation.hpp e conveyor.hpp; src/core/electrical.cpp, simulation.cpp e conveyor.cpp; src/project/document.cpp e schema.hpp.in; apps/project/main.cpp; README.md.

## Compila??o e valida??o
Foram reutilizados os diret?rios CMake existentes, com Ninja e GCC 16.2.0 no Windows x64. A regenera??o CMake incluiu o novo schema e os testes. Depend?ncias locais existentes, sem download. MSVC n?o foi executado; presets e manifesto vcpkg foram preservados.

Comandos executados na raiz do projeto:
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

Os testes novos cobrem prazo exato e n?o m?ltiplo, preset zero, reset LOW/UNDEFINED/FAULT, pausa/retomada/parada, satura??o num?rica, independ?ncia de timers, ordena??o de rungs, replay entre jobs, argumentos inv?lidos, persist?ncia e CLI. A su?te anterior da esteira permanece inclu?da.

## Executar o exemplo
~~~powershell
./build/conveyor-debug/sim_project.exe validate examples/conveyor-ton.json
./build/conveyor-debug/sim_project.exe run examples/conveyor-ton.json
./build/conveyor-debug/sim_project.exe control examples/conveyor-ton.json step step pause step resume step step stop
~~~

## Limites e pr?ximo avan?o
Sem TOF/TP, temporizadores retentivos, acesso ET por instru??es Ladder, m?ltiplos blocos em um rung ou edi??o durante a execu??o. Sem altera??o do modelo mec?nico, interface gr?fica ou arquivos compactados. Nenhuma opera??o Git.

Pr?ximo incremento recomendado: contador CTU por borda de subida, com reset e testes de replay; preparar a contagem de passagens pelo sensor antes de ampliar para v?rias pe?as.

## Resultado confirmado

| Configuração | Aprovados | Falhas |
|---|---:|---:|
| debug | 97 | 0 |
| release | 97 | 0 |

97 testes por configuração: 86 anteriores e 11 novos. Total de 194 execuções aprovadas. Evidências em build/conveyor-debug/test-results.xml e build/conveyor-release/test-results.xml; logs em build/ton-debug-tests.txt e build/ton-release-tests.txt. A compilação inicial identificou uma cópia desnecessária no teste JSON (aviso tratado como erro); corrigida antes da execução final das duas suítes.


Nota de localiza??o (27/10/2026): entrega integrada em C:\Projetos\simulador_industrial\simulador_automa-o_industrial. Os resultados acima documentam as entregas originais; a revalida??o neste diret?rio consta em integracao-local.md.
