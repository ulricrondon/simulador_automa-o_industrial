# InteractiveSession: consulta de historico e reserva de eventos — 2026-10-30

Contrato completo em [interactive-session.md](interactive-session.md).

## Consulta de historico (A5)

Antes, history() fazia a copia profunda sob o mesmo mutex de publication() e do passo. Agora:

- publication() tem mutex proprio, usado apenas para a troca de um ponteiro.
- O trace guarda shared_ptr<const Snapshot>, e latest aponta para o mesmo objeto.
- Eventos, recibos e journal ficam em blocos imutaveis de 4096 registros, compartilhados pelas visoes.
- history_view() e snapshots() sao as consultas novas, sem copia profunda.
- history() mantem assinatura e resultado; copia fora dos locks da sessao.
- O descarte do modo bounded usa deque, sem mover a janela a cada tick. replay_session segue o mesmo criterio.

## Reserva de eventos

O passo reservava todos os eventos configurados, mesmo os de outros ticks, e todas as entradas e saidas do PLC. Agora reserva os eventos e entradas do proprio tick e o que o passo pode emitir, conforme a conexao real do programa. O limite real continua falhando com history_limit, sem descartar registros.

## Medicao

Harness em build/review-interactive (fora do produto). GCC 16.2.0 -O2, Windows x64. Cenario: 100.000 ticks, 256 entradas e saidas, TON e CTU. O "antes" e o codigo da entrega A1-A4; tres execucoes quando ha faixa.

| Medida | Antes | Depois |
|---|---:|---:|
| Espera de publication() durante history() com 99.000 snapshots | 470 a 695 ms | 0,4 a 0,8 ms |
| Duracao de history() para quem chama | 469 a 695 ms | 444 a 933 ms |
| Commit acrescentado por history() | cerca de 940 MiB | cerca de 941 MiB |
| history_view() com 99.000 snapshots | nao existia | 1,7 a 2,2 ms |
| Commit de 20 visoes retidas | nao existia | cerca de 30 MiB |
| Pagina de 4096 snapshots | nao existia | 0,09 a 0,17 ms |
| Ticks/s com bounded 256 e leitor continuo | 120 mil a 135 mil | 278 mil a 315 mil |
| Pior espera de publication() nesse avanco | 7 a 19 ms | 0,13 a 0,19 ms |
| Commit com bounded 256 | 8,2 MiB | 5,7 MiB |
| Documento com 100.000 eventos distribuidos | history_limit no primeiro passo | Completa os 100.000 ticks |

Depois da correcao, sem medida anterior comparavel: bounded 50.000 faz 245 mil ticks/s com 483 MiB de commit; full 100.000 faz 184 mil ticks/s com 965 MiB. history() continua fazendo copia integral por compatibilidade: seu custo fica com quem chama, e nao bloqueia a sessao.

## Validacao

GCC Debug e Release: 209/209 no CTest, sendo 198 anteriores e 11 novos em tests/interactive_history_test.cpp. As saidas do console para os 36 ensaios sobre examples/ tem os mesmos SHA-256 da medicao anterior de memoria. Testes de expectativa existentes nao foram alterados. MSVC nao foi executado.
