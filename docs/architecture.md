# DecisÃµes de implementaÃ§Ã£o â€” incremento 0.1

## DireÃ§Ã£o de dependÃªncias

`sim_headless â†’ sim_runtime â†’ sim_core`.

O nÃºcleo compila somente com a biblioteca padrÃ£o C++20. O runtime usa threads padrÃ£o. Qt Widgets e VTK entrarÃ£o em um alvo desktop separado; tipos Qt/VTK nÃ£o atravessarÃ£o a API funcional. PersistÃªncia converterÃ¡ DTOs validados em valores do nÃºcleo.

## Tempo, ordem e replay

- Tick: uint64, perÃ­odo em microssegundos inteiros. O primeiro tick Ã© 0; snapshot registra fim do passo.
- Eventos sÃ£o ordenados por (tick, sequence). Sequence Ã© Ãºnico na execuÃ§Ã£o, fornecido antes do despacho.
- A mesma lista lÃ³gica pode chegar em ordem diferente no vetor e produzir o mesmo resultado. Se dois comandos disputam uma entrada no mesmo tick, a maior sequÃªncia prevalece.
- Entradas comeÃ§am falsas. Cada passo aplica eventos, avalia Ladder contra a imagem anterior das saÃ­das, publica todas as bobinas simultaneamente, avanÃ§a o eixo e registra o estado.
- Bobinas sem rung ficam falsas. Mais de um escritor para a mesma bobina Ã© erro.
- Contatos de saÃ­da veem o scan anterior: isto Ã© semÃ¢ntica prÃ³pria do IR inicial, deliberadamente explÃ­cita, nÃ£o equivalÃªncia com qualquer PLC comercial.
- Limites iniciais: 100.000 ticks, 256 entradas e 256 saÃ­das, 4.096 contatos, 100.000 eventos. DuraÃ§Ã£o Ã© verificada contra overflow.
- A posiÃ§Ã£o usa micrÃ´metros int64 nÃ£o negativos, movimento por tick nÃ£o negativo e saturaÃ§Ã£o sem overflow. NÃ£o hÃ¡ integraÃ§Ã£o em ponto flutuante neste cenÃ¡rio.

O determinismo garantido Ã© para o mesmo modelo, entradas e semÃ¢ntica desta versÃ£o. NÃ£o se promete compatibilidade bit a bit entre versÃµes futuras. A futura versÃ£o de modelo/engine deve acompanhar replays persistidos.

## ConcorrÃªncia e encerramento

`Runner` aceita execuÃ§Ãµes completas por valor. ValidaÃ§Ã£o acontece antes da admissÃ£o; a thread dedicada executa uma tarefa de cada vez. Capacidade padrÃ£o: duas execuÃ§Ãµes pendentes, mÃ¡ximo oito, mais uma ativa. Fila cheia falha explicitamente com `QueueFull`; nÃ£o bloqueia a interface nem descarta silenciosamente.

O destrutor solicita parada, fecha a fila e aguarda a thread. ExecuÃ§Ã£o interrompida lanÃ§a `RunCancelled`; pendÃªncias recebem a mesma exceÃ§Ã£o. NÃ£o hÃ¡ snapshots parciais tratados como sucesso. Como qualquer objeto C++, o dono deve impedir chamadas concorrentes com a destruiÃ§Ã£o. Futuros e resultados podem sobreviver ao Runner.

A fila genÃ©rica Ã© limitada por quantidade; os limites de RunSpec contÃªm o tamanho de cada tarefa. NÃ£o hÃ¡ limite global para resultados guardados pelos consumidores. Streaming de snapshots e ediÃ§Ã£o ao vivo sÃ£o incrementos futuros.

## Assets

`AssetCache` Ã© um cache de buffers CPU. O orÃ§amento cobre retenÃ§Ã£o forte de payloads; o Ã­ndice fraco tambÃ©m Ã© limitado. OperaÃ§Ãµes sÃ£o serializadas por mutex. A polÃ­tica de descarte Ã© determinÃ­stica para a mesma ordem de acessos; concorrÃªncia pode mudar essa ordem, mas cache nÃ£o altera o estado fÃ­sico.

NÃ£o foi introduzido pool genÃ©rico antes de existir carregador real. O escalonador jÃ¡ usa `std::jthread`; os workers de assets serÃ£o adicionados com fila prÃ³pria, limites de bytes em trÃ¢nsito e publicaÃ§Ã£o imutÃ¡vel. Objetos VTK serÃ£o criados e destruÃ­dos pelo adaptador de apresentaÃ§Ã£o no contexto apropriado.

## Build e dependÃªncias

MSVC/Windows x64 permanece o alvo de produto. CMake 3.25+, C++20, Ninja e runtime MSVC dinÃ¢mico; avisos rigorosos nos alvos prÃ³prios. GoogleTest vem do manifesto ou de uma Ã¡rvore local explicitamente informada.

Baseline vcpkg: `9eec002df1cd109f8f104cd966a763daf9751489`, release [2026.10.16](https://github.com/microsoft/vcpkg/releases/tag/2026.10.16). O nome do port do validador Ã© [json-schema-validator](https://vcpkg.io/en/package/json-schema-validator.html), implementaÃ§Ã£o nlohmann/json-schema-validator; nÃ£o `nlohmann-json-schema-validator`.

Features desktop/persistÃªncia nÃ£o sÃ£o habilitadas na compilaÃ§Ã£o do nÃºcleo. O aplicativo nÃ£o efetua downloads nem acessa rede ao executar. Downloads sÃ£o apenas uma etapa do ambiente de desenvolvimento.

## PrÃ³ximos incrementos, sem nova votaÃ§Ã£o por etapa

1. Documento de projeto: UUIDv4, DTOs versionados, Draft-07, validaÃ§Ã£o semÃ¢ntica e fixtures; SHA-256 de conteÃºdo; rejeitar migraÃ§Ã£o silenciosa.
2. Pacote local minizip-ng: limites de expansÃ£o, nomes normalizados, gravaÃ§Ã£o atÃ´mica, Ã­ndice de assets e migraÃ§Ãµes explÃ­citas.
3. Carregador hÃ­brido e workers de assets com cancelamento, deduplicaÃ§Ã£o e integraÃ§Ã£o do cache.
4. Shell Qt Widgets: abrir/salvar, comandos de execuÃ§Ã£o, painel de estado e ponte de snapshots para a UI.
5. Adaptador VTK encapsulado e importaÃ§Ã£o glTF 2.0; expandir o cenÃ¡rio industrial e o IR Ladder.

O prÃ³ximo incremento concreto Ã© persistÃªncia validada. Ele cria a fronteira entre projeto editÃ¡vel e nÃºcleo executÃ¡vel antes de investir em interface e 3D. Funcionalidades educacionais ficam fora do escopo.

## Formato de projeto 1.0

O aplicativo 0.2.0 usa formato major/minor 1.0, revisão de modelo 1 e engine discrete-v1. A especificação completa, limites numéricos e decisões de integridade estão em [project-format.md](project-format.md). A semântica de scan do núcleo não foi alterada.
