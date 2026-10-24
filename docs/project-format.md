# Documento de projeto — formato 1.0

O módulo `sim_project_document` converte JSON em valores de `sim_core`. Windows CNG, JSON e validação permanecem fora do núcleo. O documento guarda identidades e configuração; snapshots de execução não fazem parte deste formato.

## Envelope e identidades

O arquivo contém `format: industrial-sim-project`, `format_version: {major: 1, minor: 0}`, `model_version: 1` e `engine: discrete-v1`. Somente essa combinação é aceita. Qualquer outra versão requer migração explícita; nenhum conversor de legado foi implementado ainda.

O versionamento é híbrido: major/minor do documento, revisão inteira do modelo e identificador da semântica do engine. A versão do aplicativo, atualmente 0.2.0, é independente dessas versões.

Projeto, eixo, rungs e entradas de assets possuem UUIDv4 canônico em minúsculas, com variante RFC e versão verificadas. UUIDs não podem se repetir entre entidades do mesmo documento. Sinais continuam índices locais do IR, não entidades de catálogo. Um novo projeto gera IDs; carregar/serializar preserva os existentes.

A geração usa [BCryptGenRandom](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptgenrandom) do Windows. O RNG não é chamado durante a simulação nem ao restaurar um documento.

## Validação em camadas

1. Limite de 16 MiB de JSON UTF-8; leitura em blocos com limite, sem confiar apenas no tamanho informado pelo sistema de arquivos.
2. Parser estrito: profundidade máxima 64, rejeição de chaves duplicadas inclusive quando escapadas, UTF-8 inválido, tokens extras e números com representação de ponto flutuante.
3. Envelope de versões compatível.
4. Domínio numérico explícito, seguido do schema Draft-07 incorporado no binário.
5. Conversão com checagem de faixa, unicidade de identidades, consistência do catálogo e regras semânticas do núcleo.

Não há download de schemas, resolução de referências externas nem aceitação de um schema fornecido no documento. Campos desconhecidos são rejeitados, em todos os objetos. A serialização também valida o documento em memória.

O schema é o arquivo `schemas/project-1.0.schema.json`, incorporado pelo CMake. Alterá-lo dispara a regeneração do cabeçalho; não há cópia manual de regras em um segundo schema.

## Números e limites

Todos os números persistidos são inteiros exatos não negativos, até **9.223.372.036.854.775.807 (INT64_MAX)**, com limites menores quando aplicáveis. Não se aceitam `1.0` ou `1e3`, mesmo quando matematicamente inteiros. O parser não usa ponto flutuante para conversões.

Essa faixa evita um comportamento observado no json-schema-validator 2.4.0: suas restrições de inteiros usam int64 e podem interpretar uint64 acima dessa faixa como negativos. Uma verificação própria rejeita o valor antes da validação; o núcleo pode continuar usando uint64 internamente. Testes cobrem a fronteira exata e números acima dela.

Os limites do núcleo permanecem: 100.000 ticks/eventos, 256 entradas/saídas e 4.096 contatos. Duração total e soma do avanço do eixo têm verificações próprias de overflow. O título aceita de 1 a 160 caracteres Unicode, com teto adicional de 640 bytes UTF-8.

## Catálogo e SHA-256

Cada asset possui UUID, digest SHA-256 de 64 hexadecimais minúsculos, tamanho em bytes e media type `model/gltf+json` ou `model/gltf-binary`. Limites iniciais: 4.096 registros e 256 MiB por buffer. Dois UUIDs podem referenciar o mesmo conteúdo; registros com o mesmo digest devem concordar em tamanho e media type.

`verify_asset(record, bytes)` verifica tamanho e SHA-256 antes da futura admissão no cache. O hash é calculado sobre os bytes originais, usando [BCryptHash](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcrypthash), disponível no Windows 10 ou posterior. Há testes para conteúdo vazio, texto, um milhão de bytes e dados binários.

Verificação criptográfica não valida a estrutura glTF nem faz autenticação de origem. Catálogo, carregamento de arquivos, resolução de URIs glTF e cache serão integrados no próximo incremento. O comando `validate` atual valida apenas o documento e seus metadados.

## Estabilidade e erros

Salvar e carregar preserva IDs, ordem das listas, valores e resultado do replay. A serialização produz JSON legível e estável para este formato; não é uma implementação de JSON Canonicalization Scheme e não é usada como identidade de assets.

Erros possuem categoria: JSON inválido, schema, versão incompatível, semântica, integridade, I/O ou limite. Falhas de conteúdo não produzem uma simulação parcialmente válida.

O CLI recebe caminhos Windows Unicode via `wmain`. O comando `new` escreve na saída padrão; criação atômica, substituição segura, ZIP, migração e recuperação de falhas de disco ainda não estão implementados.
