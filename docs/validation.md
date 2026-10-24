# VerificaÃ§Ã£o do incremento 0.1

Data: 24/10/2026. Ambiente: Windows x64.

## Resultado observado

| ConfiguraÃ§Ã£o | CompilaÃ§Ã£o | CTest |
|---|---|---|
| Debug, GCC portÃ¡til | Aprovada, avisos prÃ³prios tratados como erros | 24/24 aprovados; 2,60 s |
| Release, GCC portÃ¡til | Aprovada, avisos prÃ³prios tratados como erros | 24/24 aprovados; 2,85 s |
| MSVC + vcpkg | NÃ£o executada: ferramentas ausentes | Pendente |

SÃ£o os mesmos 23 casos GoogleTest e uma execuÃ§Ã£o da demonstraÃ§Ã£o em cada configuraÃ§Ã£o, nÃ£o 48 casos diferentes.

Ferramentas realmente executadas: GCC 16.2.0, CMake 4.4.2, Ninja 1.13.2 e GoogleTest 1.18.0. O GoogleTest usado coincide com a versÃ£o do port no baseline fixado.

Cobertura exercitada:
- HistÃ³rico esperado de entradas, bobina, tempo e posiÃ§Ã£o.
- Replay com 100 permutaÃ§Ãµes da ordem de entrada dos eventos, comparando todos os snapshots.
- Prioridade de eventos no mesmo tick e independÃªncia da ordem dos rungs.
- Contatos NA/NF, AND/OR, validaÃ§Ã£o de limites e prevenÃ§Ã£o de overflow.
- Cancelamento, fila limitada, encerramento com futuros resolvidos e execuÃ§Ã£o dedicada.
- Cache: orÃ§amento, custo/LRU, referÃªncias externas, recuperaÃ§Ã£o fraca, limite de metadados e acessos concorrentes.

O executÃ¡vel produziu este resultado:

```csv
tick,time_us,input_0,output_0,position_um
0,1000,0,0,0
1,2000,1,1,10
2,3000,1,1,20
3,4000,1,1,25
4,5000,0,0,25
5,6000,0,0,25
6,7000,0,0,25
7,8000,0,0,25
```

## ReproduÃ§Ã£o

O build local foi feito fora da Ã¡rvore de fontes, com a ferramenta portÃ¡til adicionada somente ao PATH daquele processo:

```powershell
cmake -S <projeto> -B <build-debug> -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++ -DSIM_GTEST_SOURCE_DIR=<googletest-1.18.0>
cmake --build <build-debug> -j 4
ctest --test-dir <build-debug> --output-on-failure --output-junit test-results.xml
```

Repetido em outro diretÃ³rio com `CMAKE_BUILD_TYPE=Release`. NÃ£o houve instalaÃ§Ã£o global. Os relatÃ³rios CTest originais e os executÃ¡veis de verificaÃ§Ã£o estÃ£o na pasta de trabalho da sessÃ£o. O resumo por caso estÃ¡ em [test-results.json](test-results.json).

## ProveniÃªncia e verificaÃ§Ãµes adicionais

- Ferramenta portÃ¡til: [w64devkit v2.9.1, x64](https://github.com/skeeto/w64devkit/releases/tag/v2.9.1). SHA-256 do pacote conferido contra o digest da API oficial: `9208c19755cd4964b7915b9afcf02c66d493a4c870c4b3e83f6c538d9c1237a5`.
- GoogleTest: [tag v1.18.0](https://github.com/google/googletest/releases/tag/v1.18.0).
- CatÃ¡logo: [vcpkg 2026.10.16](https://github.com/microsoft/vcpkg/releases/tag/2026.10.16). Nomes e features do manifesto foram conferidos nos arquivos oficiais desse commit. A resoluÃ§Ã£o/build vcpkg ainda nÃ£o foi executada.
- JSONs de configuraÃ§Ã£o lidos com sucesso; presets reconhecidos pelo CMake.
- CabeÃ§alhos funcionais nÃ£o incluem Qt/VTK; o build do nÃºcleo nÃ£o solicita esses pacotes.

## Limites da evidÃªncia

A configuraÃ§Ã£o MSVC Debug/Release e o workflow Windows estÃ£o preparados, mas nÃ£o foram executados. O workflow nÃ£o foi enviado a nenhum serviÃ§o. NÃ£o hÃ¡ certificaÃ§Ã£o da interface Qt/VTK, persistÃªncia, SHA-256, UUIDv4 ou migraÃ§Ãµes: esses componentes estÃ£o planejados, nÃ£o implementados. Os testes concorrentes passaram, mas nÃ£o equivalem a uma anÃ¡lise com ThreadSanitizer.

O repositÃ³rio Git foi inicializado localmente, sem commit ou publicaÃ§Ã£o externa. O ZIP de entrega contÃ©m somente fontes e documentaÃ§Ã£o, nÃ£o as ferramentas portÃ¡teis nem dependÃªncias.
