---
description: Retoma o projeto de onde a última sessão parou, nesta ou em outra máquina
---

Retome o projeto a partir do `docs/ONDE_PARAMOS.md`, sem varrer o repositório inteiro. O
projeto é trabalhado em mais de uma máquina (trabalho e casa), e memória local e `git stash`
da outra máquina não existem aqui.

1. **Traga o código.** Se a árvore estiver limpa, `git fetch` e `git switch developer && git
   pull`. Se houver alteração local não commitada, não troque de branch nem descarte nada:
   mostre o `git status` e pergunte.
2. **Leia o contexto, nesta ordem:** `AGENTS.md` (se ainda não estiver no contexto) e
   `docs/ONDE_PARAMOS.md`. Abra outros arquivos só quando uma pendência pedir.
3. **Confira se o arquivo está em dia.** A linha "Última atualização" traz a data e o último
   merge que ele cobre ("depois do merge #N"). Rode `git log --oneline --merges -40
   origin/developer` e procure merges com número maior que N. Se a linha não tiver o número,
   use a data **com hora**: `--since='<AAAA-MM-DD> 00:00'`. Sem a hora, o Git lê a data no
   horário atual e esconde tudo o que foi feito antes dele no mesmo dia — em 24/09/2026 isso
   devolveu 0 commits num dia com 24. Merges que só mexem em `docs/ONDE_PARAMOS.md` ou em
   `.claude/commands/` são manutenção deste ciclo e não contam (confira com `git show --stat
   <merge>^2`). Se sobrar merge que o arquivo não menciona, ele está desatualizado: resuma o
   que esses merges fizeram, pelo `git log`, e avise o usuário.
4. **Prepare a máquina**, seguindo a seção "Ao abrir numa máquina" do `ONDE_PARAMOS.md`:
   - porta serial: se a placa estiver ligada e o caminho em `/dev/serial/by-id/` for diferente
     do versionado no `platformio.ini`, proponha o `platformio_override.ini`, sem criar sem
     perguntar;
   - `pio test -e native` e `pio run`. Se o build falhar pela checagem do `sdkconfig`
     (débito 26), rode o comando de regeneração que a própria mensagem indica.
5. **Relate em poucas linhas:** estado do código (branch, último merge, `main` e `developer`
   iguais ou não), resultado dos testes e do build, e as pendências do `ONDE_PARAMOS.md`.
6. **Sugira o próximo trabalho**, com uma recomendação e o porquê, e **pare**. Não comece nada
   antes de o usuário escolher. Commit, PR, merge e gravar na placa seguem exigindo
   autorização a cada passo (AGENTS.md, workflow).

$ARGUMENTS
