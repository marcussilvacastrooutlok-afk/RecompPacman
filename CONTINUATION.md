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
