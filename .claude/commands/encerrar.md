---
description: Atualiza o docs/ONDE_PARAMOS.md com o que a sessão fez e prepara a publicação
---

Encerre a sessão deixando o estado no repositório, para a próxima sessão — nesta ou em outra
máquina — continuar pelo `/continuar` sem varrer o projeto.

1. **Levante o que mudou nesta sessão:** a conversa, e os merges posteriores ao último que o
   `docs/ONDE_PARAMOS.md` cita na linha "Última atualização" ("depois do merge #N"), em
   `developer` e em `main` (`git log --oneline --merges -40 <branch>`). Sem número na linha,
   use a data **com hora** — `--since='<AAAA-MM-DD> 00:00'` —, porque sem a hora o Git esconde
   o que foi feito antes do horário atual no mesmo dia. Liste também o que
   **não** viaja e precisa de decisão: alterações não commitadas, `git stash list`, branches
   locais sem remoto.
2. **Pergunte antes de sair publicando** se houver trabalho não commitado ou stash com algo
   útil. As opções são commitar pelo fluxo normal, publicar numa branch `tmp/...` com commit
   "wip … do not merge", ou deixar local sabendo que não vai para a outra máquina. Não decida
   sozinho.
3. **Reescreva o `docs/ONDE_PARAMOS.md`**, mantendo a estrutura:
   - data, lugar e último merge coberto na linha "Última atualização" ("DD/MM/AAAA, fim do dia
     no trabalho/em casa (depois do merge #N)"). O número é o que o `/continuar` usa para
     saber se o arquivo ficou para trás — não omita;
   - o que fechou na sessão, com os números dos PRs;
   - pendências atualizadas: tire o que fechou, acrescente o que surgiu, com o motivo;
   - próximo passo recomendado;
   - achados de bancada que ainda não entraram nos docs definitivos (PRDs, débitos).

   O arquivo é um resumo e um mapa, não um diário: aponte para PRDs, débitos e branches em vez
   de copiar o conteúdo deles, e mantenha-o curto o bastante para ser lido inteiro no começo
   de uma sessão. Número medido vai com a data.
4. **Mostre o diff** do `ONDE_PARAMOS.md` e **pare**. Publicar segue as regras do AGENTS.md:
   só com autorização, e o fluxo usual é branch `docs/...` a partir do `developer`, commit, PR,
   merge, promoção para `main` e apagar a branch.

$ARGUMENTS
