# CONTINUIDADE DO PROJETO — RecompPacman

Última atualização: 2026-11-01

Este arquivo registra o estado técnico confirmado para que outro chat ou colaborador possa continuar sem repetir etapas. Leia antes de alterar o projeto.

## Objetivo

Gerar um APK Android ARM64 que execute a recompilação nativa de Pac-Man usando ReXGlue/ReXRuntime. Não é um emulador. O aplicativo deve permitir selecionar a pasta dos arquivos do jogo e, futuramente, oferecer configurações como limite/visualização de FPS e resolução. O APK deve abrir e permitir configurar mesmo quando os arquivos do jogo não estiverem presentes.

## Ambiente do usuário

- O usuário trabalha somente pelo celular Android (Motorola Edge 30 Pro, Android 13, Snapdragon 8 Gen 1).
- Usa Termux, ADB e GitHub Actions; não possui computador.
- Não pedir para reinstalar o conector GitHub nem repetir configurações já concluídas.
- Ao receber “Ok”, interpretar como autorização para continuar, sem pedir confirmação a cada etapa.
- Instruções no dispositivo devem ser compatíveis com Termux/Android.

## Repositório e conta

- Repositório: https://github.com/marcussilvacastrooutlok-afk/RecompPacman
- Branch principal: main
- Conta GitHub: marcussilvacastrooutlok-afk

## Diagnóstico confirmado

O APK chega à execução do jogo e encerra ao ocorrer uma chamada indireta inválida. O log mais recente identificou:

```text
Indirect dispatch miss:
callsite=0x8262C908
target=0x82738920
lr=0x8262C90C

[FATAL] Invalid indirect function:
target=0x82738920
ctr=0x0000000082738920
r12=0x000000008262C90C
r3=0x00000000833AB458
```

A análise dos fontes gerados mostrou que **0x82738920 não é início de função**. É o endereço interno de retorno dentro de `sub_827388A8`, associado a uma chamada para `sub_8234D1F0`:

```cpp
ctx.lr = 0x82738920;
sub_8234D1F0(ctx, base);
```

Há registros de função em `0x827388A8` e `0x82738930`, mas não em `0x82738920`. Portanto, não registrar esse endereço artificialmente como função. A hipótese a investigar é como um endereço interno/de retorno foi parar como destino de uma chamada indireta.

O ponto de chamada identificado em `sub_8262C898` carrega o destino de memória usando o registrador R31 como endereço do slot:

```cpp
ctx.r11.u64 = REX_LOAD_U32(ctx.r31.u32 + 0);
ctx.ctr.u64 = ctx.r11.u64;
ctx.lr = 0x8262C90C;
REX_CALL_INDIRECT_FUNC(ctx.ctr.u32, 0x8262C908);
```

Logo, R31 indica o endereço do ponteiro carregado e R11 contém o destino. É essencial descobrir o valor de ambos no próximo crash.

## Alteração já enviada ao GitHub

Commit confirmado em `main`:

- Commit: `7fa12ff9e1f299e84c2e3611d3f00f705e104090`
- Mensagem: `Add r31 and r11 to invalid indirect-call diagnostics`
- Arquivo alterado: `src/system/function_dispatcher.cpp`
- A mensagem fatal agora inclui `r11` e `r31`, além dos campos anteriores.

A alteração é apenas instrumentação de diagnóstico. **Ainda não é uma correção do crash e não há confirmação de um APK novo construído com esse commit.**

## Próximas etapas obrigatórias

1. Verificar o workflow Android/APK em `.github/workflows/` e confirmar se há execução de build posterior ao commit `7fa12ff9`.
2. Se não houver execução, iniciar o workflow disponível usando uma ação autorizada do GitHub; se isso não estiver disponível, orientar o usuário com o mínimo de passos no celular para executar Actions.
3. Confirmar que o APK foi construído a partir do commit `7fa12ff9` ou posterior. Não confundir sucesso do workflow com correção do jogo.
4. Usuário instala esse APK e reproduz o fechamento.
5. Obter o novo log da pasta privada do app: `/data/data/org.libsdl.app/files/logs/`. O usuário já conseguiu localizar e enviar logs desse diretório; não repetir instruções antigas de exportação.
6. Analisar principalmente `r31` e `r11` para localizar o slot de ponteiro e rastrear quem escreveu o valor `0x82738920`.
7. Corrigir a origem da chamada/registro conforme evidência e então reconstruir e testar.

## Arquivos e evidências já analisados

Um artefato de diagnóstico do workflow `41357951468` contém fontes gerados, incluindo:
- `pacman_colors_recomp.167.cpp`
- `pacman_colors_recomp.35.cpp`
- `pacman_colors_register.cpp`
- `pacman_colors_init.cpp`
- `pacman_colors_funcs.h`

Os registros e inicializadores confirmam a ausência de uma entrada de função em `0x82738920`.

Workflow anterior:
- Run: https://github.com/marcussilvacastrooutlok-afk/RecompPacman/actions/runs/41357951468
- Commit do run: `61ac5150836a63a15322ce84af98d1b6d88c5891`
- Artefatos: `pacman-android-apk` e `diagnostico-pacman`
- Esse APK ainda fechava; não tratá-lo como versão corrigida.

## Histórico de erros — não voltar a tratar como causa principal sem nova evidência

Erros antigos incluíram reserva de espaço de memória guest de 4 GB, bibliotecas SDL/ReXRuntime ausentes, divergência SDL Java/C e métodos JNI ausentes. O diagnóstico mais recente e específico é a chamada indireta inválida em `sub_8262C898`, com destino `0x82738920`.

## Regras para continuar

- Não dizer que o problema foi resolvido até existir build e teste confirmados.
- Não registrar `0x82738920` como função sem provar que isso é correto.
- Não pedir novamente o arquivo do jogo ou dados já fornecidos.
- O arquivo privado `Default.xex` e seus dados não devem ser adicionados ao repositório nem a artefatos públicos.
- Registrar aqui novos commits, workflows, logs, hipóteses confirmadas e próximos passos sempre que o estado mudar.


## Registro de continuidade — 2026-11-02 (run 41375019134)

### Resultado confirmado do build Android

- Workflow: `Recompilar Pac-Man para Android`
- Run: https://github.com/marcussilvacastrooutlok-afk/RecompPacman/actions/runs/41375019134
- Commit usado: `461f0a2a933bd217befcf0540755008794c4701f`
- Resultado: `success`; todas as etapas, incluindo compilação ARM64, empacotamento SDL3, diagnósticos e upload, terminaram com sucesso.
- Artefato publicado: `pacman-android-apk`, ZIP de aproximadamente 118,8 MB; APK extraído: `pacman-android-arm64-debug.apk`, aproximadamente 119,6 MB.
- Artefato de diagnóstico também publicado: `diagnostico-pacman`.
- O APK contém as bibliotecas ARM64 `libSDL3.so`, `libmain.so`, `librexruntime.so` e `librexgpu-xenos.so`.

### Bloqueio de distribuição identificado por inspeção do APK

O APK foi baixado e inspecionado diretamente. Ele contém:

```text
assets/game/default.xex  8.970.240 bytes
```

O conteúdo confirma que o XEX está embutido no APK. O workflow copia explicitamente `$GITHUB_WORKSPACE/default.xex` para `android-package/app/src/main/assets/game/default.xex` e altera `SDLActivity.getArguments()` para copiar esse asset ao armazenamento privado e passar `--game_data_root`.

**Conclusão:** o APK é um artefato de build ARM64 confirmado, mas não atende ao requisito de selecionar a pasta do jogo no armazenamento do telefone e não deve ser considerado versão final para distribuição/instalação. Não foi feito teste de execução deste APK neste ciclo. A presença do XEX no repositório público e no APK permanece uma questão de proteção/distribuição a tratar; não remover ou alterar visibilidade sem autorização explícita.

### Próximas ações obrigatórias

1. Alterar o workflow para usar o XEX somente no codegen/build host e não copiá-lo para `assets/` do APK.
2. Implementar um seletor Android real (Storage Access Framework) para escolher a pasta do jogo, persistir a permissão URI e fornecer acesso compatível ao runtime; a simples alteração de `--game_data_root` para um caminho de arquivo comum não é suficiente para URIs SAF.
3. Validar que o APK final não contém `.xex`, que mantém as quatro bibliotecas ARM64 e que inicia sem arquivos do jogo, oferecendo a seleção da pasta.
4. Depois disso, investigar o crash de dispatch indireto usando a instrumentação R11/R31 já registrada acima. O run atual usou o commit `461f0a2a`, anterior à instrumentação `7fa12ff9`; portanto, este APK não contém o diagnóstico R11/R31.
5. Registrar o novo commit/run e só solicitar teste no telefone quando houver APK adequado.



## Registro de continuidade — 2026-11-02 (run 41379736559)

- Run `41379736559` terminou com `success` no commit `298c3be2d2fbcd3df9b65af20be2d10bd7b96d42`.
- Artefatos publicados: `pacman-android-apk` (artifact ID `13114417157`, 118.765.305 bytes) e `diagnostico-pacman` (artifact ID `13113729824`).
- Esse run apenas recompilou o workflow existente após atualização do registro; não implementa o seletor de pasta nem remove o XEX embutido. Não solicitar instalação/teste deste APK.

## Decisão de arquitetura de preparação dos dados — 2026-11-02

O usuário confirmou que outros jogos recompilados que utiliza consomem armazenamento interno aproximadamente equivalente ao tamanho da pasta do jogo depois da seleção. Adotar como referência uma preparação local dos dados para Pac-Man e Resident Evil.

### Estratégia

1. Exibir uma tela inicial/configurações que funcione sem dados de jogo.
2. Oferecer seleção de diretório por Android Storage Access Framework (`ACTION_OPEN_DOCUMENT_TREE`).
3. Persistir permissão de leitura da URI selecionada.
4. Copiar recursivamente os arquivos e subpastas para diretório privado do app, mantendo nomes/estrutura.
5. Verificar espaço livre antes da cópia; exibir progresso; tratar cancelamento, I/O e falta de espaço; não iniciar runtime se a preparação falhar.
6. Reutilizar a cópia existente quando válida; permitir selecionar outra pasta e substituir/preparar dados novamente.
7. Iniciar SDL/ReXGlue passando `--game_data_root=<caminho privado POSIX>`.
8. Não incluir `default.xex` como asset no APK. O XEX pode continuar sendo entrada temporária do codegen no workflow, mas não deve ser empacotado nem publicado.

### Integração SDL identificada

- No SDL 3.5.0 fixado em `8bf3b7215ad9fc3deb583c6a3a37c6c67f2e24e4`, `SDLActivity.getArguments()` retorna argumentos para o aplicativo nativo e pode ser sobrescrito.
- O manifesto atual declara `SDLActivity` como launcher. A nova tela Android deverá ser launcher, e iniciar `SDLActivity` somente após a preparação dos dados; o argumento `--game_data_root` será fornecido via Intent extra e lido pelo override de `getArguments()`.
- Ainda não foi implementada a Activity nem a cópia recursiva. Não tratar o helper JNI ContentResolver como substituto dessa funcionalidade.
- Run/artefato atual ainda é build antigo com asset XEX; nenhum APK final SAF foi produzido.

### Próximos passos

1. Implementar classe Java Android de setup reutilizável com seletor SAF, cópia recursiva, validação de espaço, progresso e mensagens de erro.
2. Alterar manifesto para usar a tela de setup como launcher e `SDLActivity` como Activity interna.
3. Remover do workflow a cópia do XEX para `assets/game/`; modificar `getArguments()` para receber somente o caminho privado preparado.
4. Validar no APK: sem nenhum `.xex`/asset de jogo; libs ARM64 presentes; app abre sem jogo; seletor persiste; cópia e início usam caminho correto.
5. Só depois solicitar instalação no telefone e retomar diagnóstico R11/R31 com o APK instrumentado.
