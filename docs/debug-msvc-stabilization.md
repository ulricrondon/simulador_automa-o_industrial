# Estabilizacao Debug e preparacao MSVC — 2026-10-30

## Conclusao

O bloqueio imediato do console Debug foi confirmado pelo Windows: erro nativo 4551, `Uma política de Controle de Aplicativo bloqueou este arquivo.` O processo nao iniciou; nao existe codigo de saida do aplicativo nesse caso. A evidencia nao permite atribuir o bloqueio especificamente a Smart App Control, AppLocker ou outra regra de Code Integrity.

Os registros CTest apontam para o executavel correto, existente, da configuracao Debug. Nao foi identificada incorrecao de caminhos ou registro que justifique alterar o projeto. Nenhum codigo funcional, teste, preset, JSON ou politica foi alterado nesta etapa. Nao houve recompilacao, instalacao, Git, ZIP ou tentativa de evasao.

## Evidencias e classificacao

`build/stabilization-launch.json` registra a tentativa direta via Process.Start, incluindo caminho, SHA-256, excecao Win32, erro nativo, stdout e resultado. Debug nao iniciou; Release validou o mesmo JSON com codigo 0.

| Configuracao | Diretorio | Flags GCC | SHA-256 sim_project.exe |
| --- | --- | --- | --- |
| Debug | build/conveyor-debug | -g | 4A4DDCE979200AB9524E78FEF4D3EB1050552B61443C01DEA1AF8D8A2A3BC427 |
| Release | build/conveyor-release | -O3 -DNDEBUG | 3E6942B9EAEBCABE817244B9B757AEFED4BB3D439BE5B55090B02628A63BA4E9 |

Ambos usam a raiz C:/Projetos/simulador_industrial/simulador_automa-o_industrial, Ninja e GCC 16.2.0. Sao executaveis distintos em diretorios distintos. Nao foi demonstrado por que a politica aceita um e recusa o outro: tamanho, flags ou assinatura nao devem ser presumidos como causa.

A consulta somente de leitura aos canais `Microsoft-Windows-CodeIntegrity/Operational` e `Microsoft-Windows-AppLocker/EXE and DLL`, filtrada para os ultimos sete dias, foi recusada em ambos com `Attempted to perform an unauthorized operation.` Nenhuma elevacao foi solicitada. Nao foi possivel confirmar nem descartar eventos relacionados nesses canais.

### Os dez testes

Nos quatro testes diretos, CTest informa BAD_COMMAND / Not Run. Nos seis scripts, o CMake inicia, mas a primeira chamada ao console falha; o script devolve codigo 1 por FATAL_ERROR. Esses erros nao significam que a verificacao funcional do aplicativo foi executada. A tentativa direta comum ao executavel confirma o bloqueio de criacao de processo; nao uma assercao de simulacao.

| Teste | Primeira chamada ao console | Resultado CTest / mensagem do script |
| --- | --- | --- |
| conveyor_validate | validate examples/conveyor.json | BAD_COMMAND; sem saida do aplicativo |
| conveyor_run | run examples/conveyor.json | BAD_COMMAND; sem saida do aplicativo |
| conveyor_cli | new-conveyor (arquivo de teste) | Cannot create conveyor document |
| project_validate | validate examples/contactor-axis.json | BAD_COMMAND; sem saida do aplicativo |
| project_run | run examples/contactor-axis.json | BAD_COMMAND; sem saida do aplicativo |
| project_cli_roundtrip | new (arquivo de teste) | Cannot create project JSON |
| ton_cli | validate examples/conveyor-ton.json | TON profile validation failed: |
| ctu_cli | validate examples/ctu-pulses.json | CTU profile failed: |
| comparison_cli | validate examples/cv-compare-pulses.json | Comparison profile failed: |
| replay_cli | validate examples/reference-conveyor.json | Reference scenario validation failed: |

O comando completo de cada teste, com todos os argumentos absolutos, esta em `build/stabilization-ctest-commands.json` e na copia `build/stabilization-debug-registration.txt`. A reproducao individual dos dez comandos e seus codigos/mensagens exatos esta em `build/stabilization-console-results.json`. O historico anterior foi preservado em `build/stabilization-debug-original-LastTest.log`.

## Regressao executada nesta etapa

Ferramentas: GCC 16.2.0, CMake 4.4.2, Ninja 1.13.2; Windows x64. Foram utilizados os binarios existentes, sem recompilar. As ferramentas ja existentes foram lidas da instalacao local usada pelo projeto; todas as novas evidencias foram gravadas dentro da raiz autorizada.

~~~powershell
ctest --test-dir build/conveyor-debug --output-on-failure --output-junit stabilization-results.xml
ctest --test-dir build/conveyor-release --output-on-failure --output-junit stabilization-results.xml
~~~

| Configuracao | Aprovados | Impedidos/falhos | Codigo CTest |
| --- | --- | --- | --- |
| GCC Debug | 155/165 | 10 de console | 8 |
| GCC Release | 165/165 | 0 | 0 |
| MSVC Debug/Release | Nao executados | Ambiente nao encontrado | Nao aplicavel |

Release terminou em 4,87 segundos. Nenhum teste foi adicionado, removido ou enfraquecido. A suite preserva a cobertura dos cenarios fisico/pulsos, replay, Ladder/TON/CTU/comparadores, estados LOW/HIGH/UNDEFINED/FAULT, persistencia versionada, pause/resume/stop e independencia do tempo real. Os testes de nucleo e integracao passaram tambem em Debug; a camada de console Debug permanece sem aprovacao.

Logs atuais: `build/stabilization-debug-tests.txt`, `build/stabilization-release-tests.txt` e `build/conveyor-{debug,release}/stabilization-results.xml`.

## MSVC: estado real e preparacao pelo usuario

Nao foram encontrados cl no PATH, vswhere no local padrao do Visual Studio Installer, diretorios padrao Visual Studio em Program Files/Program Files (x86), Windows Kits/10/Include, chaves SxS/VS7 consultadas ou variaveis VSINSTALLDIR/VCINSTALLDIR/VCToolsInstallDir/WindowsSdkDir/VCPKG_ROOT. Isso demonstra ausencia de ambiente utilizavel nas localizacoes verificadas; nao prova inexistencia de uma instalacao personalizada fora delas. Nao houve tentativa de configuracao MSVC nem declaracao de aprovacao.

Para preparar o ambiente, o usuario pode:

1. Obter Visual Studio Build Tools pelo canal oficial e selecionar **Desktop development with C++**.
2. Confirmar **MSVC Build Tools x64/x86**, um **Windows SDK** suportado e **C++ CMake tools for Windows** (CMake/Ninja). O projeto exige CMake >= 3.25 e C++20.
3. Abrir o Developer PowerShell configurado para alvo x64 e verificar `cl`, `cmake --version` e `ninja --version`.
4. Disponibilizar uma instalacao vcpkg aprovada e definir VCPKG_ROOT para seu caminho real. O manifesto existente usa baseline fixo e os presets ativam somente tests/project (gtest, nlohmann-json e json-schema-validator); nao ativam desktop ou persistence. A resolucao de dependencias pode exigir downloads e processos internos do vcpkg; nao foi executada nesta tarefa. Se for exigida ausencia total de Git inclusive em ferramentas subordinadas, preparar previamente o cache/dependencias por um processo administrado pelo usuario.
5. Na raiz do projeto, executar os presets existentes, sem reutilizar caches GCC:

~~~powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug --output-junit stabilization-msvc-debug.xml
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
ctest --preset windows-msvc-release --output-junit stabilization-msvc-release.xml
~~~

Registrar a versao efetiva de cl, SDK, CMake, codigos e total de testes. Os presets usam diretorios build/windows-msvc-debug e build/windows-msvc-release, Ninja, cl, x64-windows e runtime DLL Debug/Release. Nenhuma mudanca de preset e necessaria com base na inspecao; sua execucao real ainda e pendente.

Fontes oficiais consultadas: [Instalacao MSVC](https://learn.microsoft.com/en-us/cpp/overview/acquire-msvc?view=msvc-170) e [MSVC por linha de comando](https://learn.microsoft.com/en-us/cpp/build/building-on-the-command-line?view=msvc-140).

## Pendencias e prontidao Qt

Alternativas legitimas: executar a validacao em um ambiente Windows de desenvolvimento ja autorizado, com MSVC e dependencias preparadas; ou o usuario disponibilizar registros CodeIntegrity/AppLocker obtidos por um administrador autorizado, para esclarecer a regra sem altera-la. Nao se solicita excecao, desativacao ou mudanca de politica. GCC Release e os testes de nucleo Debug oferecem evidencia parcial, mas nao substituem o console Debug nem MSVC.

O nucleo tem uma referencia funcional verificavel para planejar a integracao Qt. A prontidao de plataforma ainda e condicional: concluir os 165 testes em Debug e Release com o compilador oficial antes de tratar a base Windows como validada. A futura interface devera consumir resultados imutaveis e enviar comandos ao Runner, sem controlar o tempo funcional. Qt/VTK nao foram implementados.

## Arquivos desta etapa

Criado: docs/debug-msvc-stabilization.md (este relatorio). Criadas evidencias build/stabilization-* e XMLs de resultados nos dois diretorios de build. Nenhum arquivo fonte, schema, exemplo, teste ou contrato funcional foi modificado.
