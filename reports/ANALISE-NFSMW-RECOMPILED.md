# Análise técnica: NFSMW Recompiled Mobile e aplicação ao Pac-Man

**Data:** 2026-11-03  
**Tipo:** relatório de pesquisa e plano de diagnóstico  
**Escopo:** comparação técnica; este documento não altera código nem dispara compilação.

## 1. Resumo executivo

O [NFSMW-Recompiled-Mobile](https://github.com/marcussilvacastrooutlok-afk/NFSMW-Recompiled-Mobile) é uma recompilação estática nativa de Need for Speed: Most Wanted (2005), versão Xbox 360. Seu fluxo traduz antecipadamente o código PowerPC do XEX para C++, compilado como código nativo, enquanto o ReXGlue fornece serviços de sistema Xbox e a camada gráfica.

A descoberta mais útil para Pac-Man não é o renderer de NFS, mas o método de investigar destinos indiretos não reconhecidos como entradas de função. O NFSMW mantém um analisador de lacunas (tools/huecos.py) e arquivos TOML de declarações manuais (app/huecos.toml e app/overrides.toml). O próprio projeto alerta que uma lacuna pode conter várias funções pequenas ou dados; declarar todo o intervalo como uma função pode esconder entradas internas e apenas deslocar o crash.

**Conclusão:** aproveitar a metodologia de análise, instrumentação, documentação e validação. Não copiar endereços, TOMLs, patches ou renderer do NFSMW para Pac-Man.

## 2. O que foi confirmado no NFSMW

### 2.1 Pipeline de recompilação

A documentação descreve: extração do XEX; codegen ReXGlue para analisar PowerPC e gerar C++; resolução manual de tabelas de salto, regiões de dados misturadas com código e limites de funções; compilação e ligação ao runtime; execução com logs para identificar imports/stubs e comportamentos ainda não implementados.

Referências:
- [README](https://github.com/marcussilvacastrooutlok-afk/NFSMW-Recompiled-Mobile/blob/main/README.md)
- [Fase de codegen](https://github.com/marcussilvacastrooutlok-afk/NFSMW-Recompiled-Mobile/blob/main/docs/02-codegen.md)
- [Runtime e stubs](https://github.com/marcussilvacastrooutlok-afk/NFSMW-Recompiled-Mobile/blob/main/docs/03-runtime.md)
- [Arquitetura](https://github.com/marcussilvacastrooutlok-afk/NFSMW-Recompiled-Mobile/blob/main/docs/arquitectura.md)

### 2.2 Ferramenta de lacunas

[tools/huecos.py](https://github.com/marcussilvacastrooutlok-afk/NFSMW-Recompiled-Mobile/blob/main/tools/huecos.py) mede o tamanho das funções geradas contando instruções PowerPC comentadas (4 bytes por instrução), compara os finais calculados com os inícios de codegen.partition.json e calcula intervalos não atribuídos. Também consulta endereços específicos com --comprobar.

Limitações explícitas no próprio script:
- uma lacuna pode conter uma função, várias funções pequenas/thunks ou dados;
- declarar apenas o início pode fazer o codegen absorver o intervalo inteiro e deixar entradas internas sem registro;
- subdividir indiscriminadamente em blocos de 8 bytes pode criar falsas entradas no meio de funções reais;
- cada candidato precisa de desassemblagem e evidência de execução.

[app/huecos.toml](https://github.com/marcussilvacastrooutlok-afk/NFSMW-Recompiled-Mobile/blob/main/app/huecos.toml) distingue funções independentes de continuações de função. [app/overrides.toml](https://github.com/marcussilvacastrooutlok-afk/NFSMW-Recompiled-Mobile/blob/main/app/overrides.toml) registra declarações manuais, opções de codegen e experimentos revertidos. Isso demonstra a importância de registrar o motivo e o resultado de cada override.

### 2.3 Patches rastreáveis

O NFSMW mantém patches em scripts separados, com aplicação, consulta de estado e reversão. A documentação registra ordem de aplicação e evidências de cada correção. Referência: [catálogo de patches](https://github.com/marcussilvacastrooutlok-afk/NFSMW-Recompiled-Mobile/blob/main/docs/parches.md).

### 2.4 Android e renderer

A documentação Android descreve port ARM64 com NDK, CMake, Gradle, SDL/integração Android, acesso aos arquivos do jogo e logs persistentes. O estado relatado foi testado em outro aparelho (Snapdragon 8 Elite/Adreno 830, Android 16) e ainda registra limitações, como ausência de controles táteis integrados e problemas de áudio e carregamento.

O projeto diferencia o caminho Xenos do renderer nativo feito sob medida para NFSMW. O renderer nativo substitui partes da emulação da GPU Xbox para o fluxo gráfico específico de NFS; não é um backend universal.

Referências:
- [Port Android](https://github.com/marcussilvacastrooutlok-afk/NFSMW-Recompiled-Mobile/blob/main/docs/android.md)
- [Pipeline gráfico nativo](https://github.com/marcussilvacastrooutlok-afk/NFSMW-Recompiled-Mobile/blob/main/docs/pipeline-nativo.md)
- [Gradle Android](https://github.com/marcussilvacastrooutlok-afk/NFSMW-Recompiled-Mobile/blob/main/android/app/build.gradle.kts)

## 3. Aplicação ao diagnóstico atual do Pac-Man

### 3.1 Evidências já registradas em CONTINUATION.md

- O crash ocorre durante o despacho de um callback indireto.
- O slot investigado é 0x83480CF4, associado ao índice 53.
- O destino encontrado imediatamente antes do despacho é 0x82738920.
- Esse endereço foi identificado como endereço interno de código/retorno em sub_827388A8, não como início de função. Não deve ser registrado artificialmente como função.
- A chamada indireta ocorre em 0x8262C908, dentro da rotina sub_8262C898.
- A rotina conhecida de cadastro sub_8262C828 grava registros com stride de 20 bytes.
- O chamador observado no cadastro é 0x82DA25A8.
- A investigação do artefato #82 não encontrou um segundo gravador explícito nos shards preservados, mas não excluiu caminhos em shards não preservados.

Isso aponta para origem/semântica do ponteiro ou preenchimento do registro, e não para uma função ausente que possa ser declarada sem análise.

### 3.2 Adaptação segura da metodologia NFSMW

1. Preservar as tags e instrumentação existentes: ReXCallbackReg, ReXCallback53 e ReXDispatch.
2. Inspecionar o shard preservado que contém 0x82DA25A8, identificar a função chamadora e desassemblar o trecho que chama sub_8262C828.
3. Seguir o valor passado em r4 para o cadastro e descobrir por que chega a 0x82738920.
4. Pesquisar todas as referências à base da tabela, ao slot 0x83480CF4, ao contador 0x83482808 e ao stride de 20 bytes. Distinguir gravação do registro, atualização do contador e alteração posterior do campo.
5. Adaptar a ideia de huecos.py somente após validar o formato real dos arquivos gerados do Pac-Man: comentários de instruções, nomes de funções e metadados de partição.
6. Para cada endereço candidato, verificar se é início de função, interior de função, thunk, continuação ou dados, cruzando desassemblagem e referências.
7. Só então fazer uma alteração mínima e reversível no manifesto/override/codegen ou na rotina responsável.
8. Confirmar workflow e APK antes de solicitar novo teste no aparelho.

### 3.3 Hipóteses que precisam ser distinguidas

- Ponteiro incorreto na origem: o chamador fornece um endereço interno/retorno em vez de uma entrada de callback.
- Registro sobrescrito ou parcialmente atualizado: o destino era correto ao cadastrar e mudou depois.
- Caminho de cadastro não instrumentado: contador ou slot atualizado por outra rotina.
- Interpretação incorreta da estrutura: campos, stride ou estado podem ter semântica diferente da inferida.

O próximo objetivo é decidir entre essas hipóteses com o shard e o fluxo de dados, não mascarar o trap.

## 4. Técnicas reaproveitáveis

| Técnica do NFSMW | Uso no Pac-Man |
|---|---|
| Medir início/fim das funções geradas | Localizar intervalos sem função atribuída |
| Consultar endereços específicos | Distinguir início, interior ou lacuna |
| Separar funções e continuações | Evitar declarar continuação como função independente |
| Documentar overrides e motivos | Manter histórico das correções do codegen |
| Instrumentar pontos específicos | Rastrear cadastro, origem do ponteiro e despacho |
| Patches idempotentes e reversíveis | Evitar alterações repetidas ou difíceis de desfazer |
| Ciclo build → log → hipótese → teste | Manter decisões baseadas em evidências |

## 5. O que não copiar

- Endereços, funções, tabelas e overrides do NFSMW: pertencem a outro XEX.
- Patches de XMA, áudio, privilégios, física ou streaming de NFS.
- Renderer Vulkan/OpenGL ES nativo de NFSMW.
- Suposições sobre tamanho de função ou formato de codegen sem validação no Pac-Man.
- Declaração automática de todas as lacunas como funções.

## 6. Plano de trabalho

**A — Recuperar o contexto:** abrir o shard com 0x82DA25A8, localizar a função chamadora e registrar o fluxo de r4.

**B — Auditar a tabela:** encontrar todas as referências de leitura/escrita da base, slot 53 e contador; identificar caminhos alternativos de cadastro.

**C — Adaptar o analisador:** gerar relatório de candidatos, sem criar overrides automaticamente. Cada item deve trazer endereço, função anterior, próxima função, tamanho da lacuna e evidência de instruções/código.

**D — Corrigir pontualmente:** corrigir a origem do callback ou a descoberta/registro da função, conforme a evidência; documentar reversão.

**E — Validar:** workflow concluído, APK publicado, teste do usuário e logs demonstrando que o índice 53 aponta para entrada válida e que a execução avança além do ponto anterior.

## 7. Limites e estado

Este relatório é análise comparativa e planejamento. **Não altera código, manifesto, runtime, workflow ou APK.** A causa do callback 53 permanece sem correção confirmada. O próximo passo é examinar o shard com 0x82DA25A8 e rastrear o valor fornecido ao cadastro.

## 8. Fontes

- [NFSMW README](https://github.com/marcussilvacastrooutlok-afk/NFSMW-Recompiled-Mobile/blob/main/README.md)
- [tools/huecos.py](https://github.com/marcussilvacastrooutlok-afk/NFSMW-Recompiled-Mobile/blob/main/tools/huecos.py)
- [app/huecos.toml](https://github.com/marcussilvacastrooutlok-afk/NFSMW-Recompiled-Mobile/blob/main/app/huecos.toml)
- [app/overrides.toml](https://github.com/marcussilvacastrooutlok-afk/NFSMW-Recompiled-Mobile/blob/main/app/overrides.toml)
- [docs/02-codegen.md](https://github.com/marcussilvacastrooutlok-afk/NFSMW-Recompiled-Mobile/blob/main/docs/02-codegen.md)
- [docs/03-runtime.md](https://github.com/marcussilvacastrooutlok-afk/NFSMW-Recompiled-Mobile/blob/main/docs/03-runtime.md)
- [docs/android.md](https://github.com/marcussilvacastrooutlok-afk/NFSMW-Recompiled-Mobile/blob/main/docs/android.md)
- [docs/parches.md](https://github.com/marcussilvacastrooutlok-afk/NFSMW-Recompiled-Mobile/blob/main/docs/parches.md)
- [docs/pipeline-nativo.md](https://github.com/marcussilvacastrooutlok-afk/NFSMW-Recompiled-Mobile/blob/main/docs/pipeline-nativo.md)
