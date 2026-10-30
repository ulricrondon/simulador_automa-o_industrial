# Correcoes preventivas MSVC — 2026-10-30

## Achados e correcoes

- Confirmado no codigo: project-1.4.schema.json tem 18.238 bytes e era substituido integralmente em um unico raw string literal. A geracao agora le os bytes em hexadecimal e emite literais adjacentes de ate 1.024 bytes (4.096 caracteres de escapes), concatenados pelo compilador. Aplicado aos cinco schemas, preservando inclusive quebras de linha e UTF-8. Os arquivos JSON originais nao foram alterados. O risco C2026 foi tratado preventivamente, nao reproduzido com MSVC neste ambiente.
- Confirmado em check_semantics: a referencia local ids ocultava o conjunto ids externo, com potencial C4456 sob /W4 /WX. A referencia foi renomeada para conveyor_ids e seus cinco usos atualizados. Registro e rejeicao de UUIDs continuam identicos; nenhuma flag de warning foi relaxada.

## Arquivos

Modificados: CMakeLists.txt; src/project/schema.hpp.in; src/project/document.cpp.
Criados: cmake/EmbedSchema.cmake; tests/schema_embedding_test.cpp; docs/msvc-compatibility-fixes.md.

O novo teste SchemaEmbedding.AllVersionsPreserveOriginalBytes compara os bytes compilados dos schemas 1.0 a 1.4 com os respectivos arquivos lidos em modo binario. A suite aumentou de 165 para 166 testes. Nenhum modulo de replay, cache ou execucao concorrente foi modificado.

## Validacao efetiva

GCC 16.2.0, Windows x64, CMake/Ninja. Comandos executados com as ferramentas e dependencias locais existentes:

```powershell
cmake --build build/conveyor-debug -j 4
ctest --test-dir build/conveyor-debug --output-on-failure --output-junit msvc-fixes-results.xml
cmake --build build/conveyor-release -j 4
ctest --test-dir build/conveyor-release --output-on-failure --output-junit msvc-fixes-results.xml
```

| Configuracao | Build | CTest | Tempo dos testes |
| --- | --- | --- | --- |
| GCC Debug | codigo 0 | 166/166; codigo 0 | 12,21 s |
| GCC Release | codigo 0 | 166/166; codigo 0 | 5,69 s |
| MSVC Debug/Release | Nao executado | Pendente | — |

O console Debug recompilado iniciou normalmente nesta rodada, inclusive nos dez testes antes impedidos. Nenhuma politica foi alterada e nao se atribui a estas correcoes a resolucao de uma regra especifica de seguranca. Os registros anteriores de bloqueio permanecem validos historicamente.

Evidencias: build/msvc-fixes-debug-build.txt, build/msvc-fixes-debug-tests.txt, build/msvc-fixes-release-build.txt, build/msvc-fixes-release-tests.txt e msvc-fixes-results.xml em cada diretorio de build.

## Pendencias

cl nao foi encontrado no PATH, vswhere nao existe no local padrao consultado, os diretorios padrao Visual Studio e as variaveis VSINSTALLDIR/VCINSTALLDIR/VCToolsInstallDir nao indicaram instalacao utilizavel. Uma instalacao personalizada nao detectada nao pode ser descartada. Nada foi instalado. Os presets windows-msvc-debug e windows-msvc-release permanecem inalterados e devem ser executados quando o ambiente oficial estiver disponivel. Nao se declara compatibilidade MSVC sem compilacao efetiva.

Nenhum Git, ZIP, instalador, Qt/VTK ou nova funcionalidade industrial foi utilizado ou implementado.
