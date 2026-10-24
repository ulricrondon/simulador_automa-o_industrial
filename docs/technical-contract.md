# Contrato técnico — Etapa 3.38

Decisão aprovada: **C — cache orientado a orçamento + LRU + custo de reconstrução**.

Este documento consolida RT-351–360. O histórico acessível contém as escolhas anteriores, mas não o texto integral de RT-001–350; esses requisitos não foram recriados nem renumerados. Os detalhes abaixo são decisões de implementação tomadas sob a autonomia concedida pelo usuário.

| ID | Requisito consolidado | Aplicação no incremento 0.1 |
|---|---|---|
| RT-351 | O cache deve ter orçamento configurável em bytes de payload CPU retido e limite separado de metadados. | `AssetCache(budget, max_entries, cold_window)`; bytes dos vetores, sem prometer limite de RSS. |
| RT-352 | A admissão e a redução de orçamento devem liberar retenções até respeitar o limite. | `put`, `get` e `set_budget` terminam com retenção ≤ orçamento; item maior que o orçamento fica apenas fracamente indexado. |
| RT-353 | A recência deve usar ordem monotônica de acessos, sem depender do relógio. | Contador interno sob mutex; compactação preserva ordem em caso de overflow. |
| RT-354 | O descarte deve combinar LRU com custo de reconstrução. | Entre os K retidos menos recentes, descartar menor custo; empates por recência e digest. K padrão = 4, configurável. K = 1 equivale a LRU. |
| RT-355 | O custo não pode impedir o respeito ao orçamento. | Custo é um inteiro estimado pelo carregador; não há classe de asset imune a descarte. |
| RT-356 | Payloads compartilhados devem ser imutáveis e a perda da retenção do cache não pode invalidar consumidores. | `shared_ptr<const AssetData>` e índice `weak_ptr`; cache constrói o vetor const internamente. |
| RT-357 | A identidade de conteúdo deve usar SHA-256 normalizado, independente de nomes e UUIDs de entidades. | Cache exige 64 dígitos hexadecimais minúsculos e rejeita conteúdos conflitantes ainda indexados. Cálculo e verificação criptográfica pelo carregador estão pendentes. |
| RT-358 | Publicação e consulta concorrentes devem preservar contabilidade e integridade; resultados de workers não podem alterar o estado de simulação fora da ordem do escalonador. | Cache protegido por mutex, resultados de simulação const; futura fila de assets separada. O núcleo atual não lê cache. |
| RT-359 | Propriedade VTK/GPU deve permanecer encapsulada na camada de apresentação e fora do cache de dados funcionais. | Nenhuma dependência VTK no núcleo/runtime; destruição no contexto correto será obrigação do adaptador desktop. Adaptador pendente. |
| RT-360 | O cache deve ser observável e testável: bytes retidos, entradas, acertos, falhas e descartes, com testes de orçamento, recência, custo, referências e concorrência. | `CacheStats` e suíte GoogleTest. Nenhuma telemetria remota. |

## Precisões operacionais

O orçamento cobre o tamanho lógico dos payloads retidos pelo cache. Capacidade de alocação de vetores, nós do mapa, alocador, buffers em trânsito, consumidores e futuras cópias GPU não entram nessa conta. Portanto, **não é um teto de memória total do processo**. O limite de metadados evita crescimento ilimitado do índice.

Ao atingir o limite de metadados, o cache remove expirados; se necessário, esquece a entrada não retida mais antiga, ou a retida mais antiga. Consumidores continuam válidos, mas uma entrada esquecida deixa de permitir deduplicação até ser novamente carregada. Preservar todo objeto externo exigiria um índice ilimitado; o MVP prefere limite explícito.

Um hit fraco pode readmitir o payload e provocar descarte de outra retenção. Custo de reconstrução é uma estimativa estável fornecida pelo carregador; medição de tempo real nunca participa do estado determinístico. Os testes usam chaves sintéticas válidas para testar a política, não como demonstração de hashing.

Budget zero desativa retenção forte, inclusive de payloads vazios. O valor inicial em produção será escolhido após medir assets reais; não há um número universal de memória assumido.
