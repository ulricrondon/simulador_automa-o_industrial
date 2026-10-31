# Aplicacao desktop Qt Widgets — Sprints Qt 1.2 e 1.3B

**Estado: codigo preparado, nunca compilado com Qt nem executado.** Qt, moc, cl e vcpkg nao estao disponiveis no ambiente destas sprints. A logica de apresentacao foi compilada e testada; a janela nao. Nao ha validacao visual nem de integracao real com Qt.

## Arquitetura

```text
sim_desktop (Qt Widgets, opcional)         src/desktop/qt
  MainWindow: widgets, QTimer, closeEvent
        |
sim_desktop_presentation (sem Qt)          src/desktop/presentation
  SessionController: sessao, IDs, tickets, encerramento
  view_model: controles, paineis, diagnosticos
        |
sim_runtime / sim_project_document / sim_core (sem alteracao)
```

MainWindow nao decide regra de negocio: repassa cliques ao controlador e mostra o que ele entrega. Sem Q_OBJECT nem sinais proprios, a janela nao depende de moc. O nucleo, o runtime e os schemas nao foram alterados.

## Regras de concorrencia

- Um QTimer de 50 ms chama SessionController::poll(), que nunca espera a thread da sessao.
- O .get() de um ticket, da carga ou da criacao da sessao so ocorre depois de wait_for(0) indicar pronto.
- Cada poll() faz uma unica aquisicao de observation(). Publicacao, imagem, estado apos controles, diagnostico fatal e eventos vem dela. history(), history_view(), publication() e diagnostic() do runtime nao sao usados. Rejeicoes chegam pelos recibos dos comandos.
- O controlador guarda so a observacao atual; a anterior e solta na troca.
- Abrir projeto e criar a sessao rodam em std::async. A copia e a validacao do RunSpec ficam na tarefa, que compartilha o documento imutavel. A falha da propria chamada std::async vira diagnostico.
- O controlador e o unico emissor de IDs de comando. Cada sessao tem novo session_id e reinicia os IDs de comando.
- Encerrar usa request_close(). A sessao so e destruida depois de a conclusao estar pronta, quando a thread ja terminou seu trabalho.
- closeEvent recusa o fechamento enquanto o controlador nao esta idle, pede o encerramento e o timer fecha a janela depois. O close() nao pode partir de dentro de closeEvent, pois o Qt ignora close() aninhado.
- Fechar durante a carga descarta o resultado sem alerta. Fechar durante a criacao da sessao encerra a sessao recem-criada sem envia-la a start.
- Todo callback Qt roda dentro de guarded(), que nao deixa excecao C++ cruzar o laco de eventos. A falha aparece uma vez em Diagnosticos e na barra de status.

## Operacoes

| Operacao | Comportamento |
|---|---|
| Abrir | Valida fora da thread grafica. Falha mostra o tipo do erro e mantem o projeto anterior |
| Iniciar | Cria a sessao fora da thread grafica e envia start. Projeto sem esteira valida, mas nao inicia |
| Avancar 1 / N ticks | step e advance; desabilitados durante outro avanco. O limite e da API |
| Pausar / Continuar | pause e resume; a pausa suspende um avanco em andamento |
| Parar | Usa a posicao reservada da API. Disponivel tambem em failed, pois a parada aplica o reset real |
| Encerrar sessao | request_close, sem bloquear. Permite iniciar outra sessao |

O avanco executa a toda velocidade: nao ha ritmo de tempo real.

## Dados dos paineis

Disponiveis e exibidos: estado operacional; proximo tick e horizonte; tick da imagem; posicao da peca; entradas e saidas do PLC; comando, alimentacao, estado e velocidade do motor; alimentacao e valores amostrado e publicado do sensor; ET, preset e Q de cada TON; CV, PV e Q de cada CTU; comparadores; eventos perdidos; ate 200 eventos recentes.

Sem imagem publicada, os valores aparecem como "—". Nada e estimado.

- A imagem e sempre a de fim do ultimo tick. Depois de parar, nao ha imagem nova: por isso "Motor apos controles" pode mostrar LOW enquanto a imagem ainda mostra HIGH.
- Comparadores sao avaliados com a funcao pura do nucleo sobre os contadores da imagem: mostram o que a proxima varredura le. Na imagem em que CV muda, o comparador ja mostra o novo resultado e a saida ainda reflete o CV anterior.
- Iniciar uma sessao ou abrir um projeto limpa imediatamente todos os paineis, o cursor, os eventos e o contador de perdas. Diagnosticos permanecem, pois registram o que ocorreu. A causa da falha anterior e limpa ao iniciar ou abrir com sucesso.

## Eventos

poll() consome os eventos da mesma observacao com observation_events() e cursor estavel. Uma pagina de 8192 alcanca sempre o fim, pois a janela nao retem mais do que isso. O cursor so avanca depois de as linhas estarem prontas; em caso de excecao, o proximo poll repete o mesmo trecho.

A ordem usa a chave completa do contrato: publication_sequence, time_us, tick, phase, phase_order, sequence.

Quando a API informa lost, o painel recebe uma linha de lacuna com a quantidade e o campo "Eventos nao exibidos (perdidos)" acumula o total da sessao. A linha de lacuna pode sair da lista de 200, mas o total permanece. A perda e so de exibicao: o historico funcional e o replay continuam integrais.

A janela aplica so as linhas novas (events_added) em vez de reescrever o painel, preservando a rolagem.

**Limitacao conhecida:** a janela do runtime guarda 32 blocos, e cada lote de eventos ocupa um bloco, mesmo parcial. Na pratica ela retem cerca de 32 ticks com eventos, nao 8192 registros. A velocidade maxima e polling de 50 ms, a medicao abaixo perdeu mais de 99% das linhas. O painel mostra corretamente os eventos mais recentes e declara a perda, mas nao serve para acompanhar todos os eventos de um avanco longo. A correcao pertence ao runtime.

## Diagnosticos

Severidade decidida por code/reason e por ser a causa publicada, nunca pelo texto: FALHA (causa publicada, orcamentos de snapshots e eventos, excecoes), Rejeicao (fila, estado, horizonte, identidade, orcamentos de admissao) e Informacao (cancelamento). Cada diagnostico entra uma vez no painel, comparando ponteiros, mesmo chegando por ticket e pela publicacao. So FALHA abre uma caixa nao modal, uma vez. A causa fica em destaque depois do encerramento, ate iniciar outra sessao ou abrir outro projeto. Mensagens usam sempre texto simples, e tick, limite e comando so aparecem quando presentes.

## Build

O alvo Qt e opcional (SIM_BUILD_DESKTOP, desligado por padrao); os builds headless nao precisam de Qt. A feature desktop do vcpkg agora contem apenas qtbase; VTK passou para a feature reservada visualization. Com Qt e MSVC disponiveis:

```powershell
cmake --preset windows-msvc-desktop-debug
cmake --build --preset windows-msvc-desktop-debug
./build/windows-msvc-desktop-debug/sim_desktop.exe examples/reference-conveyor.json
```

Sem vcpkg, com uma instalacao local: acrescentar -DSIM_BUILD_DESKTOP=ON e -DCMAKE_PREFIX_PATH=<pasta do Qt 6>. Esses caminhos nao foram executados. A resolucao da feature desktop no baseline do vcpkg tambem nao foi testada.

## Roteiro da primeira execucao real

1. Abrir examples/reference-conveyor.json: titulo, horizonte 12, apenas Abrir e Iniciar habilitados.
2. Iniciar, avancar 1 tick, avancar 4 ticks: tick 5 / 12, tabela conforme docs/reference-replay.md.
3. Pausar e continuar: controles acompanham o estado.
4. Parar: motor apos controles LOW; imagem ainda do tick 4.
5. Encerrar sessao e iniciar de novo sem avancar: sinais, blocos, posicao e eventos mostram "—" ou vazio; Diagnosticos permanece.
6. Avancar ate o tick 6: CTU O1 com CV=1, comparador LOW, O0 ainda HIGH; no tick 7, O0 LOW.
7. Abrir examples/reference-pulses.json: mensagem de projeto sem esteira e Iniciar desabilitado.
8. Abrir um JSON invalido: uma unica caixa, com o tipo do erro.
9. Fechar a janela durante avanco longo, durante pausa e durante a abertura de um projeto grande: fecha sem travar e sem caixa de erro.
10. Avancar um projeto grande a toda velocidade e rolar o painel de eventos para cima: a contagem de perdidos aumenta, aparecem linhas de lacuna e a rolagem nao salta.
11. Abrir projeto com titulo contendo marcacao HTML: deve aparecer como texto literal.

## Lacunas da API headless

- A janela de observacao retem cerca de 32 ticks com eventos, por causa dos blocos parciais.
- O valor que um comparador leu na varredura exibida nao e publicado; so se deriva da imagem.
- Nao ha notificacao de mudanca; a interface depende de polling.
- A API interativa so executa o perfil de esteira.
- O projeto nao tem nomes de sinais: os paineis usam I0 e O0.
- Avanco nao tem ritmo de tempo real; precisara ser feito na camada desktop.

## Testes e medicoes

tests/desktop_presentation_test.cpp: 33 testes da camada sem Qt, com sessoes e projetos reais. GCC Debug e Release: 261/261 no CTest (247 anteriores + 14 novos). Os arquivos Qt passaram somente em verificacao de sintaxe contra declaracoes escritas a mao em build/review-qt, que nao substitui compilar com Qt.

Medicao em Release, tres execucoes, projeto de 13 MiB, 100000 ticks, 70000 comandos e 256 entradas e saidas: chamadas open() ate 203 µs (carga no worker 1,0 a 1,3 s), start() ate 131 µs (sessao pronta em 39 a 59 ms), poll() durante avanco com mediana de 76 a 119 µs e pior caso de 333 µs, com poucas amostras (5 ou 6 por execucao), poll() durante o encerramento ate 4,4 ms, poll() lendo uma janela cheia de 8192 registros 1,0 a 1,8 ms. Dos 210001 eventos da execucao, entre 209521 e 209617 nao foram exibidos.
