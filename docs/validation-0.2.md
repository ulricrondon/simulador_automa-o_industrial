# Verificação do incremento 0.2

Data: 24/10/2026. Windows x64.

## Resultados locais

| Configuração | Compilação | CTest |
|---|---|---|
| GCC Debug | Aprovada, avisos próprios tratados como erros | 51/51 aprovados; 1,01 s |
| GCC Release | Aprovada, avisos próprios tratados como erros | 51/51 aprovados; 0,89 s |
| Núcleo sem projeto/JSON/CNG | Build separado aprovado | Demonstração executada com histórico esperado |

São 47 casos GoogleTest e quatro verificações de executáveis/CLI por configuração, totalizando 51 casos distintos. O relatório por caso está em [test-results-0.2.json](test-results-0.2.json).

Ferramentas locais: GCC 16.2.0, CMake 4.4.2, Ninja 1.13.2, GoogleTest 1.18.0. Dependências: nlohmann/json 3.12.0 e json-schema-validator 2.4.0, correspondentes ao baseline vcpkg já fixado.

## Evidência funcional

- Documento salvo e carregado preserva UUIDs e histórico completo da simulação.
- Schema Draft-07 incorporado; referências externas desabilitadas.
- Rejeição de versões incompatíveis, campos desconhecidos, chaves duplicadas, UTF-8 inválido, números imprecisos e limites excedidos.
- Fronteira INT64_MAX coberta: valor permitido preservado exatamente, valores maiores rejeitados antes do validador.
- UUIDv4: versão, variante, formato e geração de 1.000 valores distintos no teste.
- SHA-256: vetores vazio, abc, um milhão de caracteres e buffer binário; alterações de conteúdo/tamanho são rejeitadas.
- Carregamento de arquivo com nome Unicode coberto em GoogleTest. CLI validado adicionalmente com caminho contendo acentos e execução completa.
- CLI: gerar JSON, validar, executar e rejeitar documento/comando inválidos.
- Testes originais de determinismo, fila, cancelamento e cache continuam aprovados.

O CTest da ferramenta portátil falhou ao iniciar seu próprio CMake quando um argumento do driver continha acentos. O teste de round-trip mantém argumento ASCII; o suporte Unicode da biblioteca foi testado em C++ e o CLI foi invocado diretamente com caminho Unicode, ambos com sucesso. Essa limitação do driver não foi confundida com sucesso de execução do teste.

## Reprodução local

```powershell
cmake -S <projeto> -B <build> -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++ -DSIM_GTEST_SOURCE_DIR=<googletest> -DSIM_JSON_SOURCE_DIR=<json> -DSIM_JSON_SCHEMA_SOURCE_DIR=<validator>
cmake --build <build> -j 4
ctest --test-dir <build> --output-on-failure --output-junit test-results.xml
```

Repetido em outro diretório com Release. Para a verificação de isolamento: `SIM_BUILD_PROJECT=OFF` e `BUILD_TESTING=OFF`.

## Windows/MSVC e limites

A base 0.1 foi aprovada em MSVC Debug e Release no [GitHub Actions](https://github.com/ulricrondon/simulador_automa-o_industrial/actions/runs/40120196285). A validação MSVC deste novo incremento é realizada pelo workflow após a publicação; o resultado deve ser consultado na execução correspondente ao commit, sem extrapolar o resultado da base.

O incremento ainda não oferece pacote ZIP, gravação transacional, migração de legado, carregamento glTF ou integração dos buffers verificados ao cache. O comando validate confere o documento e o catálogo, não arquivos externos de assets.
