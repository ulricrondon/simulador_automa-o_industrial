# Comparadores inteiros de CV — entrega incremental
Data: 30/10/2026.
Projeto: C:\Projetos\simulador_industrial\simulador_automa-o_industrial.

## Funcionalidades e escopo
Condições Ladder CTU.CV == constante, !=, <, <=, > e >=, combináveis com contatos NO/NC e com o AND/OR existente. A leitura é somente de consulta: não incrementa, reseta nem altera armamento ou Q do CTU. CV continua int64_t; a comparação explícita devolve DigitalState::LOW ou HIGH, sem conversão implícita entre inteiro e sinal.

A constante aceita todo o intervalo assinado de 64 bits, de -9223372036854775808 a 9223372036854775807. CV permanece não negativo, limitado a INT64_MAX. Os operadores comparam diretamente, sem subtração ou outras operações que possam causar overflow. Não há interpretador matemático nem aritmética adicional.

## Extensão mínima da representação Ladder
Contact recebeu um campo opcional CvComparison, criado por Contact::compare. Assim, Branch e os programas/initializers existentes permanecem compatíveis. A comparação é alternativa ao contato discreto; não pode trazer simultaneamente banco/sinal/inversão diferentes dos valores neutros. No JSON, oneOf distingue as duas formas, rejeitando campos misturados.

CvComparison contém CounterReference, ComparisonOp e constante int64_t. CounterReference usa o slot de bobina do CTU na representação compilada. A persistência referencia o UUID do rung do contador, que é resolvido e validado ao carregar. Referências ausentes, para rung sem CTU ou com mais de um destino são rejeitadas antes da execução. Trocar a ordem dos rungs não modifica a identidade da referência.

Comparações são admitidas em condições de branches, inclusive como CU de outro CTU ou condição de TON. O reset do CTU continua sendo um contato discreto. A comparação não adiciona um segundo bloco de estado ao rung.

## Semântica temporal e diagnósticos
No início da avaliação Ladder, uma cópia dos estados dos contadores forma a imagem coerente de leitura. Todos os rungs consultam essa imagem, enquanto os CTUs atualizam seus próprios estados para a varredura seguinte. Assim, a comparação de CV e os contatos de Q têm a mesma fronteira temporal: alteração em um scan é percebida no próximo.

Auto-referências e realimentações por comparação são temporalmente atrasadas; não se cria um ciclo algébrico instantâneo nem dependência da ordem visual. A comparação não tem estado próprio e segue pause/resume/stop existentes. Pause não avalia nem avança contadores; resume retoma a mesma fronteira. Stop termina a sessão e limpa estados transitórios, sem reavaliar comparações ou modificar snapshots anteriores.

Um CTU com Q em UNDEFINED ou FAULT carrega diagnóstico sobre sua amostra; a comparação propaga esse estado mesmo que o CV retido satisfaça numericamente a condição. Com Q LOW ou HIGH, a decisão usa exclusivamente CV. Os diagnósticos também são lidos da imagem anterior, com a mesma latência de um scan. Na primeira varredura, CV=0 e o estado inicial é conhecido; comparar com zero pode resultar HIGH. Valores internos inválidos (CV negativo ou estado elétrico desconhecido) geram erro.

## Exemplo verificável
examples/cv-compare-pulses.json reutiliza a bancada de pulsos, os UUIDs e as referências existentes. Não amplia a mecânica. I0 habilita a saída O0, cuja condição é CV do CTU >= 3; os pulsos chegam por I1 e R por I2.

O CTU tem PV=99, portanto Q permanece LOW quando CV atinge 3. Isso demonstra o acesso direto a CV. O terceiro pulso é processado no tick 5 (5000 µs). A condição lê CV=3 no início do tick 6 (6000 µs), quando O0 fica HIGH. O snapshot desse passo registra 7000 µs, conforme o contrato de fim de passo. O reset posterior é observado pela comparação na varredura seguinte.

No console, as linhas COUNTER exibem CV e tempo da mudança. A tabela tick,time_us,input_0,output_0,position_um mostra a saída controlada.

## Persistência e compatibilidade
Novo perfil 1.4/modelo 5/ladder-compare-v1. São exigidos ao menos um CTU e uma comparação. A instrução persistida é:
~~~json
{
  "compare": {
    "counter_ref": "b0000001-0000-4000-8000-000000000001",
    "operator": ">=",
    "constant": 3
  }
}
~~~
A referência precisa apontar ao UUID real de um rung CTU do documento. Não há CV, Q ou estado transitório no arquivo. A validação rejeita operadores não suportados, constantes fracionárias, booleanas, textuais ou fora de int64, UUIDs/referências inválidos e formas misturadas.

Os schemas 1.0 a 1.3 foram preservados. Documentos anteriores permanecem nos seus perfis ao carregar e salvar. Instruções novas são rejeitadas em perfis antigos, sem interpretação silenciosa. A permissão para constantes negativas é exclusiva do operando da comparação; PV, posições e os demais campos anteriores mantêm seus limites originais.

## Arquivos criados
- src/core/comparison.cpp
- schemas/project-1.4.schema.json
- examples/cv-compare-pulses.json
- tests/comparison_test.cpp
- tests/comparison_project_test.cpp
- tests/comparison_cli.cmake
- docs/comparisons.md

## Arquivos modificados
- include/sim/core/simulation.hpp
- src/core/simulation.cpp
- src/core/electrical.cpp
- src/project/document.cpp
- src/project/schema.hpp.in
- apps/project/main.cpp
- CMakeLists.txt
- README.md

## Compilação e testes
Validação local no Windows x64 com GCC 16.2.0, CMake/Ninja e dependências já configuradas. MSVC não foi encontrado via cl no PATH ou pelo local padrão de vswhere com o componente C++; não foi testado. Presets MSVC e manifesto vcpkg preservados.

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
Os testes novos cobrem os seis operadores, limites int64 e zero, diagnósticos, ausência de efeitos colaterais, coerência da imagem anterior, independência da ordem, AND/OR, bobinas, TON, reset, pausa/retomada/stop, replay, rejeição de referências e constantes inválidas, UUIDs e round-trip JSON. Os testes anteriores permanecem incluídos.

## Executar
~~~powershell
.\build\conveyor-debug\sim_project.exe validate examples/cv-compare-pulses.json
.\build\conveyor-debug\sim_project.exe run examples/cv-compare-pulses.json
~~~

## Limitações e próximo avanço
Somente CV de CTU contra uma constante; sem comparação entre contadores, aritmética, variáveis genéricas ou comparação de ET. Não existe observabilidade gráfica nem editor. O controle interativo por roteiro continua limitado às sessões de esteira, como antes; a bancada de pulsos é executada em lote.

Próxima entrega recomendada: consolidar um cenário de sequência industrial usando os blocos já disponíveis (TON, CTU e comparações), com critérios de aceitação e replay completo, antes de ampliar o conjunto de instruções.

Nenhum comando Git, arquivo compactado, instalador, Qt/VTK ou funcionalidade educacional foi introduzido.

## Resultado confirmado

| Build | Aprovados | Falhas | Tempo CTest |
|---|---:|---:|---:|
| debug | 145 | 0 | 13 s |
| release | 145 | 0 | 7 s |

22 testes novos (14 de nucleo, 7 de persistencia e 1 de CLI), somados aos 123 anteriores: 145 por build e 290 execucoes aprovadas, sem testes ignorados. Evidencias: build/conveyor-debug/test-results.xml e build/conveyor-release/test-results.xml. Logs: build/comparison-debug-tests.txt e build/comparison-release-tests.txt. Demonstracao: build/comparison-example-output.txt.
