# Integração na pasta atual — 27/10/2026

Diretório do código: C:\Projetos\simulador_industrial\simulador_automa-o_industrial.

As entregas da esteira e do TON foram integradas da cópia antiga. Foram selecionados 29 arquivos de código, testes, schemas, exemplos e documentação. Os arquivos existentes substituídos foram preservados, sem compactação, em build/integration-before. Outros 28 arquivos preexistentes foram comparados por SHA-256 e permaneceram idênticos. Nenhum comando Git foi executado.

## Validação neste diretório
- Debug: 97/97 aprovados, zero falhas.
- Release: 97/97 aprovados, zero falhas.
- Compilador: GCC 16.2.0 no Windows x64. MSVC não executado nesta integração.
- Builds novos, configurados nesta pasta; não foram copiados binários ou caches CMake da cópia anterior.
- Evidências: build/conveyor-debug/test-results.xml e build/conveyor-release/test-results.xml.
- Logs: build/integration-debug-configure.txt, integration-debug-build.txt, integration-debug-tests.txt e equivalentes release.

Dependências e ferramentas previamente disponíveis foram lidas em:
C:/Users/ulric/Documents/Codex/2026-10-24/referenced-chatgpt-conversation-this-is-an/work/tools

Configuração CMake: -S . -B build/conveyor-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++, com SIM_GTEST_SOURCE_DIR, SIM_JSON_SOURCE_DIR e SIM_JSON_SCHEMA_SOURCE_DIR apontando para googletest-1.18.0, json-3.12.0 e json-schema-validator-2.4.0 nessa pasta. Release usa build/conveyor-release e CMAKE_BUILD_TYPE=Release.

Compilação: cmake --build build/conveyor-debug -j 4 e equivalente release.
Testes: ctest --test-dir build/conveyor-debug --output-on-failure --output-junit test-results.xml e equivalente release.

## Uso
A partir de C:\Projetos\simulador_industrial:
~~~powershell
cd .\simulador_automa-o_industrial
.\build\conveyor-debug\sim_project.exe run examples/conveyor.json
.\build\conveyor-debug\sim_project.exe run examples/conveyor-ton.json
~~~

Este é o diretório de implementação adotado a partir desta correção. Os relatórios funcionais completos estão em docs/conveyor.md e docs/ton.md.
