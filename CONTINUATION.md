# CONTINUIDADE DO PROJETO — RecompPacman

Última atualização: 2026-11-02

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

O APK chega à execução do jogo e encerra ao ocorrer uma chamada indireta inválida. O log mais recente do Workflow #72 confirmou:

- ReXApp: ConstructRuntime OK
- ReXApp: LaunchModule queued
- App OnInitialize returned: 1
- Em seguida: Fatal signal 6 (SIGABRT) no thread Main XThread (F).
- Backtrace: rex::runtime::InvalidFunctionTrap(PPCContext&, unsigned char*)+288, chamado por sub_8262C898+356, depois sub_8256DE18, sub_829FA4F8 e xstart.
- O novo prefixo [REX_DISPATCH_FATAL] não aparece no logcat do Workflow #72. A hipótese operacional é que stderr não está sendo encaminhado ao logcat nessa configuração; o arquivo não contém os valores novos de R11/R31.

A evidência de diagnóstico anterior identificou o destino 0x82738920, que é um endereço interno de retorno dentro de sub_827388A8, não um início de função. Não registrar esse endereço artificialmente como função. O ponto de chamada sub_8262C898 carrega o destino via R31 e coloca em R11/CTR. Precisamos capturar os valores atuais de R31 e R11 para rastrear a origem do ponteiro.

## Workflow #72 — APK testado pelo usuário

- Workflow: Recompilar Pac-Man para Android
- Run ID: 41449217917 (run #72)
- Commit do run: fcd506f328dbd731807c67d20e1705fd221e02fe
- Resultado: success; compilação ARM64, empacotamento SDL3, instalação SAF, compatibilidade JNI e publicação concluídos.
- Artefato APK: pacman-android-apk, ID 13148100489, aproximadamente 109,8 MB.
- Artefato diagnóstico: diagnostico-pacman, ID 13147730871.
- O usuário instalou o APK #72, selecionou a pasta do jogo e tocou em Iniciar. Resultado: tela preta breve e retorno à tela inicial (crash).
- Log enviado pelo usuário: pacman-workflow72.txt (14 mil linhas). O crash ocorre em 11-02 11:08:10; o backtrace confirma InvalidFunctionTrap e sub_8262C898. Não há linha [REX_DISPATCH_FATAL], nem os registradores R11/R31.
- Não solicitar nova instalação do mesmo APK #72.

## Alteração de instrumentação do Workflow #72

- Commit: 236361a0e05c1be29897494226347b18c154dc41 — src/system/function_dispatcher.cpp.
- Tentou escrever os registradores diretamente em stderr via std::fprintf + std::fflush.
- O teste real no aparelho provou que a linha não chegou ao arquivo Logcat. A instrumentação não forneceu os dados necessários.
- A causa original do crash continua sem correção.

## Nova alteração enviada após análise do log #72

1. Commit e57adc4d0063f64e0ec53b665b1e0765926f5948 — src/system/function_dispatcher.cpp:
   - Em Android, InvalidFunctionTrap agora usa __android_log_print(ANDROID_LOG_ERROR, "ReXDispatch", ...) para enviar [REX_DISPATCH_FATAL] e os campos target, LR, callsite, CTR, R11, R31, R12 e R3 diretamente ao logd.
   - Em plataformas não Android, mantém fprintf(stderr, ...).
   - A lógica fatal do runtime permanece inalterada.
2. Commit 8198cda8cfc13b2072c9c92ded6fe4af99db192b — src/system/CMakeLists.txt:
   - Adicionado link explícito log ao alvo rexruntime sob if(ANDROID), para resolver __android_log_print.

Essas mudanças são instrumentação de diagnóstico, não correção do jogo. Aguardando o GitHub Actions disparado pelo push para compilar um novo APK. Não pedir instalação até confirmar build concluído e artefato publicado.

## Próximas etapas

1. Verificar status do workflow iniciado pelos commits e57adc4/8198cda.
2. Se falhar, inspecionar os logs do job e corrigir a falha de compilação/link antes de pedir qualquer teste.
3. Se tiver sucesso, confirmar APK e artefato de diagnóstico publicados.
4. Só então solicitar ao usuário instalar o novo APK e reproduzir uma vez o crash.
5. Coletar logcat e procurar REX_DISPATCH_FATAL/tag ReXDispatch; usar R11 e R31 para rastrear o slot de ponteiro e quem escreveu o destino inválido.
6. Corrigir a origem da chamada/registro com base nos valores capturados; não mascarar o crash registrando endereço interno como função.
7. Registrar commits, workflows, logs, hipóteses confirmadas e próximos passos neste arquivo.

## Regras para continuar

- Não dizer que o problema foi resolvido até existir build e teste confirmados.
- Não pedir novamente o arquivo do jogo ou dados já fornecidos.
- O arquivo privado Default.xex e seus dados não devem ser adicionados ao repositório nem a artefatos públicos.
- Documentação isolada não deve disparar compilação.
- Não pedir para instalar APK sem alteração funcional/diagnóstica real e artefato confirmado.
- O projeto deve continuar sendo uma recompilação nativa, não um emulador.


## Instrumentação do cadastro de callbacks — próxima compilação

- O diagnóstico do APK #75 confirmou que o trap ocorre na entrada 53 da tabela global de callbacks: base 0x834808D0, stride de 20 bytes, slot em 0x83480CF4.
- A rotina gerada `sub_8262C828` é a rotina que grava novos registros: r4 é o destino/callback; r5, r6 e r7 são os demais campos do registro; o contador fica em 0x83482808.
- O endereço inválido observado, 0x82738920, é um endereço interno de retorno em `sub_827388A8`, portanto não deve ser registrado como função.
- O workflow foi alterado para instrumentar a entrada de `sub_8262C828` com tag Android `ReXCallbackReg`, registrando índice e r4-r7 antes de gravar cada entrada. Também adiciona link explícito da biblioteca Android `log` ao alvo gerado `pacman_colors`.
- Commit da alteração do workflow: ad5fa2234b626db1cdc322e6d222362f6fad47da.
- Objetivo do próximo teste: comparar a linha de cadastro do índice 53 com o valor encontrado no trap. Se o cadastro já mostrar target=0x82738920, o ponteiro foi fornecido errado ao registrar; se mostrar outro valor, investigar sobrescrita posterior do slot.
- A compilação foi disparada pelo push; verificar o resultado e validar o APK antes de solicitar instalação. O usuário não precisa repetir testes anteriores.


## Diagnóstico do callback 53 — APK #77
- O usuário testou o APK #77; o fechamento em tela preta continua ao iniciar o jogo.
- O log `pacman-callbacks.txt` registra callbacks pelos índices 3, 5, 7... 49 e depois 54; não há registro do índice 53 pela rotina `sub_8262C828`.
- Na falha, `ReXDispatch` confirma alvo `0x82738920`, slot `0x83480CF4` e chamada indireta em `0x8262C908`.
- A rotina `sub_8262C828` grava o alvo no primeiro campo de cada registro e incrementa o contador em `0x83482808`. Cada registro ocupa 20 bytes; o registro 53 usa o destino em `0x83480CF4`.
- Nova etapa no workflow instrumenta a leitura do registro 53 imediatamente antes da chamada indireta, registrando contador, destino, argumentos, estado e campos restantes. A próxima coleta terá a tag `ReXCallback53`.
- Não registrar `0x82738920` como função: é endereço interno de código/retorno, não entrada de função.
- Estado: causa ainda não corrigida; próxima compilação é diagnóstica.

## Diagnóstico do Workflow #79 — registro 53
- O log enviado do APK #79 confirma: count=63, slot=0x83480CF4, target=0x82738920, arg0=0x833AB458, arg5=0x82738AA0, state=0, field12=0, field16=0.
- O endereço inválido está presente no registro imediatamente antes da chamada indireta; portanto, o trap apenas revela um ponteiro já armazenado.
- No código gerado, sub_827388A8 é um alocador/rotina de inicialização de objeto. O endereço 0x82738920 é o LR configurado no ramo de falha dessa rotina antes de chamar sub_8234D1F0; não é uma entrada de função válida.
- A próxima instrumentação acrescenta caller_lr ao log ReXCallbackReg para identificar quem chama sub_8262C828 e quem fornece target 0x82738920. Ainda não foi aplicada correção funcional.

## Atualização — workflow #82 (2026-11-03)
- Commit: b583fdc3b69014152fe38c329dd52c12a99ce6c9.
- Corrigida a instrumentação de `sub_8262C828`: `ctx.lr.u32` foi substituído por `(uint32_t)ctx.lr`, pois LR é armazenado como valor numérico e não como membro com `.u32`.
- Workflow de build #82: sucesso. Compilação ARM64, empacotamento SDL3, assinatura e publicação do APK passaram.
- APK: artefato `pacman-android-apk`, ID 13198209998, cerca de 109,8 MB.
- Diagnóstico: artefato `diagnostico-pacman`, ID 13198920206.
- Esta alteração é apenas diagnóstica; não corrige ainda o callback 53 nem comprova que o jogo inicia.
- Próxima etapa: testar o APK #82, iniciar o jogo e recolher `ReXCallbackReg`, `ReXCallback53` e `ReXDispatch` para identificar o LR de quem cadastra o callback e confirmar o valor gravado no slot 53.


## Análise do log do APK #82 e próxima investigação (2026-11-03)
- O arquivo `pacman-callback82.txt` confirmou que `sub_8262C828` registra callbacks com chamador `caller_lr=0x82DA25A8`, mas os valores `count_before` observados saltam (por exemplo 49 para 54 e 62 para 63). O índice 53 não aparece nos logs dessa rotina.
- Imediatamente antes do despacho, `ReXCallback53` confirmou `count=63`, `slot=0x83480CF4`, `target=0x82738920`, `arg0=0x833AB458`, `arg5=0x82738AA0`, estado 0 e campos 12/16 zerados.
- `ReXDispatch` confirmou chamada indireta em `0x8262C908`, com destino `0x82738920` e slot `0x83480CF4`.
- A inspeção do código gerado mostra que `sub_8262C828` calcula a tabela na base `0x834808C8`, grava cada registro com stride de 20 bytes e incrementa o contador em `0x83482808`. `sub_8262C898` percorre os registros e chama o primeiro campo quando o estado do registro é zero.
- O contador salta entre os valores registrados; isso sugere que há atualizações do contador/registro por outro caminho ou que nem todas as entradas passam pela rotina instrumentada. Ainda não foi localizada a origem do índice 53.
- Próxima alteração diagnóstica: durante a etapa de preparação do artefato, varrer todas as funções geradas e preservar as funções que referenciam diretamente os endereços da tabela ou o padrão de base `+2248` com contador `+8000`. O relatório será `diagnosticos/referencias-tabela-callbacks.txt`.
- Não há correção funcional ainda; não pedir instalação do APK até avaliar o relatório e produzir uma alteração que justifique novo teste.


## Investigação do artefato diagnóstico #82 — 2026-11-03

- O artefato `diagnostico-pacman` do Workflow #82 foi baixado e inspecionado.
- A busca independente nos quatro shards preservados encontrou apenas duas funções com referências à tabela global:
  - `sub_8262C828`: lê e incrementa o contador em `0x83482808`, grava os campos do registro com stride de 20 bytes.
  - `sub_8262C898`: percorre os registros e despacha o callback.
- Não foi encontrado, entre os arquivos preservados do #82, um segundo gravador explícito do contador. Isso não prova que não exista outro caminho em shards que não foram preservados.
- O log anterior do #82 apontou `caller_lr=0x82DA25A8`, mas o shard que contém esse endereço não estava no artefato #82. Por isso, ainda não é possível identificar o chamador.
- Alteração de coleta feita no workflow:
  - Commit `c14eb341223a8126af6e447b4a34d236afcb6170`: acrescenta busca de contexto para `0x82DA25A8` e preserva o shard que contiver esse endereço.
  - Commit `7771369ca45350c694830cd68abfe2074aed8328`: corrige as continuações de linha shell da nova busca.
- Próximo passo: aguardar/identificar o próximo artefato `diagnostico-pacman`, verificar `caller-callback.txt` e o shard adicional, e então rastrear o chamador de `sub_8262C828`.
- A alteração é exclusivamente diagnóstica. Não houve correção funcional nem novo APK para teste do usuário.


## Atualização — Workflow #99 concluído (2026-11-04)

- Commit do workflow: `d94629874762fac1d13230315078c504ed86b2b7` — `Corrigir tabs literais na instrumentação C++`.
- Corrigidos nove escapes duplicados `\\\\t` no workflow para que a instrumentação Python gere tabulações reais no código C++.
- Workflow de build #99, run ID `41781179314`: `completed/success`.
- Lint #99, run ID `41781178969`: `completed/success`.
- Build ARM64, Gradle, assinatura APK e verificação de bibliotecas passaram.
- APK publicado: artefato `pacman-android-apk`, ID `13300981941`, arquivo `pacman-android-arm64-debug.apk` (110.597.432 bytes; SHA-256 `0bf9995a0b721ddb4e79189c335946a44ece52f38d623339f3369798e1169604`).
- APK inclui `libSDL3.so`, `libmain.so`, `librexruntime.so` e `librexgpu-xenos.so` em `arm64-v8a`. Ícone mipmap do launcher foi gerado com Pac-Man.
- Artefato diagnóstico: `diagnostico-pacman`, ID `13301156733`. Inclui `pacman_colors_recomp.167.cpp`, `.35.cpp`, `.64.cpp`, `.78.cpp`, `.82.cpp`, `pacman_colors_init.cpp`, `pacman_colors_register.cpp`, headers e contexto do chamador.
- Instrumentação incluída:
  - `ReXSlot53 BEFORE`: antes do cadastro, quando o contador é 53, registra LR e r4-r7/target anterior.
  - `ReXSlot53 AFTER`: depois da gravação do registro 53, quando o contador passa a 54, registra target, argumentos e campos do registro.
  - `ReXCallback53`: antes do despacho, registra contador, endereço do slot e os campos do registro 53.
  - `ReXSlot53 WRITER init_list`: observa as chamadas indiretas da lista de inicialização quando o contador é 54; não confundir esta observação com prova de escrita na tabela.
- Verificação estática do artefato #99: a rotina `sub_8262C828` grava o destino em `r4), os outros campos em r5-r7, atualiza o contador em `0x83482808` e grava o estado zero. O registro 53 começa em `0x83480CF4`; a instrumentação de leitura do dispatcher compara esse endereço.
- O endereço `0x82738920` continua identificado como LR/endereço interno no ramo de `sub_827388A8` antes de chamar `sub_8234D1F0`, não como início de função. Não adicionar stub/registro artificial para esse endereço.
- O usuário testou o APK #99 duas vezes e forneceu `pacman-workflow99-full.txt`; as duas execuções reproduziram o mesmo crash no dispatcher. O arquivo é um logcat completo do aparelho, não um APK.
- No log, `ReXApp` conclui `SetupEnvironment`, `SetupPresentation` e `ConstructRuntime`; o fechamento ocorre depois de `LaunchModule queued`, no despacho de `0x82738920`. Portanto, para este teste, não há evidência de falha no SAF, SDL, carregamento das bibliotecas ou reserva de memória.
- O log de leitura mostra `count=63`, slot `0x83480CF4`, target `0x82738920`, `arg0=0x833AB458`, `arg5=0x82738AA0`, `state=0`. O contador 63 é o total atual de registros; o slot continua sendo o índice 53 (base `0x834808D0`, stride 20).
- Os marcadores antigos `ReXSlot53 BEFORE/AFTER` não aparecem. O marcador `WRITER init_list` observa a lista de inicialização `0x833A0F44`, não a tabela de callbacks `0x834808C8`; não usar essas linhas como prova de quem escreveu o callback.
- O artefato diagnóstico #99 foi baixado e inspecionado. `sub_8262C828` calcula o destino de cada registro como `base + 8 + count*20`; grava target de `r4`, demais parâmetros de `r5-r7`, incrementa o contador em `0x83482808` e grava o estado zero. A instrumentação antiga dependia do contador estar exatamente em 53/54 e, por isso, não identificou o escritor.
- `0x82738920` é o endereço de retorno definido imediatamente antes da chamada a `sub_8234D1F0` no ramo de erro de `sub_827388A8`. O ponto de entrada real dessa função é `0x827388A8`, que consta na tabela `SetFunction`; `0x82738920` não consta como início de função. Não criar stub nem registrar esse endereço artificialmente.
- A rotina de registro `sub_8262C828` tem apenas um call-site direto visível no código gerado (`sub_83386380`), mas pode também ser invocada por despacho indireto. É necessário capturar todas as entradas da rotina, inclusive LR do chamador e r4-r7, para identificar a chamada que fornece o target inválido.
- Commit `4f1b332844603317e6dd8bbc74efdbcc21ce4e67`: workflow atualizado para substituir o teste por contador por instrumentação baseada no endereço real calculado do slot. A nova tag `ReXCbWrite` registra cada entrada em `sub_8262C828` (contador, LR do chamador e r4-r7) e, quando o destino calculado é exatamente `0x83480CF4` ou o target é `0x82738920`, registra os valores antes/depois da gravação. Este commit apenas altera o workflow; ainda não é um APK testado.
- Próximo passo: verificar se o workflow novo compila e se o artefato diagnóstico inclui o shard instrumentado. Só solicitar novo teste no telefone depois de um APK novo ser gerado com sucesso. No próximo log, procurar `ReXCbWrite`, `ReXCallback53` e `ReXDispatch`.
- Não considerar o jogo corrigido até o teste real. Não alterar a lógica do jogo nem registrar artificialmente `0x82738920`.


## Referência externa analisada — Black Android (2026-11-04)

Fonte: APK fornecido pelo usuário `Black.apk.zip` e repositório público https://github.com/RanggaCasper/Black-Android (branch main). Análise estática; o APK não foi executado neste ambiente.

### Diferenças fundamentais
- Black Android recompila o Black do Xbox original: XBE/x86. Pac-Man continua sendo Xbox 360: XEX/PowerPC. Não copiar código de kernel, ABI, tradução de CPU, chamadas de sistema ou renderer específico do Black para o Pac-Man.
- O README do Black confirma recompilação estática antecipada via XboxXBE-Recomp para C++, compilada para ARM64; declara explicitamente que não é emulador.
- O repositório Black usa XboxXBE-Recomp com SDK fixado no commit `99078b119b5d5e0d1d6afb1390e29437c0c984c0`.
- No CMake do Black, o runtime portátil do SDK, o código recompilado do jogo, as fontes específicas do jogo e o host Android são ligados em uma única `libmain.so`. O APK analisado tem `libSDL3.so`, `libc++_shared.so` e `libmain.so`; o `DT_NEEDED` de `libmain.so` lista SDL3, Vulkan, Android, log, libc++ e bibliotecas de sistema. Não existe `librexruntime.so` separado no APK Black. Isso não significa que Pac-Man deva remover sua biblioteca ReXRuntime: a arquitetura e o SDK são diferentes.
- O Black usa SDL3 3.4.16 (tag `release-3.4.16`) tanto no CMake quanto no Java SDLActivity. A lição aplicável é manter o lado Java e o nativo SDL rigorosamente sincronizados; isso é especialmente relevante diante dos antigos erros de incompatibilidade JNI/versão SDL no Pac-Man.

### Padrões Android reutilizáveis como referência
- `BlackActivity` estende `SDLActivity`; `getLibraries()` retorna `SDL3` e `main`, definindo a ordem de carregamento. Isso é uma referência para revisar o carregamento de bibliotecas e JNI do launcher Pac-Man, não uma implementação para copiar cegamente.
- O host nativo envia stderr para Logcat por pipe e também inicializa logs persistentes em uma pasta acessível do app. Pac-Man já tem tags Android próprias; comparar e reforçar a persistência/flush dos logs pode ser útil, mas não substitui as tags de diagnóstico já instrumentadas.
- O Black trata ciclo de vida Android com eventos SDL de foreground/background, estado atômico e espera do renderer até voltar ao primeiro plano.
- O Black implementa pacing de FPS por relógio monotônico e resolução dinâmica ajustada pela taxa de quadros. Ideias para uma etapa posterior, depois de o jogo iniciar de forma estável; não são prioridade para resolver o callback 53.
- O Black tem controles touchscreen próprios integrados ao SDL e um caminho adicional para gamepads Android que não são reconhecidos como joysticks SDL. Pode servir como referência futura para controles, não para a causa atual do crash.

### Descoberta sobre armazenamento e seleção de jogos
- Correção importante em relação à impressão inicial: o Black não oferece como fluxo principal um seletor SAF de pasta de jogo. Ele procura `default.xbe`/ISO em `/storage/emulated/0/Black`, nas pastas `Black` dos volumes removíveis e em `Android/data/com.racas.black/files/assets`.
- O Black pede permissão especial `MANAGE_EXTERNAL_STORAGE` (Android 11+) para ler a pasta compartilhada; também oferece importação SAF de arquivo ISO com `ACTION_OPEN_DOCUMENT`.
- O importador copia em thread de trabalho para arquivo temporário `.part`, verifica tamanho/espaço livre e assinatura do disco quando pode, faz sync e renomeia ao final para não expor cópia incompleta ao carregador.
- Pac-Man já tem uma implementação diferente de seleção de pasta via SAF em `tools/android/install_game_setup.py`, copiando os arquivos selecionados para armazenamento privado do app antes de iniciar SDL. Portanto, não substituir esse fluxo pelo modelo de acesso amplo do Black sem necessidade. As ideias aproveitáveis são cópia atômica, validação do arquivo de entrada, progresso, tratamento de cancelamento e logs de erro.

### Carregamento e validação do jogo no Black
- `native/game_files.cpp` procura o XBE em diretórios conhecidos e também abre ISOs; calcula SHA-1 do XBE e compara com o XBE exato usado para gerar o código nativo.
- Uma versão europeia conhecida é convertida por patch antes da comparação; outras versões são identificadas e só iniciam após confirmação do usuário.
- O Black verifica limites/tamanho dos arquivos dentro da ISO e registra erros de imagem truncada.
- O carregador aguarda arquivos aparecerem sem bloquear completamente os eventos SDL.
- Conceito aproveitável no Pac-Man: validar que o XEX selecionado corresponde ao binário que gerou a recompilação (hash/identidade) e registrar claramente caminho, tamanho, versão/hash e erro de leitura. Não importar o código XBE/Xbox original.

### Build e dependências do Black
- O build oficial do Black usa Windows 2025/Visual Studio para executar o recompiler x86 host, Android NDK 28.2.13676358, CMake 3.31.6, Java 17, SDL3 3.4.16 e Vulkan.
- O workflow oficial é disparado por tags `v*`, não a cada push em main. Não copiar seu pipeline de build para Pac-Man, que já possui workflow Linux/Android próprio.
- O APK examinado é ARM64; suas bibliotecas nativas foram identificadas como ELF AArch64. `libmain.so` depende dinamicamente de `libSDL3.so`, `libvulkan.so`, `libandroid.so`, `liblog.so`, `libc++_shared.so` e bibliotecas de sistema.

### Aplicação prática no Pac-Man — plano, ainda não implementado
1. Prioridade permanece obter o log real do APK #99 e determinar quem fornece/sobrescreve o target do slot 53. O Black não fornece evidência para corrigir `0x82738920`.
2. Revisar a ordem de carregamento SDL Java/nativo e a lista de bibliotecas no launcher Pac-Man, garantindo uma única versão SDL coerente. Não alterar sem conferir o workflow e os arquivos reais do Pac-Man.
3. Depois de estabilizar a execução, considerar reforço do log persistente, validação de identidade/hash do XEX e melhorias da cópia SAF (temporário + rename), mantendo a seleção de pasta já existente.
4. Em etapa posterior, estudar pacing de FPS, resolução dinâmica e controles touchscreen do Black, adaptando-os à arquitetura ReXGlue/ReXRuntime e ao renderer do Pac-Man.

Status: referência analisada; nenhuma fonte do Black foi copiada para o Pac-Man e nenhuma alteração funcional foi feita por causa desta análise.


## Continuação — instrumentação do escritor do callback (2026-11-04)

- O diagnóstico do APK #99 foi incorporado acima. Não pedir novo teste do #99: o usuário já o executou duas vezes e enviou o log completo.
- O workflow foi alterado no commit `4f1b332844603317e6dd8bbc74efdbcc21ce4e67` para instrumentar o registro `sub_8262C828` sem pressupor o valor do contador. A tag `ReXCbWrite` emite uma linha `ENTER` por chamada e linhas `BEFORE/AFTER` quando o endereço calculado da gravação é o slot 53 ou quando `r4` contém `0x82738920`.
- O log `ENTER` é emitido no prólogo, antes de `ctx.lr` ser substituído pelo LR interno da própria função, preservando o LR do chamador.
- Ainda não há confirmação de build para este commit. Verificar o resultado do GitHub Actions antes de solicitar qualquer teste no Motorola.
- Se não houver `BEFORE/AFTER` para o slot 53, mas o dispatcher continuar lendo o target inválido, a gravação está ocorrendo fora de `sub_8262C828`; a próxima etapa será instrumentar os caminhos genéricos de escrita de memória do ReXRuntime com filtro restrito ao intervalo do registro 53, sem registrar todos os acessos à memória.


## Resultado da instrumentação de callbacks — APK #103 (2026-11-04)

- Workflow Android #102 (run ID 41907978251) e #103 (run ID 41908033888) concluídos com sucesso; os lint checks correspondentes também passaram.
- APK #103 baixado do artefato `pacman-android-apk` (artifact ID 13364390864), extraído e verificado localmente: `lib/arm64-v8a/libmain.so`, `libSDL3.so` e `librexruntime.so` presentes.
- SHA-256 do APK #103: `443bcf9b3007df4aa6d22a3c83704af5f5f213a404f64db88248917f11ce2f97`.
- A inspeção de strings em `libmain.so` confirmou que os marcadores `ReXCbWrite`, `ENTER count`, `BEFORE slot` e `AFTER slot` estão compilados no binário.
- O artefato diagnóstico #103 (artifact ID 13364076200) foi extraído. Em `pacman_colors_recomp.167.cpp`, a instrumentação está na definição de `sub_8262C828`, captura o LR de entrada e os registradores r4-r7 e calcula o destino real da gravação. O endereço esperado para o slot 53, com índice 53, é `0x83480CF4`.
- Não houve alteração da lógica do jogo nem registro artificial de `0x82738920`; esta versão é diagnóstica.
- Próxima etapa: o usuário instalará e executará o APK #103 com a pasta do jogo já selecionada e enviará um logcat completo após o fechamento. O dado principal será a sequência `ReXCbWrite ENTER`, `BEFORE`, `AFTER`, comparada com `ReXCallback53` e `ReXDispatch`. Se aparecer `BEFORE/AFTER slot=0x83480CF4`, teremos o chamador e os parâmetros exatos que gravaram o target. Se o dispatcher ainda encontrar `0x82738920` sem qualquer gravação correspondente por `sub_8262C828`, a investigação deverá migrar para outros caminhos de escrita de memória.
- O APK #99 não precisa ser testado novamente. O APK de diagnóstico atual é o #103.

## Resultado do log completo do APK #103 — 2026-11-04

- O usuário enviou `pacman-workflow103-full.txt`, com 3.620 linhas. O arquivo registra duas execuções do APK #103 (PIDs 8301 e 11209); ambas reproduzem o mesmo fechamento ao iniciar o jogo.
- Nas duas execuções, o runtime inicia e chega ao dispatcher. Antes do crash, `ReXCallback53` lê:
  - contador global: 63;
  - endereço do registro: `0x83480CF4` (índice 53, base do primeiro registro `0x834808D0`, stride 20);
  - target: `0x82738920`;
  - arg0: `0x833AB458`;
  - arg5: `0x82738AA0`;
  - state: 0; field12/field16: 0.
- `ReXDispatch` confirma target/CTR/R11 `0x82738920`, slot em R31 `0x83480CF4` e callsite `0x8262C908`. O tombstone confirma `InvalidFunctionTrap` chamado por `sub_8262C898+360`; não é falha de SDL/SAF nem de reserva de memória.
- A nova instrumentação aparece no binário e no log: `ReXCbWrite ENTER` é emitida para várias chamadas a `sub_8262C828`. Porém, não existe nenhuma linha `ReXCbWrite BEFORE` nem `AFTER` nas duas execuções, e nenhum ENTER mostra `r4_target=0x82738920`.
- Isso é evidência de que o target inválido não foi gravado por uma das execuções instrumentadas de `sub_8262C828` que aparecem no log. Ainda não prova qual caminho alternativo gravou o slot; pode ser outra rotina de escrita, inicialização/cópia de memória ou caminho de registro diferente.
- As linhas `ReXSlot53 WRITER init_list_slot=...` observam a lista separada percorrida por `sub_82DA24F0`; os endereços ficam em `0x833A04D8..0x833A052C`, não no registro de callbacks `0x83480CF4`. Não interpretar essas linhas como escrita no callback 53.
- A inspeção do shard `pacman_colors_recomp.167.cpp` confirma que `sub_827388A8` começa em `0x827388A8`; no ramo de falha de alocação, o código define LR `0x82738920` imediatamente antes de chamar `sub_8234D1F0`. A tabela de funções registra `0x827388A8` e `0x82738AA0`, mas não `0x82738920`. Portanto, o target continua sendo um endereço interno de retorno, não uma entrada registrada. Não adicionar stub nem mapear esse endereço como função.
- O shard `pacman_colors_recomp.78.cpp` confirma que `sub_82DA24F0` chama callbacks de uma lista distinta; a instrumentação nessa função não identifica o escritor do registro 53.
- Próxima investigação: localizar os caminhos de escrita de memória usados pelo código recompilado/runtime e instrumentar somente escritas cujo endereço de destino seja `0x83480CF4` (e, se necessário, os campos vizinhos do registro). Registrar PC/LR do PPC, valor escrito, thread e endereço efetivo. Antes de alterar, localizar a implementação real de `REX_STORE_U32`/store no SDK para evitar instrumentar macros erradas ou gerar logs massivos.
- Estado: causa funcional ainda não corrigida. O APK #103 é diagnóstico e não deve ser apresentado como versão funcional. Não solicitar novo teste do #103; o log já foi enviado e analisado.

## Investigação do caminho real de gravação PPC — 2026-11-04

- A análise do SDK confirmou que `REX_STORE_U8/U16/U32/U64` são macros definidos no template `resources/templates/codegen/pch_h.inja`, linhas 142–145, e não em `src/core/memory.cpp`.
- Os macros escrevem diretamente em `base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)`; U16/U32/U64 fazem byte-swap antes da gravação.
- O gerador PPC em `src/codegen/builders/memory.cpp` emite essas macros para instruções `stw`, `stwu`, `stwux`, `stwx` e operações de armazenamento correlatas.
- O template inclui `<rex/ppc.h>`, logging e headers padrão; o código gerado utiliza `PPCContext& ctx`, que contém `ctx.lr`. Isso permite registrar LR junto ao endereço e valor no ponto da gravação.
- O fluxo atual executa codegen no passo 151–163 do workflow e depois instrumenta funções geradas. A instrumentação anterior em `sub_8262C828` não encontrou a escrita do callback 53; não deve ser considerada prova de que essa função é a única via de gravação.
- Próximo passo: localizar com precisão o header PCH gerado pelo codegen e adicionar um watchpoint de software estreito nos macros de store, observando qualquer operação de 1/2/4/8 bytes que sobreponha o intervalo `0x83480CF4–0x83480CF7`. Registrar endereço efetivo, largura, valor, `ctx.lr` e valor antes/depois. Incluir também variantes MMIO apenas se o caminho puder atingir esse intervalo; ele não é um endereço MMIO.
- Não alterar a lógica do jogo, não registrar stub em `0x82738920` e não solicitar novo teste até existir APK de diagnóstico compilado com essa observação.
- Nenhum código de produção ou workflow foi alterado nesta etapa; foi feita apenas análise do caminho de geração/armazenamento.
