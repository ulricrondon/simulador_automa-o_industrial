# Plataforma híbrida de simulação industrial

MVP incremental proprietário, offline-first, para Windows x64. Versão 0.2.0: núcleo determinístico e documentos de projeto validados. A interface desktop ainda não está implementada.

## Implementado

- C++20, CMake, presets MSVC Debug/Release e manifesto vcpkg fixado.
- Núcleo funcional independente de Qt, VTK, arquivos, rede e relógio de parede.
- IR Ladder mínimo: contatos NA/NF, AND/OR e bobinas com atualização simultânea.
- Eixo linear limitado, snapshots e escalonador dedicado com `std::jthread`.
- Fila limitada, cancelamento, resultados imutáveis e cache CPU com orçamento + LRU + custo.
- Documento JSON 1.0, modelo 1 e engine `discrete-v1`; schema Draft-07 embutido.
- UUIDv4 para projeto, eixo, rungs e assets; geração aleatória pelo Windows CNG.
- SHA-256 pelo Windows CNG e verificação de tamanho/conteúdo dos buffers de assets.
- Leitura de arquivos, serialização, validação estrutural/semântica e execução do cenário persistido.
- GoogleTest/CTest, exemplo reproduzível e comandos de console.

## Compilar no Windows x64

Pré-requisitos: Visual Studio Build Tools com C++20 e Windows SDK, CMake ≥ 3.25, Ninja, Git e vcpkg. Abra **x64 Native Tools** e configure `VCPKG_ROOT` para um checkout que contenha o baseline do manifesto.

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

Troque `debug` por `release` para a segunda configuração. A primeira resolução pode baixar dependências. O executável não acessa rede; schema e regras estão incorporados. Builds offline exigem dependências e caches previamente disponíveis.

As features `tests;project` fornecem GoogleTest, nlohmann/json e json-schema-validator. Qt/VTK e ZIP não são necessários neste incremento.

## Usar o projeto de exemplo

Na raiz do repositório:

```powershell
./build/windows-msvc-debug/sim_project.exe validate examples/contactor-axis.json
./build/windows-msvc-debug/sim_project.exe run examples/contactor-axis.json
```

O exemplo energiza uma bobina no tick 1, move o eixo até 25 µm e desenergiza no tick 4. Posições esperadas: `0,10,20,25,25,25,25,25`.

Para gerar um novo documento com identidades próprias, `new` escreve JSON UTF-8 na saída padrão. No PowerShell 7:

```powershell
./build/windows-msvc-debug/sim_project.exe new | Set-Content -Encoding utf8 meu-projeto.json
./build/windows-msvc-debug/sim_project.exe validate meu-projeto.json
./build/windows-msvc-debug/sim_project.exe run meu-projeto.json
```

A redireção pode substituir o arquivo indicado. Ainda não existe salvamento transacional nem pacote ZIP. `validate` confere o documento e os metadados do catálogo; não abre nem valida o conteúdo glTF de arquivos externos. A função `verify_asset` verifica buffers fornecidos pelo futuro carregador.

## Compilar com dependências locais

Para outra ferramenta C++20 no Windows e fontes previamente obtidas:

```powershell
cmake --preset headless-local -DSIM_GTEST_SOURCE_DIR=C:/deps/googletest -DSIM_JSON_SOURCE_DIR=C:/deps/json -DSIM_JSON_SCHEMA_SOURCE_DIR=C:/deps/json-schema-validator
cmake --build --preset headless-local
ctest --preset headless-local
```

Sem o módulo de projeto, use `-DSIM_BUILD_PROJECT=OFF`; essa configuração mantém núcleo/runtime portáteis e dispensa JSON e Windows CNG. Nenhuma rota baixa dependências silenciosamente durante o build local.

## Mapa

| Área | Responsabilidade |
|---|---|
| `include/sim/core`, `src/core` | Valores, IR, passos determinísticos |
| `include/sim/runtime`, `src/runtime` | Thread dedicada, fila e cache CPU |
| `include/sim/project`, `src/project` | Documentos, validação, identidade e integridade |
| `schemas`, `examples` | Schema versionado e projeto de demonstração |
| `apps/headless`, `apps/project` | Demonstração original e CLI de documentos |
| `tests` | Comportamento, replay, limites, integridade e CLI |
| `docs` | Contratos, decisões e evidências de validação |

Leia [formato de projeto](docs/project-format.md), [contrato técnico](docs/technical-contract.md), [arquitetura](docs/architecture.md) e [verificação 0.2](docs/validation-0.2.md).

## Escopo e próximos incrementos

A lógica elétrica é booleana e a mecânica é cinemática simplificada. O IR não implementa toda a IEC 61131-3. Ainda não existem editor, temporizadores, pacote ZIP, migração de formatos antigos, importador glTF ou visualização 3D.

A próxima etapa é pacote local minizip-ng, limites de expansão, gravação atômica e catálogo integrado ao carregador/cache. Depois virão os workers de assets, shell Qt Widgets e adaptador VTK. Funcionalidades educacionais permanecem fora do escopo.

O projeto é proprietário; dependências conservam suas licenças. A distribuição desktop e os avisos de terceiros serão preparados antes de empacotar Qt/VTK.
