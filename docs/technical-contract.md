# Contrato tÃ©cnico â€” Etapa 3.38

DecisÃ£o aprovada: **C â€” cache orientado a orÃ§amento + LRU + custo de reconstruÃ§Ã£o**.

Este documento consolida RT-351â€“360. O histÃ³rico acessÃ­vel contÃ©m as escolhas anteriores, mas nÃ£o o texto integral de RT-001â€“350; esses requisitos nÃ£o foram recriados nem renumerados. Os detalhes abaixo sÃ£o decisÃµes de implementaÃ§Ã£o tomadas sob a autonomia concedida pelo usuÃ¡rio.

| ID | Requisito consolidado | AplicaÃ§Ã£o no incremento 0.1 |
|---|---|---|
| RT-351 | O cache deve ter orÃ§amento configurÃ¡vel em bytes de payload CPU retido e limite separado de metadados. | `AssetCache(budget, max_entries, cold_window)`; bytes dos vetores, sem prometer limite de RSS. |
| RT-352 | A admissÃ£o e a reduÃ§Ã£o de orÃ§amento devem liberar retenÃ§Ãµes atÃ© respeitar o limite. | `put`, `get` e `set_budget` terminam com retenÃ§Ã£o â‰¤ orÃ§amento; item maior que o orÃ§amento fica apenas fracamente indexado. |
| RT-353 | A recÃªncia deve usar ordem monotÃ´nica de acessos, sem depender do relÃ³gio. | Contador interno sob mutex; compactaÃ§Ã£o preserva ordem em caso de overflow. |
| RT-354 | O descarte deve combinar LRU com custo de reconstruÃ§Ã£o. | Entre os K retidos menos recentes, descartar menor custo; empates por recÃªncia e digest. K padrÃ£o = 4, configurÃ¡vel. K = 1 equivale a LRU. |
| RT-355 | O custo nÃ£o pode impedir o respeito ao orÃ§amento. | Custo Ã© um inteiro estimado pelo carregador; nÃ£o hÃ¡ classe de asset imune a descarte. |
| RT-356 | Payloads compartilhados devem ser imutÃ¡veis e a perda da retenÃ§Ã£o do cache nÃ£o pode invalidar consumidores. | `shared_ptr<const AssetData>` e Ã­ndice `weak_ptr`; cache constrÃ³i o vetor const internamente. |
| RT-357 | A identidade de conteÃºdo deve usar SHA-256 normalizado, independente de nomes e UUIDs de entidades. | Cache exige 64 dÃ­gitos hexadecimais minÃºsculos e rejeita conteÃºdos conflitantes ainda indexados. CÃ¡lculo e verificaÃ§Ã£o criptogrÃ¡fica pelo carregador estÃ£o pendentes. |
| RT-358 | PublicaÃ§Ã£o e consulta concorrentes devem preservar contabilidade e integridade; resultados de workers nÃ£o podem alterar o estado de simulaÃ§Ã£o fora da ordem do escalonador. | Cache protegido por mutex, resultados de simulaÃ§Ã£o const; futura fila de assets separada. O nÃºcleo atual nÃ£o lÃª cache. |
| RT-359 | Propriedade VTK/GPU deve permanecer encapsulada na camada de apresentaÃ§Ã£o e fora do cache de dados funcionais. | Nenhuma dependÃªncia VTK no nÃºcleo/runtime; destruiÃ§Ã£o no contexto correto serÃ¡ obrigaÃ§Ã£o do adaptador desktop. Adaptador pendente. |
| RT-360 | O cache deve ser observÃ¡vel e testÃ¡vel: bytes retidos, entradas, acertos, falhas e descartes, com testes de orÃ§amento, recÃªncia, custo, referÃªncias e concorrÃªncia. | `CacheStats` e suÃ­te GoogleTest. Nenhuma telemetria remota. |

## PrecisÃµes operacionais

O orÃ§amento cobre o tamanho lÃ³gico dos payloads retidos pelo cache. Capacidade de alocaÃ§Ã£o de vetores, nÃ³s do mapa, alocador, buffers em trÃ¢nsito, consumidores e futuras cÃ³pias GPU nÃ£o entram nessa conta. Portanto, **nÃ£o Ã© um teto de memÃ³ria total do processo**. O limite de metadados evita crescimento ilimitado do Ã­ndice.

Ao atingir o limite de metadados, o cache remove expirados; se necessÃ¡rio, esquece a entrada nÃ£o retida mais antiga, ou a retida mais antiga. Consumidores continuam vÃ¡lidos, mas uma entrada esquecida deixa de permitir deduplicaÃ§Ã£o atÃ© ser novamente carregada. Preservar todo objeto externo exigiria um Ã­ndice ilimitado; o MVP prefere limite explÃ­cito.

Um hit fraco pode readmitir o payload e provocar descarte de outra retenÃ§Ã£o. Custo de reconstruÃ§Ã£o Ã© uma estimativa estÃ¡vel fornecida pelo carregador; mediÃ§Ã£o de tempo real nunca participa do estado determinÃ­stico. Os testes usam chaves sintÃ©ticas vÃ¡lidas para testar a polÃ­tica, nÃ£o como demonstraÃ§Ã£o de hashing.

Budget zero desativa retenÃ§Ã£o forte, inclusive de payloads vazios. O valor inicial em produÃ§Ã£o serÃ¡ escolhido apÃ³s medir assets reais; nÃ£o hÃ¡ um nÃºmero universal de memÃ³ria assumido.


## Incremento 0.2 — documentos e integridade

Decisões adicionais tomadas sob a autonomia concedida, sem modificar a semântica do núcleo:

| ID | Requisito | Implementação |
|---|---|---|
| RT-361 | Separar versões de documento, modelo e engine; rejeitar versões desconhecidas. | Formato 1.0, modelo 1, discrete-v1; erro exige migração explícita. Conversores de legado pendentes. |
| RT-362 | Identidades persistentes UUIDv4 canônicas e únicas entre entidades. | Projeto, eixo, rungs e assets; geração Windows CNG somente na criação. |
| RT-363 | Validar estrutura por Draft-07 local e imutável em execução. | Schema embutido pelo CMake; resolução externa desabilitada. |
| RT-364 | Preservar números inteiros exatos sem conversões silenciosas. | Faixa persistida não negativa até INT64_MAX, com limites específicos menores e conversão checada. |
| RT-365 | Limitar a leitura de documentos não confiáveis. | 16 MiB, profundidade 64, UTF-8 estrito, rejeição de chaves duplicadas e campos desconhecidos. |
| RT-366 | Serialização e leitura preservam identidades e resultado da simulação. | Round-trip com comparação do histórico completo; sem recriar UUID ao carregar. |
| RT-367 | Verificar tamanho e SHA-256 dos buffers de assets. | Windows BCryptHash, vetores conhecidos e rejeição de conteúdo alterado. Integração com carregador/cache pendente. |
| RT-368 | Isolar JSON, schema, arquivos e APIs de plataforma do núcleo funcional. | Alvo sim_project_document; sim_core continua somente C++ padrão. |
| RT-369 | Distinguir falhas de sintaxe, schema, versão, semântica, integridade, I/O e limites. | ErrorCode e falha antes de executar documento inválido. |
| RT-370 | Manter evidência automatizada de identidade, integridade, limites e replay persistido. | GoogleTest/CTest e CLI que gera, valida e executa JSON, incluindo caminho Unicode. |

Atualização de RT-357: cálculo/verificação SHA-256 de buffers está implementado; a verificação antes de admissão no cache será ligada ao carregador no próximo incremento. RT-359 permanece uma obrigação futura do adaptador VTK.
