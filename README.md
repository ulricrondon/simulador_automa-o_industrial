# Plataforma híbrida de simulação industrial

Primeiro incremento real do MVP, criado em 24/10/2026. Aplicação proprietária para Windows x64, offline-first. Este incremento entrega um executável de console; a interface desktop ainda não está implementada.

## O que já existe

- Monorepo CMake/C++20, presets MSVC Debug/Release e manifesto vcpkg com baseline fixo.
- Núcleo funcional sem Qt, VTK, arquivos, rede ou relógio de parede.
- IR Ladder mínimo: contatos NA/NF, séries AND, ramos paralelos OR e bobinas.
- Passos determinísticos: entradas → Ladder → saídas → eixo linear limitado → snapshot.
- Escalonador exclusivo com `std::jthread`, fila limitada, cancelamento e resultados `shared_ptr<const RunResult>`.
- Cache CPU com orçamento, janela LRU, custo de reconstrução, retenção forte limitada e referências fracas.
- Testes GoogleTest/CTest e cenário de demonstração com saída CSV.

O exemplo energiza uma bobina no tick 1, move o eixo até 25 µm e desenergiza no tick 4. Posições esperadas: `0,10,20,25,25,25,25,25`.

## Compilar no Windows x64 com MSVC

Pré-requisitos: Visual Studio Build Tools com C++20 e Windows SDK, CMake ≥ 3.25, Ninja, Git e vcpkg. Abra um terminal **x64 Native Tools** e aponte `VCPKG_ROOT` para um checkout vcpkg que contenha o baseline do manifesto. Não é necessário instalar Qt/VTK para este incremento.

Na raiz deste projeto:

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
./build/windows-msvc-debug/sim_headless.exe
```

Troque `debug` por `release` para a segunda configuração. A primeira resolução vcpkg pode baixar dependências; a execução do simulador não utiliza rede. Para builds sem rede, pré-carregue fontes, pacotes e cache binário. O baseline fixo congela o catálogo, não transforma uma máquina nova em ambiente offline.

## Verificação com outra ferramenta C++20

Para uma ferramenta já disponível e GoogleTest previamente obtido:

```powershell
cmake --preset headless-local -DSIM_GTEST_SOURCE_DIR=C:/deps/googletest
cmake --build --preset headless-local
ctest --preset headless-local
```

Essa rota não usa FetchContent nem baixa dependências silenciosamente. Ela verifica o núcleo portátil e não certifica a cadeia MSVC/vcpkg.

## Mapa

| Diretório | Responsabilidade |
|---|---|
| `include/sim/core`, `src/core` | Contratos por valor, IR e evolução determinística |
| `include/sim/runtime`, `src/runtime` | Thread dedicada, fila, cache CPU |
| `apps/headless` | Executável de demonstração |
| `tests` | Comportamento, replay, concorrência e limites |
| `docs` | RT-351–360, decisões, estado e próximos incrementos |

Veja [contrato técnico](docs/technical-contract.md), [decisões](docs/architecture.md) e [verificação](docs/validation.md).

## Limites deste incremento

A lógica elétrica é booleana; não resolve circuitos analógicos, correntes ou tensões. A mecânica é um eixo cinemático unidirecional em micrômetros inteiros; não há dinâmica de corpos rígidos. O IR não pretende implementar toda a IEC 61131-3. Não há editor, temporizadores, persistência, importação glTF ou visualização 3D ainda.

Qt Widgets, VTK, minizip-ng, nlohmann/json e Draft-07 permanecem decisões aprovadas. As features `desktop` e `persistence` do manifesto apenas reservam dependências; não criam módulos funcionais. UUIDv4, cálculo/verificação SHA-256 e migrações explícitas serão implementados na camada de projeto/persistência. Nenhum recurso educacional está incluído.

O código do projeto é proprietário. Dependências conservam suas próprias licenças; a cadeia de distribuição Qt e os avisos de terceiros serão definidos antes de empacotar o desktop.
