# Decisões de implementação — incremento 0.1

## Direção de dependências

`sim_headless → sim_runtime → sim_core`.

O núcleo compila somente com a biblioteca padrão C++20. O runtime usa threads padrão. Qt Widgets e VTK entrarão em um alvo desktop separado; tipos Qt/VTK não atravessarão a API funcional. Persistência converterá DTOs validados em valores do núcleo.

## Tempo, ordem e replay

- Tick: uint64, período em microssegundos inteiros. O primeiro tick é 0; snapshot registra fim do passo.
- Eventos são ordenados por (tick, sequence). Sequence é único na execução, fornecido antes do despacho.
- A mesma lista lógica pode chegar em ordem diferente no vetor e produzir o mesmo resultado. Se dois comandos disputam uma entrada no mesmo tick, a maior sequência prevalece.
- Entradas começam falsas. Cada passo aplica eventos, avalia Ladder contra a imagem anterior das saídas, publica todas as bobinas simultaneamente, avança o eixo e registra o estado.
- Bobinas sem rung ficam falsas. Mais de um escritor para a mesma bobina é erro.
- Contatos de saída veem o scan anterior: isto é semântica própria do IR inicial, deliberadamente explícita, não equivalência com qualquer PLC comercial.
- Limites iniciais: 100.000 ticks, 256 entradas e 256 saídas, 4.096 contatos, 100.000 eventos. Duração é verificada contra overflow.
- A posição usa micrômetros int64 não negativos, movimento por tick não negativo e saturação sem overflow. Não há integração em ponto flutuante neste cenário.

O determinismo garantido é para o mesmo modelo, entradas e semântica desta versão. Não se promete compatibilidade bit a bit entre versões futuras. A futura versão de modelo/engine deve acompanhar replays persistidos.

## Concorrência e encerramento

`Runner` aceita execuções completas por valor. Validação acontece antes da admissão; a thread dedicada executa uma tarefa de cada vez. Capacidade padrão: duas execuções pendentes, máximo oito, mais uma ativa. Fila cheia falha explicitamente com `QueueFull`; não bloqueia a interface nem descarta silenciosamente.

O destrutor solicita parada, fecha a fila e aguarda a thread. Execução interrompida lança `RunCancelled`; pendências recebem a mesma exceção. Não há snapshots parciais tratados como sucesso. Como qualquer objeto C++, o dono deve impedir chamadas concorrentes com a destruição. Futuros e resultados podem sobreviver ao Runner.

A fila genérica é limitada por quantidade; os limites de RunSpec contêm o tamanho de cada tarefa. Não há limite global para resultados guardados pelos consumidores. Streaming de snapshots e edição ao vivo são incrementos futuros.

## Assets

`AssetCache` é um cache de buffers CPU. O orçamento cobre retenção forte de payloads; o índice fraco também é limitado. Operações são serializadas por mutex. A política de descarte é determinística para a mesma ordem de acessos; concorrência pode mudar essa ordem, mas cache não altera o estado físico.

Não foi introduzido pool genérico antes de existir carregador real. O escalonador já usa `std::jthread`; os workers de assets serão adicionados com fila própria, limites de bytes em trânsito e publicação imutável. Objetos VTK serão criados e destruídos pelo adaptador de apresentação no contexto apropriado.

## Build e dependências

MSVC/Windows x64 permanece o alvo de produto. CMake 3.25+, C++20, Ninja e runtime MSVC dinâmico; avisos rigorosos nos alvos próprios. GoogleTest vem do manifesto ou de uma árvore local explicitamente informada.

Baseline vcpkg: `9eec002df1cd109f8f104cd966a763daf9751489`, release [2026.10.16](https://github.com/microsoft/vcpkg/releases/tag/2026.10.16). O nome do port do validador é [json-schema-validator](https://vcpkg.io/en/package/json-schema-validator.html), implementação nlohmann/json-schema-validator; não `nlohmann-json-schema-validator`.

Features desktop/persistência não são habilitadas na compilação do núcleo. O aplicativo não efetua downloads nem acessa rede ao executar. Downloads são apenas uma etapa do ambiente de desenvolvimento.

## Próximos incrementos, sem nova votação por etapa

1. Documento de projeto: UUIDv4, DTOs versionados, Draft-07, validação semântica e fixtures; SHA-256 de conteúdo; rejeitar migração silenciosa.
2. Pacote local minizip-ng: limites de expansão, nomes normalizados, gravação atômica, índice de assets e migrações explícitas.
3. Carregador híbrido e workers de assets com cancelamento, deduplicação e integração do cache.
4. Shell Qt Widgets: abrir/salvar, comandos de execução, painel de estado e ponte de snapshots para a UI.
5. Adaptador VTK encapsulado e importação glTF 2.0; expandir o cenário industrial e o IR Ladder.

O próximo incremento concreto é persistência validada. Ele cria a fronteira entre projeto editável e núcleo executável antes de investir em interface e 3D. Funcionalidades educacionais ficam fora do escopo.
