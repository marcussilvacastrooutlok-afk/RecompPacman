# CONTINUIDADE DO PROJETO — RecompPacman

Última atualização: 2026-11-06

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

## Watchpoint seletivo no caminho de armazenamento PPC — 2026-11-04

- Commit do workflow: `93606513c57a8f8957253a20eaa6cd4c2bf76cc0` (com commits intermediários de ajuste do script).
- O workflow agora modifica o PCH gerado `pacman_project/generated/default/pacman_colors_pch.h`, nome e caminho confirmados por `resources/templates/init/rexglue_cmake.inja` (a função `rexglue_apply_recomp_settings` aplica o header `pacman_colors_pch.h` ao alvo recompilado).
- A instrumentação troca temporariamente as macros PPC `REX_STORE_U8/U16/U32/U64` por wrappers que preservam endianess e escrita original, mas só registram operações que se sobrepõem ao intervalo do callback 53 `0x83480CF4–0x83480CF7`.
- Os logs `ReXMemWatch BEFORE/AFTER` registram endereço, largura, valor, `ctx.lr` e o valor atual do slot 53. As macros `REX_MM_STORE_U8/U16/U32/U64` são preservadas: MMIO continua usando `CheckStore`; o caminho não-MMIO delega aos novos wrappers para também ser observado.
- O pacote de diagnóstico agora inclui `pacman_colors_pch.h` e `watchpoint-pch.txt`, para confirmar o conteúdo efetivamente compilado.
- Lint do commit anterior `c4adad12ef46e329d3662413dda802ef34fd94e5` terminou com sucesso. Build #110, run ID `41951049942`, para o commit `93606513...`, foi iniciado e está em andamento no momento deste registro.
- Nenhum teste no telefone foi solicitado ainda. Aguardar o build mais recente; só entregar APK após confirmar compilação e artefatos.
- A observação cobre os stores PPC gerados por essas macros. Se o log não mostrar gravações, será necessário investigar caminhos alternativos de cópia/memset ou stores diretos, sem concluir prematuramente que o slot não foi escrito.


## Resultado do Workflow #110 — watchpoint PPC compilado (2026-11-06)

- Workflow Android #110, run ID `41951049942`, commit `93606513c57a8f8957253a20eaa6cd4c2bf76cc0`: `completed/success`.
- Todas as etapas passaram, incluindo geração do código, instrumentação da tabela de callbacks, watchpoint PPC, instrumentação de leitura e lista de inicialização, compilação ARM64, Gradle/SDL3 e publicação do APK.
- Lint #112, run ID `41951049882`: `completed/success`.
- Artefato diagnóstico `diagnostico-pacman`, ID `13384011106`; contém `pacman_colors_pch.h` e `watchpoint-pch.txt`. A inspeção confirmou os wrappers `REX_WATCHED_STORE`, stores U8/U16/U32/U64 e a delegação dos caminhos não-MMIO de `REX_MM_STORE`.
- Artefato APK `pacman-android-apk`, ID `13383641910`; APK extraído: `pacman-android-arm64-debug.apk`, 137.467.192 bytes.
- SHA-256 do APK #110: `061477bb839aaa93d09a55da686e0080cebd7358bbfa56cbbc4a64ef406cb3c8`.
- Inspeção do APK confirmou `lib/arm64-v8a/libmain.so`, `libSDL3.so` e `librexruntime.so`. A busca de strings em `libmain.so` encontrou `ReXMemWatch` e `ReXCallback53`, comprovando que o watchpoint está no binário nativo.
- Esta versão é um APK de diagnóstico: a compilação e a presença da instrumentação foram confirmadas, mas ainda não houve execução no aparelho e nenhuma correção funcional do jogo foi comprovada.
- Próxima etapa: analisar o primeiro log do APK #110, procurando `ReXMemWatch BEFORE/AFTER` e correlacionar endereço, largura, valor e LR com `ReXCallback53`/ `ReXDispatch`. Se não houver watchpoint, investigar cópias/memsets ou stores diretos que não passam pelos macros instrumentados. Não adicionar stub para `0x82738920`.
- APK #110 está pronto para o teste diagnóstico no Motorola; não é necessário repetir os testes dos APKs #99 ou #103.


## Análise do log do APK #110 — primeiro escritor localizado (2026-11-06)

- Arquivo recebido: `pacman-workflow110-full.txt`, 2.353 linhas; contém duas execuções do APK #110, PIDs 22768 e 22828.
- Nas duas execuções, o watchpoint capturou exatamente uma gravação de 4 bytes em `0x83480CF4`, com `value=0x82738920` e `ctx.lr=0x8262C748`; imediatamente antes o slot era zero e imediatamente depois passou a `0x82738920`.
- O evento ocorre antes da leitura fatal do callback 53. A leitura posterior confirma `count=63`, `slot=0x83480CF4`, `target=0x82738920`, `arg0=0x833AB458`, `arg5=0x82738AA0`, `state=0`; o dispatcher falha em `0x8262C908`.
- Nenhuma chamada `ReXCbWrite` com `r4_target=0x82738920` aparece; portanto, o alvo não foi gravado pelo caminho instrumentado de `sub_8262C828`.
- O endereço de LR `0x8262C748` pertence ao bloco da função convidada `sub_8262C740` (entrada `0x8262C740)); a função é declarada e registrada no mapa gerado. O tombstone confirma o crash no dispatcher `sub_8262C898`, não em SDL/JNI/memória de reserva.
- O artefato diagnóstico #110 não incluiu o shard de código que contém `sub_8262C740`; a tabela DWARF do `libmain.so` aponta para `pacman_colors_recomp.276.cpp`. A inspeção do assembly nativo mostra que essa função contém várias gravações instrumentadas, mas ainda não permite atribuir o evento a uma instrução PPC específica sem o fonte desse shard e os registradores completos.
- Alteração feita no workflow: commit `b069520265b93baba4866c32424be3135055a070`. O empacotamento de diagnósticos agora inclui shards que contenham `sub_8262C740`/`0x8262C748` e gera `callback53-writer-8262C740.txt` com contexto de código.
- Próxima etapa: examinar o fonte de `sub_8262C740` no novo artefato, identificar a instrução PPC exata e correlacionar registradores de base/valor. Não alterar a tabela, não substituir `0x82738920` por stub e não registrar `0x82738920` como função até confirmar a semântica da instrução.


## Análise do arquivo adicional diagnostico-pacman.zip — 2026-11-06

- O usuário informou que também havia baixado e anexou `diagnostico-pacman.zip`; o arquivo foi aberto e seu índice interno foi verificado.
- O ZIP contém 19 entradas, incluindo `caller-callback.txt`, `enderecos-trap.txt`, `pacman_colors_funcs.h`, `pacman_colors_init.cpp`, `pacman_colors_register.cpp`, `pacman_colors_pch.h`, os shards `pacman_colors_recomp.35.cpp`, `.64.cpp`, `.78.cpp`, `.82.cpp` e `.167.cpp`, além de `resumo.txt` e arquivos de build.
- O pacote inclui parte dos dados já analisados para o APK #110 e serve como referência complementar. Ele **não contém** `pacman_colors_recomp.276.cpp`, que é o shard necessário para examinar a implementação PPC de `sub_8262C740), apontada pelo watchpoint como contexto da gravação do callback 53.
- A alteração anterior no workflow, commit `b069520265b93baba4866c32424be3135055a070`, já amplia a coleta para incluir `sub_8262C740`/`0x8262C748` e deve incluir o shard ausente no próximo pacote de diagnóstico.
- Nenhuma nova conclusão sobre a instrução exata que grava `0x82738920` foi obtida apenas com este ZIP. Manter a investigação pendente até inspecionar `pacman_colors_recomp.276.cpp`; não adicionar stub nem registrar `0x82738920` como função.


## Workflow #111 — shard 276 recuperado e rotina de registro identificada (2026-11-06)

- Workflow Android #111, run ID `42320045436`, commit `b069520265b93baba4866c32424be3135055a070`: `completed/success`. Todas as etapas de geração, instrumentação, compilação ARM64, Gradle/SDL3 e publicação passaram.
- Artefato diagnóstico `diagnostico-pacman`, ID `13554755960`; artefato APK `pacman-android-apk`, ID `13554163465`. O pacote diagnóstico foi baixado e inspecionado.
- O pacote agora contém `pacman_colors_recomp.276.cpp` e `callback53-writer-8262C740.txt`, que estavam ausentes no pacote #110.
- No shard 276, a função `sub_8262C740` foi localizada. Ela lê o contador em `0x83482808`, calcula o deslocamento `contador * 20` e grava os campos de um registro de callback. A instrução `stwx r3,r7,r8` grava o valor de `r3` no campo de destino do registro; os demais stores gravam argumentos e estado.
- O endereço calculado para o campo de destino do índice 53 é `0x834808C8 + 8 + (53 * 20) = 0x83480CF4`, exatamente o endereço observado pelo watchpoint no APK #110. Isso identifica `sub_8262C740` como o caminho de registro que grava o target do callback 53; a investigação anterior monitorava apenas `sub_8262C828` e por isso não capturou esse caminho.
- O valor gravado vem de `r3`. A função salva o LR original do chamador em `r12` antes de chamar `__savegprlr_29`; depois essa rotina altera `ctx.lr` para `0x8262C748`. Portanto, o LR visto pelo watchpoint identifica o retorno interno da chamada auxiliar, não o chamador que forneceu o target.
- Foram encontrados 28 callsites estáticos para `sub_8262C740` nos shards presentes. Os exemplos examinados passam endereços diferentes em `r3`; ainda não foi identificado qual chamada, ou caminho dinâmico, fornece especificamente `0x82738920`.
- Novo passo preparado no workflow: registrar na entrada de `sub_8262C740` o contador, LR original salvo em `r12` e argumentos `r3-r6`, com filtro para contadores a partir de 50 ou target igual a `0x82738920`. Isso deve revelar o chamador real e o valor entregue em `r3` quando o índice 53 é preenchido.
- Commit da instrumentação de entrada: `7564d0b2ab8ff7f9dcc37f84b1ae07dd7034bf96`. Aguardando o próximo workflow para obter o log `ReXReg740`.
- Não adicionar stub nem registrar `0x82738920` como função. Não solicitar teste do APK #111: ele só incluiu o shard ausente para análise e não corrige o jogo.


## Análise do diagnóstico do Workflow #112 — caminho alternativo confirmado (2026-11-07)

- Workflow Android #112, run ID `42325930796`, commit `7564d0b2ab8ff7f9dcc37f84b1ae07dd7034bf96`: `completed/success`. Todas as etapas passaram, inclusive a instrumentação da entrada de `sub_8262C740`, compilação ARM64, empacotamento SDL3 e publicação do APK.
- Artefato de diagnóstico: `diagnostico-pacman`, ID `13557143001`; pacote baixado e extraído. Artefato APK: `pacman-android-apk`, ID `13557296125`.
- O pacote finalmente contém `pacman_colors_recomp.276.cpp` e `callback53-writer-8262C740.txt`, que faltavam nos pacotes anteriores.
- O código de `sub_8262C740` confirma o formato do registro de 20 bytes. Com base `0x834808C8) e contador `n`, grava `r5` no offset +0, `r6` em +4, `r3` em +8 (target), `r4` em +12 e zero em +16; depois incrementa o contador em `0x83482808`. Para índice 53, o target está em `0x834808C8 + 53*20 + 8 = 0x83480CF4`. Portanto, `sub_8262C740` é o caminho alternativo que escreve o callback 53, não apenas uma função próxima ao endereço observado.
- A origem do target passa por `r3) na entrada de `sub_8262C740`. O pacote mostra 28 callsites estáticos para essa rotina nos shards coletados; ainda não se identificou qual deles fornece `0x82738920` no registro de índice 53.
- **Correção importante da interpretação do LR:** o watchpoint anterior capturou `ctx.lr=0x8262C748` porque a própria função altera LR ao chamar `__savegprlr_29`. Esse valor é o retorno interno, não o chamador. Além disso, a instrumentação #112 imprimiu `ctx.r12` antes da instrução original `mflr r12`, portanto o campo chamado `caller_lr` no log #112 não é confiável como endereço do chamador.
- Para corrigir isso, o workflow foi atualizado no commit `27574144e0768e822b0993de436fa609b4ec154a`: a instrumentação de entrada agora imprime `ctx.lr`, que contém o LR real do chamador no momento de entrada, antes que o prólogo PPC o copie para `r12` e a chamada auxiliar altere LR. A captura continua registrando contador e `r3-r6), com filtro para contador >= 50 ou target igual a `0x82738920`.
- Próximo passo: analisar o Workflow #113 e seu log `ReXReg740`; correlacionar o registro de contador 53 com `caller_lr` correto e `target_r3`. Identificar o callsite e o fluxo que fornecem o endereço interno `0x82738920`; então corrigir a origem/semântica do registro, não criar stub.
- O APK #112 é diagnóstico, não uma correção funcional. Não solicitar instalação/teste dele: o próximo APK #113 deve conter o LR corrigido para capturar o chamador de forma válida.


## Resultado do APK #113 — chamador e origem literal do callback 53 (2026-11-07)

- O usuário enviou `pacman-reg740-113.txt` e o pacote `diagnostico-pacman.zip`. O log contém três execuções (PIDs 20495, 20665 e 20765), todas com o mesmo resultado.
- Em todas as execuções, as entradas 50–52 de `sub_8262C740` recebem `target_r3=0x8294D8D8`. Na entrada de índice 53, o log registra:
  - `caller_lr=0x83387778`
  - `target_r3=0x82738920`
  - `arg_r4=0x82738AA0`
  - `arg_r5=0`
  - `arg_r6=0x833AB458`
- O LR correto `0x83387778` mapeia para a instrução imediatamente após a chamada `bl 0x8262c740` dentro de `sub_83387708`, no shard `pacman_colors_recomp.100.cpp`. Isso identifica o chamador estático sem ambiguidade.
- O trecho PPC de `sub_83387708` monta explicitamente os argumentos:
  - `lis r8,-32140` seguido de `addi r3,r8,-30432`, resultando em `r3=0x82738920`.
  - `lis r9,-32140` seguido de `addi r4,r9,-30048`, resultando em `r4=0x82738AA0`.
  - `mr r6,r31` fornece `r6=0x833AB458` no registro 53.
  - Em seguida chama `sub_8262C740`.
- Portanto, o valor não é corrupção aleatória de memória nem uma sobrescrita posterior: o código recompilado de inicialização `sub_83387708` passa intencionalmente o endereço literal `0x82738920` como campo target do registro de callback 53.
- No shard `pacman_colors_recomp.167.cpp`, `0x82738920` é exatamente o LR/continuação após `bl 0x8234d1f0` no ramo de falha de `sub_827388A8` (entrada real `0x827388A8`). O endereço está no meio dessa função, não tem entrada `DEFINE_REX_FUNC` nem registro `SetFunction`. `0x82738AA0`, por outro lado, é uma entrada de função separada.
- O dispatcher `sub_8262C898` faz chamada indireta via `bctrl` ao target do registro. O registro contém `r6` em +4 e o dispatcher carrega esse campo em `r3` antes da chamada; no índice 53, o argumento passado em `r3` é `0x833AB458`.
- Interpretação segura: está confirmado que o inicializador registra como callback um endereço que é uma continuação interna de outra função, não uma entrada de função registrada. Ainda precisamos verificar a semântica de chamada/captura de contexto do ReXRuntime e o propósito dos dois endereços pareados antes de decidir se a correção é no codegen (representar uma continuação válida), na geração de funções/limites de bloco, ou na rotina de inicialização. Não adicionar stub simples nem registrar cegamente `0x82738920`: o bloco de continuação não tem prólogo independente e pode depender do contexto/registradores da função original.
- O pacote diagnóstico #113 contém os shards necessários, incluindo `pacman_colors_recomp.100.cpp`, `.167.cpp`, `.276.cpp`, além dos mapas de funções e inicialização. O log foi repetido três vezes e é consistente.
- Workflow #113: run ID `42332380242`, commit `27574144e0768e822b0993de436fa609b4ec154a`, resultado `completed/success`. APK artifact ID `13559334624`; diagnóstico artifact ID `13559184987`.
- Próxima etapa: inspecionar a implementação de `REX_CALL_INDIRECT_FUNC`/dispatcher e a semântica da rotina `sub_8234D1F0`; mapear referências e propósito de `sub_83387708`, `0x82738920` e `0x82738AA0`. Só então definir uma correção de recompilação com contexto correto. APK #113 é diagnóstico e o crash ainda não foi corrigido.


## Semântica confirmada do despacho indireto — continuação da análise #113 (2026-11-07)

- O header gerado `pacman_colors_pch.h` do artefato #113 contém a macro `REX_CALL_INDIRECT_FUNC`. Ela procura o ponteiro de função no dispatch table do módulo; se não estiver registrado, chama `rex::runtime::ResolveIndirectFunction`; em seguida executa diretamente `rex_indirect_func_(ctx, base)`.
- A macro não cria um novo `PPCContext`, não restaura automaticamente os registradores não voláteis e não reconstrói a pilha da função de origem. A função chamada recebe o mesmo contexto que o dispatcher está usando.
- Em `sub_8262C898`, o dispatcher usa `r31` como ponteiro para o registro e carrega `r3` do campo +4 antes do `bctrl`. Portanto, uma chamada direta para o bloco interno `0x82738920` não equivale a reentrar normalmente em `sub_827388A8`: o bloco não executa o prólogo que inicializa/salva `r31` e a pilha.
- Isso reforça que não é seguro resolver o crash simplesmente adicionando `SetFunction(0x82738920, ...)` ou criando um stub vazio. A solução precisa preservar a semântica do callback e o estado PPC esperado, ou corrigir a geração/representação do destino no caminho de inicialização.
- O artefato #113 contém declarações e registros para `sub_82738AA0`, mas não inclui a definição do seu corpo entre os shards coletados; o corpo de `sub_8234D1F0` também não está entre os arquivos-fonte selecionados. Não inferir o papel desses dois endereços nem se `sub_8234D1F0` retorna sem coletar os respectivos corpos ou analisar a fonte completa.
- Próxima ação de análise: localizar as definições completas de `sub_82738AA0` e `sub_8234D1F0` no codegen completo e rastrear a origem de `sub_83387708` (quem chama essa rotina e em que fase). Em paralelo, revisar como o gerador determina limites de função e se existe suporte a callbacks/continuações que apontam para blocos internos. Nenhuma correção funcional foi aplicada ainda.


## Análise do codegen e dispatcher após o APK #113 (2026-11-07)

- A inspeção do corpo gerado de `sub_827388A8` confirma que o endereço `0x82738920` é o LR definido pela instrução `bl 0x8234D1F0` em `0x8273891C`. O ramo de falha prepara dados na pilha e faz essa chamada; o corpo C++ gerado termina imediatamente depois da chamada, sem emitir um bloco de continuação separado para `0x82738920`. Isso, por si só, não determina se a rotina chamada é noreturn nem qual é o contrato do callback.
- A inspeção de `resources/templates/codegen/_indirect_call.inja` confirmou que `REX_CALL_INDIRECT_FUNC` procura o endereço exato na tabela de funções do módulo; se o slot estiver vazio ou fora do intervalo, chama `ResolveIndirectFunction`, e depois invoca o ponteiro PPC com o mesmo `PPCContext&` e a mesma base de memória.
- Em `src/system/function_dispatcher.cpp`, `ResolveIndirectFunction` chama `FunctionDispatcher::GetFunction(guest_address)`; `GetFunction` faz busca exata no mapa `function_table_`. Não existe resolução automática de um endereço interno para o início da função que o contém.
- Em `src/codegen/function_graph.cpp`, `FunctionGraph::classifyTarget` classifica um endereço como `InternalLabel` quando um branch/call estático aponta para um endereço dentro dos blocos da função chamadora. Isso é usado pelo gerador para emitir `goto loc_...` dentro da função C++; não cria uma entrada independente no dispatch table. Um ponteiro de callback dinâmico como `0x82738920` não passa por essa classificação estática.
- `FunctionNode::emitCpp` gera um `PPCFunc` por nó/função e labels internos como labels C++ locais. Assim, o código atual explica por que um endereço interno/continuação pode existir como valor PPC, mas não ser invocável pelo dispatcher como função registrada.
- Ainda não está demonstrado se `0x82738920` deveria ser um continuation legítimo que exige suporte explícito do codegen, ou se a rotina `sub_83387708` está montando incorretamente um callback em um caminho de erro. Não adicionar uma entrada artificial nem remapear para `sub_827388A8`: isso poderia executar o prólogo inteiro de novo com contexto já alterado.
- O pacote #113 não contém os corpos completos de `sub_8234D1F0` e `sub_82738AA0`; esses corpos são necessários para interpretar a chamada e o par de endereços do registro 53.
- Commit do workflow para ampliar a coleta: `eb7c4abf1c915d2da81a32f204cd4f042eec9a70`. O próximo artefato diagnóstico deve incluir shards que contenham `sub_8234D1F0`, `sub_82738AA0`, `sub_827388A8` e `sub_83387708`, além de `callback53-continuations.txt`.
- Próxima etapa: aguardar o build acionado pelo commit de coleta; examinar os corpos completos das duas funções e a instrução/código em torno de `0x82738920`. Só depois escolher entre corrigir a origem do callback ou acrescentar suporte correto a continuations internas. APK #113 continua sendo diagnóstico; nenhuma correção funcional foi aplicada.


## Resultado do Workflow #114 — corpos das funções recuperados (2026-11-07)

- Workflow Android #114, run ID `42376075002`, commit `eb7c4abf1c915d2da81a32f204cd4f042eec9a70`: `completed/success`. Todas as etapas de geração, instrumentação, compilação ARM64, Gradle/SDL3 e publicação passaram.
- Artefato APK: `pacman-android-apk`, ID `13576957743`.
- Artefato diagnóstico: `diagnostico-pacman`, ID `13576803309`; ZIP baixado e inspecionado. Inclui os shards `.100.cpp`, `.167.cpp`, `.181.cpp`, `.34.cpp`, além de `callback53-continuations.txt`, mapas e relatórios.
- Em `sub_827388A8` (entrada `0x827388A8`), o caminho de sucesso aloca 44 bytes e inicializa os campos do nó. No caminho de falha, constrói na pilha um pequeno objeto/descritor (campos em `r1+80` e `r1+84`) e chama `sub_8234D1F0` com `r3=r1+80`. O LR dessa chamada é exatamente `0x82738920`.
- O corpo completo de `sub_8234D1F0` foi localizado no shard `pacman_colors_recomp.181.cpp`. Ele:
  1. guarda o argumento em `r31);
  2. lê um ponteiro global e, se não nulo, chama-o indiretamente;
  3. lê o objeto apontado pelo primeiro campo de `r31`, obtém o método no offset +8 da vtable e chama-o indiretamente;
  4. zera `r3-r7` e chama `sub_832CE868`.
- O C++ gerado para `sub_8234D1F0` termina imediatamente após essa chamada a `sub_832CE868`, sem epílogo ou retorno visível. Isso é compatível com uma rotina terminal (por exemplo, caminho de exceção/falha), mas **não prova** que `sub_832CE868` seja noreturn: a definição desse helper não veio no ZIP. A declaração e o registro do símbolo aparecem nos arquivos agregados, mas não o corpo.
- O corpo completo de `sub_82738AA0` foi localizado no shard `pacman_colors_recomp.34.cpp`. Ele lê dois ponteiros do objeto recebido; se o primeiro for não nulo, chama `sub_82457C28` com os campos internos; depois chama `sub_82457C28` com o segundo ponteiro; finalmente zera o primeiro campo e retorna. Portanto, é uma rotina de limpeza/liberação de dois recursos, mas o tipo exato dos recursos ainda não está identificado.
- A função `sub_83387708` confirma que os valores `0x82738920` e `0x82738AA0` são montados explicitamente e passados ao registrador `sub_8262C740`. O primeiro é o endereço de retorno/continuação após a chamada de falha em `sub_827388A8`; o segundo é uma entrada de função independente que executa a limpeza descrita acima.
- A nova evidência estreita a hipótese: o callback parece combinar um endereço de continuação de caminho de falha com uma rotina de limpeza, mas ainda não se sabe se isso representa um callback/continuation válido que o codegen não modelou, ou se a rotina de inicialização está registrando um destino inadequado. Não concluir que o helper é noreturn sem examinar `sub_832CE868` e o código PPC bruto na região `0x82738920`.
- O artefato #114 é diagnóstico. Não pedir instalação/teste no telefone: ainda não existe correção funcional demonstrada.

### Próxima etapa após #114

1. Ampliar a coleta para preservar a definição completa de `sub_832CE868` (o shard que a contém), e gerar um relatório específico sobre seus chamadores e comportamento.
2. Obter/inspecionar as instruções PPC brutas em `0x82738908–0x82738930`, principalmente a sequência após o `bl 0x8234D1F0`, para determinar se há código executável de continuação e qual contexto ele exige.
3. Rastrear a função de limpeza `sub_82738AA0` e o registrador `sub_83387708` até o chamador/fase de inicialização, para identificar o contrato do registro.
4. Só então decidir se a correção pertence ao codegen (suporte a continuations/callbacks internos) ou ao caminho de inicialização. Não adicionar stub, não mapear `0x82738920` para `sub_827388A8` e não alterar o runtime às cegas.


## Próxima coleta — workflow #115 (2026-11-07)

- Após analisar o artefato #114, o workflow foi ajustado para também preservar o shard que contém `sub_832CE868`:
  - relatório de contexto agora procura a definição e referências de `sub_832CE868`;
  - lista de shards preservados inclui `sub_832CE868` e `832CE868`.
- Commit do workflow: `cd5cc44f8d11ffc9f76ebca67c18918cdea03eb6`.
- Build Android #115, run ID `42382886957`: iniciado; job `128434051006` estava `in_progress` no primeiro acompanhamento.
- Lint #127, run ID `42382886938`: iniciado; job `128434051122` estava `in_progress`.
- Atualização deste documento após o resultado #114: commit `2f12f761e24bd7b5380c504a07564a1117fcbc51`.
- Próximo passo: acompanhar #115 e #127. Se o build passar, verificar se o pacote agora contém o corpo de `sub_832CE868`; não solicitar teste no telefone, pois esta continua sendo uma coleta diagnóstica.


## Resultado do Workflow #115 — helper fatal identificado (2026-11-07)

- Workflow Android #115, run ID `42382886957`, commit `cd5cc44f8d11ffc9f76ebca67c18918cdea03eb6`: `completed/success`. Todas as etapas, incluindo geração do código, compilação ARM64, empacotamento SDL3 e publicação do APK, passaram.
- Artefato APK: `pacman-android-apk`, ID `13579988420`.
- Artefato diagnóstico: `diagnostico-pacman`, ID `13579768856`; baixado e inspecionado. O shard `pacman_colors_recomp.110.cpp` contém a definição completa de `sub_832CE868`.
- Corpo confirmado de `sub_832CE868`:
  1. cria seu frame de pilha;
  2. chama `sub_832CE7C8` com `r3=2`, `r4=0xC0000417` e `r5=1`;
  3. configura `r3=30`;
  4. chama `__imp__KeBugCheck`.
  Não há epílogo nem retorno após `KeBugCheck`.
- Isso confirma que a cadeia `sub_827388A8 -> sub_8234D1F0 -> sub_832CE868 -> KeBugCheck(30)` é um caminho fatal, não uma continuação de retorno normal. Assim, o endereço `0x82738920` é o LR/endereço de retorno após a chamada para `sub_8234D1F0`, mas o caminho chamado termina em bugcheck. Não devemos tratar `0x82738920` como uma função comum nem inventar um stub para fazê-la retornar.
- A conclusão ainda não explica por que `sub_83387708` registra esse LR como target no slot 53. O próximo ponto é entender o contrato do registro e em quais circunstâncias o dispatcher chama o target. A hipótese a testar é que o registro representa um handler/callback de falha cuja invocação leva deliberadamente ao bugcheck; outra possibilidade é que o registrador ou a interpretação do tipo de callback estejam incorretos.
- Não há correção funcional implementada neste build. O APK #115 é apenas diagnóstico; não solicitar instalação/teste no telefone.

### Próximas investigações

1. Inspecionar `sub_83387708` integralmente e todos os seus chamadores para identificar a fase e o propósito do registro.
2. Inspecionar `sub_8262C740`, `sub_8262C898` e o consumidor do slot 53, incluindo condições, ordem dos argumentos e contexto PPC usado na invocação.
3. Obter disassembly/instruções PPC brutas em `0x827388A8–0x82738930` e confirmar a sequência do caminho fatal.
4. Identificar o significado do valor de bugcheck `30` no contexto Xbox/kernel a partir de evidência apropriada; não inferir o motivo específico apenas pelo número.
5. Só depois decidir se existe defeito de registro/dispatch ou se o jogo está acionando corretamente um caminho de falha por uma condição anterior. Não alterar o runtime às cegas.


## Dispatcher do callback 53 — fluxo de execução identificado (2026-11-07)

- A inspeção do artefato #115 confirmou o corpo de `sub_8262C898`: percorre a tabela de callbacks, lê o target em cada registro, ignora target nulo e registros cujo byte de estado em `+8` não seja zero, carrega o argumento de `-4` em `r3`, move o target para `CTR` e executa `bctrl`.
- Para o registro 53, o dispatcher chama o target `0x82738920` com `r3=0x82738AA0`. Isso confirma que a falha não é apenas a leitura de um ponteiro: o dispatcher efetivamente tenta invocá-lo como função PPC.
- Foram encontrados dois chamadores diretos de `sub_8262C898` no código gerado:
  - `sub_8256DE18`, no caminho de inicialização global, após chamadas a `sub_82458BA8`, `sub_8260B048` e `sub_8260BC70`; chama o dispatcher em `0x8256DE5C` e depois retorna.
  - `sub_824497D8`, no caminho de inicialização de um objeto, após `sub_82449A00); chama o dispatcher em `0x824497FC`, depois inicializa o campo em `+3088` e marca o objeto em `+3860`.
- A função `sub_83387708` registra explicitamente o target `0x82738920` e o argumento `0x82738AA0`, mas nenhum chamador direto foi encontrado nos shards preservados; o endereço consta na tabela de funções registradas, então pode ser invocado indiretamente.
- Essa evidência aponta para o registro de uma entrada interna de função que o dispatcher chama como callback comum. Ainda precisamos observar em execução qual dos dois chamadores aciona o slot 53, o estado real do registro, o LR de entrada e o contexto de pilha/registradores antes do `bctrl`.
- O helper fatal `sub_832CE868` chama `__imp__KeBugCheck` com código 30 e não apresenta retorno no código gerado. Isso sugere que o endereço `0x82738920` é a continuação após uma chamada que, no caminho analisado, não retorna normalmente. Não assumir que a execução desse endereço seja válida como callback comum.
- Instrumentação adicional foi adicionada ao workflow para registrar a entrada de `sub_83387708`, `sub_8256DE18`, `sub_824497D8` e `sub_8262C898`, além de registrar `r1`, `r3`, `CTR`, LR e dados do slot 53 imediatamente antes do `bctrl`.
- Commit do workflow de diagnóstico #116: `62d658add04e7452df8752640230b4b8aafb0d6f`.
- Build #116, run ID `42391845264`: iniciado, job `128460869679` em andamento no primeiro acompanhamento.
- Lint #130, run ID `42391845268`: iniciado, job `128460869663` em andamento no primeiro acompanhamento.
- Não solicitar instalação do APK #116 antes de verificar se a instrumentação compilou. Se o build passar, este APK será necessário para um único teste direcionado: abrir o jogo e capturar logcat do crash, sem repetir testes anteriores desnecessariamente.

## Atualização de 2026-11-07 — Workflow #116 e correção da instrumentação

- O Workflow Android #116 (run ID `42391845264`, commit `62d658add04e7452df8752640230b4b8aafb0d6f`) terminou com falha no passo **Instrumentar origem e contexto do dispatcher de callbacks**.
- A falha foi exclusivamente de sintaxe Python no script que injeta os logs: strings de quebra de linha estavam escapadas duas vezes e a construção de uma f-string com aspas triplas ficou inválida (`SyntaxError: unexpected character after line continuation character`).
- As etapas anteriores, incluindo checkout, compilação/instalação do ReXGlue host, inicialização do projeto, geração do código do Pac-Man e instrumentações anteriores, foram concluídas. As etapas Android e publicação do APK foram ignoradas após a falha.
- O artifact `diagnostico-pacman` foi publicado (ID `13581667554`), mas não há APK #116 para testar.
- Corrigido o passo para montar o diagnóstico com concatenação explícita de strings Python, usando `"\n"` e `"\t"` corretamente. A correção foi commitada em `6adb899069d36aa2453754712d35e8f42fe5cade` no arquivo `.github/workflows/recompilar-pacman.yml`.
- Próximo passo: verificar o novo workflow disparado pelo commit `6adb899...`. Se a instrumentação passar, confirmar que os quatro pontos (`sub_83387708`, `sub_8256DE18`, `sub_824497D8`, `sub_8262C898`) receberam logs e só então permitir que o build prossiga até gerar o APK.
- Não instalar APK #116 (não foi gerado) nem alterar a lógica de callback/runtime. Continuar sem registrar stub para `0x82738920`.


## Atualização de 2026-11-07 — Build #117 e instrumentação confirmada

- Build Android #117, run ID `42411843613`, commit `6adb899069d36aa2453754712d35e8f42fe5cade`: **concluído com sucesso**. Todas as etapas do job `128521458816` passaram, incluindo geração do código, instrumentação, compilação ARM64, empacotamento SDL3/JNI e publicação do APK.
- A etapa **Instrumentar origem e contexto do dispatcher de callbacks** passou.
- Artifact `diagnostico-pacman`: ID `13591901212`, ZIP de 21,6 MB. A inspeção confirmou os logs de entrada em todos os quatro pontos:
  - `sub_83387708` / tag `ReXCbRegister`, shard `.100.cpp`;
  - `sub_8256DE18` / tag `ReXCbDispatchCaller`, shard `.64.cpp`;
  - `sub_824497D8` / tag `ReXCbObjectCaller`, shard `.82.cpp`;
  - `sub_8262C898` / tag `ReXCbDispatchEntry`, shard `.35.cpp`.
- O shard `.35.cpp` também contém a instrumentação `ReXCallback53` imediatamente antes do `REX_CALL_INDIRECT_FUNC`, registrando count, slot, target, arg0, state, LR, r1, r3 e CTR.
- Isso valida a instrumentação no código-fonte gerado e compilado. Ainda não temos os valores reais desses logs em execução; o diagnóstico ZIP preserva os fontes instrumentados, não o logcat de uma execução no telefone.
- Artifact APK: `pacman-android-apk`, ID `13591940971`, ZIP de 136,6 MB contendo `pacman-android-arm64-debug.apk` (137.471.288 bytes).
- Próximo passo: solicitar um único teste do APK #117 no Motorola: instalar, abrir, selecionar a pasta do jogo já usada anteriormente e iniciar até ocorrer o comportamento (abrir ou fechar). Depois coletar apenas as tags `ReXCbRegister`, `ReXCbDispatchCaller`, `ReXCbObjectCaller`, `ReXCbDispatchEntry` e `ReXCallback53` via logcat para identificar a origem real do callback 53 e o contexto de execução.
- Não alterar ainda ReXRuntime/codegen nem registrar stub para `0x82738920`; aguardar evidência runtime.


## Teste real do APK #117 — falha reproduzida (2026-11-07)

- O usuário instalou/testou o APK #117 e confirmou que o comportamento continua igual: ao tocar em **Iniciar jogo**, o aplicativo retorna à tela inicial/fecha, sem iniciar o Pac-Man.
- Portanto, o build #117 é tecnicamente bem-sucedido, mas **não corrigiu o defeito funcional**. A instrumentação adicionada foi validada estaticamente no artefato, porém ainda não foram obtidos os valores runtime.
- Não marcar o problema como resolvido e não criar outra alteração especulativa no dispatcher/codegen.
- Próxima ação: obter do aparelho o logcat da execução #117 filtrado pelas tags ReXCbRegister, ReXCbDispatchCaller, ReXCbObjectCaller, ReXCbDispatchEntry e ReXCallback53. Usar esses registros para identificar qual chamador aciona o dispatcher, o contexto PPC e os valores efetivos do slot 53.
- Depois de analisar o log real, decidir a correção baseada em evidência. Não registrar stub para 0x82738920 nem remapear esse endereço para sub_827388A8.


## Logcat real do APK #117 — callback 53 confirmado em execução (2026-11-07)

- O usuário gerou `pacman-117-logcat-completo.txt`; o arquivo filtrado anterior ficou com 0 bytes porque as tags solicitadas inicialmente não correspondiam a todas as tags emitidas pelo build. A captura completa encontrou os registros.
- O log real confirma a execução de `ReXCbRegister`, `ReXReg740`, `ReXMemWatch`, `ReXCbDispatchCaller`, `ReXCbDispatchEntry`, `ReXCallback53` e `ReXDispatch`.
- Registro do callback: `ReXReg740: ENTRY count=53 caller_lr=0x83387778 target_r3=0x82738920 arg_r4=0x82738AA0 arg_r5=0 arg_r6=0x833AB458`.
- Escrita confirmada: `ReXMemWatch AFTER addr=0x83480CF4 value=0x82738920 slot53=0x82738920`. Portanto, o endereço já é fornecido ao registrador e gravado no slot; não é uma corrupção posterior do slot demonstrada pelo log.
- No despacho: `count=63 slot=0x83480CF4 target=0x82738920 arg0=0x833AB458 state=0 caller_lr=0x8262C90C dispatch_r1=0x7010FBA0 dispatch_r3=0x833AB458 dispatch_ctr=0x82738920`.
- `ReXDispatch` confirma o trap no destino `0x82738920`, com callsite provável `0x8262C908`, CTR/R11 apontando para `0x82738920`, R31 contendo o endereço do slot e R3=`0x833AB458`.
- Observação: o log de registro apresenta `arg_r4=0x82738AA0`, enquanto a leitura imediatamente antes do despacho apresenta `arg0=0x833AB458`. Não assumir que esses campos representam o mesmo argumento: o log indica que o dispatcher está usando `0x833AB458` como R3 nesse ponto. Revisar o layout efetivo do registro e o código gerado em `sub_8262C740`/`sub_8262C898` antes de interpretar a discrepância.
- A tag `ReXCbWrite` aparece em vários registros com `caller_lr=0x82DA25A8`; porém o registro específico de callback 53 está instrumentado separadamente por `ReXReg740` e `ReXMemWatch`. O registro 53 foi explicitamente passado por `sub_83387708`, com LR de chamada `0x83387778`.
- O log confirma a origem imediata do target inválido: `sub_83387708` fornece explicitamente `0x82738920` ao registrador. Isso não determina ainda se a intenção é uma continuação/handler de falha ou se há erro no registro/dispatch. A cadeia de código previamente analisada termina em `KeBugCheck(30)`; não criar stub nem remapear o endereço para `sub_827388A8`.
- O primeiro grep do usuário encontrou também um crash antigo às 19:33:18 e uma execução instrumentada às 19:39:28. O conjunto de linhas 12776–12831 corresponde à execução instrumentada posterior; usar essa sequência como evidência principal.
- Próxima etapa: correlacionar `sub_83387708` com o chamador e a fase em que é executada; revisar a assinatura/layout real do registro (incluindo qual campo é carregado em R3) e comparar a semântica do dispatcher com o contrato esperado desse tipo de callback. Nenhuma correção funcional foi aplicada; o APK #117 continua falhando.


## Revisão do layout do registro 53 com o artefato diagnóstico #117 (2026-11-07)

- O ZIP diagnóstico #117 foi baixado e inspecionado localmente. Os corpos completos de `sub_83387708`, `sub_8262C740`, `sub_8262C898` e `sub_827388A8` foram conferidos nos shards.
- Correção de interpretação importante: os valores `arg_r4=0x82738AA0` no log `ReXReg740` e `arg0=0x833AB458` no log `ReXCallback53` não são argumentos contraditórios. A rotina `sub_8262C740` grava o registro com este layout: offset +0 recebe r5; +4 recebe r6; +8 recebe r3 (destino); +12 recebe r4; +16 recebe o estado zero.
- O dispatcher aponta r31 para o campo +8, lê o target em +0 relativo a r31 e carrega o argumento de chamada em `r3) a partir de `-4(r31)`, ou seja, do campo +4 do registro. Logo, o argumento entregue ao callback é `r6=0x833AB458`; `r4=0x82738AA0` permanece no campo +12 e não é o argumento usado pelo dispatcher nesse caminho.
- O corpo de `sub_83387708` confirma a montagem explícita dos parâmetros: `r3=0x82738920`, `r4=0x82738AA0`, `r5=0), `r6=r31), seguido de chamada a `sub_8262C740` com LR `0x83387778`.
- O corpo de `sub_827388A8` confirma que `0x82738920` é a instrução imediatamente após `bl 0x8234D1F0` no bloco de falha de alocação. O caminho alternativo de sucesso executa o epílogo e retorna; o bloco de falha em `0x82738908` prepara um descritor na pilha e chama `sub_8234D1F0`. O endereço registrado é o LR dessa chamada, não o início de `sub_827388A8`.
- A inspeção de `sub_8262C740` mostra que a tabela é uma estrutura de registros com cinco campos, e o dispatcher `sub_8262C898` invoca o destino com o campo +4 como único argumento visível nessa chamada. O campo +12 é armazenado, mas não é consumido pelo trecho do dispatcher analisado.
- O relatório do artefato mostra 29 call-sites gerados para `sub_8262C740`, em funções de inicialização `sub_8338...`; portanto, `sub_83387708` é um entre vários registradores desse padrão. Ainda não foi demonstrado o contrato semântico completo da tabela nem por que o destino de uma das entradas é o LR interno `0x82738920`.
- Não tratar mais a diferença entre `0x82738AA0` e `0x833AB458` como discrepância de argumentos: o layout do registro a explica. A questão em aberto continua sendo por que o destino `0x82738920`, que não é início de função registrada, é passado como callback e depois invocado pelo dispatcher.
- Próxima investigação: comparar o padrão dos outros 28 call-sites de `sub_8262C740`, determinar o papel dos campos +0/+4/+12/+16 e localizar o contrato/uso da tabela (incluindo quem chama `sub_8262C898`). Só depois decidir se o codegen precisa representar um entrypoint de continuação ou se a origem está registrando o endereço errado. Nenhuma correção funcional foi aplicada; não iniciar build nem pedir teste no telefone nesta etapa.


## Comparação dos call-sites de sub_8262C740 — artefato #117 (2026-11-07)

- Foram revisados os call-sites preservados nos shards do artefato #117. O padrão de chamada é consistente: r3 recebe o destino registrado, r4 recebe um segundo endereço/descritor, r5 recebe um valor de controle (normalmente 0; há um caso com 1) e r6 recebe o contexto/objeto (frequentemente r31).
- Nos destinos não nulos examinados, vários endereços coincidem exatamente com entradas geradas como funções, por exemplo 0x823610A0, 0x8236EB08, 0x82572418, 0x825D0CC8 e 0x82447670. Há também registros com r3=0, que o dispatcher ignora.
- O call-site de sub_83387708 é diferente: r3=0x82738920, e esse endereço não possui uma definição própria DEFINE_REX_FUNC nos shards. Ele cai no interior de sub_827388A8, imediatamente após a chamada a sub_8234D1F0 no caminho fatal de alocação. Portanto, o callback 53 é uma exceção ao padrão dos destinos comuns observados.
- O segundo parâmetro r4 varia entre os registros e é armazenado no campo +12. O trecho de sub_8262C898 analisado não o carrega para a chamada indireta; o argumento visível do callback continua sendo r6, armazenado em +4. O significado de +12 ainda não está demonstrado.
- O dispatcher tem dois chamadores diretos preservados: sub_824497D8 chama sub_8262C898 durante a inicialização de um objeto; sub_8256DE18 chama-o durante a inicialização global. Isso mostra dois contextos de despacho, mas ainda não identifica qual deles encontra/invoca o slot 53 na execução observada.
- Conclusão provisória: não há evidência de que todos os registros sejam callbacks de um único formato simplificado; o padrão comum é destino + contexto, mas o slot 53 aponta para um endereço interno de caminho fatal, não para uma entrada de função como os exemplos acima. A semântica do campo +12 e a razão de esse destino ser registrado continuam em aberto.
- Próximas análises: (1) comparar o comportamento dos destinos registrados e seus primeiros blocos, especialmente os casos em que não há definição exata; (2) examinar o que consome o campo +12 fora do loop de despacho; (3) determinar qual dos dois chamadores aciona o despacho do slot 53 e em que estado/contexto; (4) avaliar o fluxo de inicialização que leva a sub_83387708. Não alterar runtime/codegen, não criar stub para 0x82738920 e não iniciar build/teste do APK até esclarecer o contrato.


## Padrão dos destinos comuns — hipótese de callbacks de limpeza (2026-11-07)

- Foi inspecionado o início de várias funções usadas como destinos não nulos em call-sites de sub_8262C740: sub_823610A0, sub_8236EB08, sub_82572418, sub_825D0CC8, sub_82447670, sub_82472E30 e sub_82460098.
- Essas rotinas têm um padrão repetido: recebem o objeto em r3, leem o primeiro campo, retornam se ele já estiver preenchido/não nulo e, caso contrário, desviam para uma rotina próxima de limpeza/destruição (por exemplo, sub_823610A0 -> sub_82361020; sub_8236EB08 -> sub_8236EA40; sub_82572418 -> sub_82572378).
- Isso é compatível com thunks de destruição/limpeza de objetos e sugere que a tabela pode representar callbacks de finalização/limpeza associados a objetos. É uma inferência baseada no padrão dos corpos examinados; o nome/contrato formal da tabela ainda não foi localizado.
- O callback 53 continua excepcional: target=0x82738920 não tem definição própria e é o endereço de retorno interno no caminho que chama sub_8234D1F0, cuja cadeia foi demonstrada como fatal via KeBugCheck(30). O argumento entregue pelo dispatcher é o contexto em r6 (0x833AB458), não o endereço de limpeza em r4 (0x82738AA0).
- O papel de r4/campo +12 permanece desconhecido; o trecho de despacho analisado não o usa. Não concluir que ele seja outro destrutor sem localizar um consumidor.
- Próxima investigação: examinar os call-sites e os objetos envolvidos em sub_83387708, especialmente por que uma rotina de caminho fatal foi registrada no mesmo mecanismo que contém thunks de limpeza. Verificar também referências/consumidores dos campos +12 e +16 e o momento em que o dispatcher percorre a tabela.
- Nenhuma correção funcional foi aplicada. Não criar stub para 0x82738920, não remapear o endereço para sub_827388A8 e não iniciar novo build/teste até entender a semântica do registro.


## Origem indireta de sub_83387708 e comparação com inicializador vizinho (2026-11-07)

- Nova inspeção dos shards #117 confirmou que não existe chamada direta textual a sub_83387708 nos fontes gerados preservados; a função consta na tabela de funções registradas, mas sua execução ocorre pelo iterador de inicialização indireta.
- O logcat runtime já mostrou caller_lr=0x82DA25A8 ao entrar em sub_83387708. O corpo de sub_82DA25A8 percorre a lista global de inicializadores em 0x833A0F44 e chama cada endereço não nulo por CTR/bctrl. Portanto, sub_83387708 é executada como uma rotina de inicialização global/lista de inicializadores, não por chamada direta comum.
- A função vizinha sub_83387680, no mesmo grupo de rotinas de inicialização, tem estrutura quase idêntica: inicializa dois membros com sub_832CE8D0, zera flags e registra um callback via sub_8262C740. Seu target é 0x8294D8D8 e seu segundo endereço é 0x8294D8F0.
- O endereço 0x8294D8D8 está presente na tabela SetFunction e possui declaração de função gerada. Assim, esse inicializador vizinho fornece um exemplo de callback com destino que é uma entrada de função registrada.
- Em contraste, sub_83387708 registra target 0x82738920 e campo secundário 0x82738AA0. O segundo endereço é uma função registrada (sub_82738AA0); o primeiro é apenas o LR após a chamada a sub_8234D1F0 dentro do bloco fatal de sub_827388A8 e não aparece como entrada SetFunction.
- Isso fortalece a conclusão de que o problema não é uma falha geral do dispatcher nem da lista de inicializadores: há pelo menos um inicializador vizinho que registra um target normal pelo mesmo mecanismo. A anomalia específica está no target montado por sub_83387708 ou na classificação/recuperação do bloco de código correspondente.
- Ainda não está provado se 0x82738920 deveria ser uma função independente omitida pela análise/codegen, um endereço de continuação de exceção deliberado, ou um ponteiro incorreto produzido pelo binário/metadata. A sequência gerada para sub_827388A8 termina no helper fatal, então é necessário obter o código PPC bruto do XEX nessa região para responder com segurança.
- Próximas etapas: comparar a sequência PPC e os limites de função no XEX em 0x827388A8–0x82738930; comparar outros inicializadores vizinhos que registram callbacks; verificar se 0x82738920 aparece em metadados/relocações como entrypoint separado. Não adicionar stub, não remapear o endereço e não iniciar build/teste antes de confirmar o significado do destino.


## Ressalva crítica — possível função omitida após chamada noreturn (2026-11-07)

- A ausência de 0x82738920 na tabela SetFunction e a ausência de DEFINE_REX_FUNC próprio nos shards preservados não provam, por si só, que o endereço não seja uma entrada de função válida.
- O código gerado de sub_827388A8 termina exatamente após a chamada a sub_8234D1F0 em 0x82738920. Como sub_8234D1F0 chega a KeBugCheck(30) e não apresenta retorno, o gerador/tradutor pode ter classificado a chamada como noreturn e encerrado a emissão/análise do bloco naquele endereço.
- Portanto, 0x82738920 pode ser (a) apenas uma continuação sem código válido, (b) uma função independente/entrypoint adjacente omitida pela análise de fluxo por causa da chamada noreturn, ou (c) outro tipo de entrypoint especial. A evidência atual não distingue essas possibilidades.
- A hipótese de função independente omitida é agora prioritária para verificação, porque sub_83387708 fornece explicitamente 0x82738920 como destino de callback, enquanto um inicializador vizinho registra 0x8294D8D8, que consta normalmente em SetFunction.
- Próxima ação recomendada: obter os bytes/instruções PPC reais do guest em 0x82738908–0x82738940 (preferencialmente por instrumentação de leitura da memória guest no runtime, sem precisar extrair o XEX manualmente no telefone). Verificar se há instruções válidas após o bl e se 0x82738920 inicia prólogo/corpo próprio. Em seguida comparar a função/entrypoint com o mapa de funções registrado e corrigir o codegen para preservar esse entrypoint se confirmado.
- Não concluir que 0x82738920 é inválido apenas por ausência em SetFunction; não criar stub artificial, não remapear para sub_827388A8 e não alterar dispatcher antes de obter os bytes PPC reais.
