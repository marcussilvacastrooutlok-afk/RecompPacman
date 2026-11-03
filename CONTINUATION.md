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
