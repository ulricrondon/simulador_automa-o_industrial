# Memória do replay — 2026-10-30

## Resumo

Foram removidas duas cópias integrais de `RunResult`. O pico de commit do `replay` caiu de 2.857,8 MiB para 1.921,8 MiB (-32,8%), com saídas funcionais idênticas. O consumo restante é o histórico integral de duas execuções, exigido pelo contrato atual de `RunResult`; nada foi truncado.

## Cópias removidas

| Local | Antes | Depois |
|---|---|---|
| `simulate_conveyor` (`src/core/conveyor.cpp`) | `return session.result();` copiava todo o histórico da sessão | `return std::move(session).result();` |
| `Runner::work` (`src/runtime/runner.cpp`) | `auto result = session.result();` copiava o histórico | O status é lido antes, e o histórico é transferido |

`ConveyorSession::result()` agora tem duas sobrecargas. Em uma sessão nomeada continua devolvendo `const RunResult&`. Somente `std::move(session).result()` transfere o histórico, e a sessão não deve ter o resultado lido depois disso. As duas chamadas ocorrem no fim da vida da sessão local.

`step()` também reserva `snapshot.inputs` e `snapshot.outputs` antes de preenchê-los. Isso evita realocações e capacidade excedente, sem alterar valores nem igualdade.

Sem alteração, por não haver cópia de `RunResult`: `compare_runs` (só referências e projeções apenas na divergência), `simulate` legado (`return result;`), `make_shared<const RunResult>` (construção por movimento) e os comandos do console (`shared_ptr`).

## Medição

Cenário: `examples/reference-conveyor.json` em memória, com 100.000 ticks e 256 entradas e saídas, enviado por named pipe. `control` usa 6.000 passos. GCC 16.2.0 Release, Windows x64, três execuções por modo, mediana. Script: `build/memory-probe.ps1`. Dados: `build/memory-before.json` e `build/memory-after.json`.

| Modo | Commit de pico antes | Depois | Variação | Working set antes | Depois |
|---|---:|---:|---:|---:|---:|
| `replay` | 2.857,8 MiB | 1.921,8 MiB | -32,8% | 2.772,8 MiB | 1.865,9 MiB |
| `inspect` | 1.908,9 MiB | 962,5 MiB | -49,6% | 1.854,1 MiB | 937,3 MiB |
| `control` | 135,7 MiB | 78,9 MiB | -41,9% | 117,5 MiB | 62,4 MiB |

Os picos são amostrados no processo vivo a cada 5 ms aproximadamente. A alocação do MSVC pode produzir números diferentes; não houve medição com MSVC.

Os SHA-256 da saída padrão e os códigos de saída são iguais antes e depois nos 9 ensaios do cenário grande e nos 36 ensaios de `validate`, `run`, `replay`, `inspect` e `control` sobre os exemplos.

## Causa do consumo restante

`RunResult` guarda um `Snapshot` completo por tick, e `replay` mantém dois resultados vivos para compará-los. No cenário medido, cada snapshot carrega cerca de 9,2 KiB de dados:

| Campo | Bytes por tick | Observação |
|---|---:|---|
| `timers` | 4.096 | 256 slots por bobina; o cenário tem um TON |
| `counters` | 4.096 | 256 slots por bobina; o cenário tem um CTU |
| `inputs` e `outputs` | 512 | Imagem em bytes |
| `conveyor.plc_inputs` e `plc_outputs` | 512 | Mesmo conteúdo da imagem em bytes |

São 89% de slots densos de TON e CTU. O consumo é proporcional a ticks × sinais; 256 sinais é o limite do modelo, não o caso típico. Slots esparsos ou a remoção da imagem duplicada mudariam a forma pública de `Snapshot` e os testes que a indexam por bobina, e por isso não foram feitos nesta correção.

## Proposta separada: sessão interativa

Não implementada. A interface não deve depender de `RunResult` crescendo sem limite:

1. Extrair de `ConveyorSession::step()` uma saída por passo, entregue a um consumidor. `RunResult` passa a ser um consumidor que acumula, preservando a API em lote e a ordem das fases.
2. Política de histórico explícita: integral com orçamento em bytes, que falha ou pausa com erro visível ao excedê-lo, ou janela circular que informa o primeiro tick disponível. Nunca descartar sem indicação.
3. Publicar o snapshot mais recente como `shared_ptr<const Snapshot>`, com contador de amostras coalescidas. A interface não acessa a sessão.
4. Comandos em fila limitada, aplicados só na fronteira de passo e registrados com `(tick, sequence)`. Esse registro, e não o histórico, é a entrada do replay de uma sessão ao vivo.
5. Comparação incremental no replay: comparar cada passo com a referência e descartá-lo. A estimativa é reduzir o `replay` para perto de um histórico; o valor não foi medido.
6. Aceitação: a mesma lista de comandos, aplicada pela sessão interativa e pelo lote, precisa produzir resultado idêntico em `compare_runs`.
