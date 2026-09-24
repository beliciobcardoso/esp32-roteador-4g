# Recusa o build de release com a arvore suja (debito 25). Ligado so no env `release`.
#
# A versao que o firmware reporta sai do `git describe`, e com a arvore suja ela vira
# `<hash>-dirty`: um numero que nao identifica codigo nenhum, porque nao diz o que estava
# modificado. Uma unidade em campo com essa imagem e uma unidade cujo codigo ninguem consegue
# reproduzir, e isso so aparece quando alguem precisa reproduzir um defeito dela.
#
# Arquivo nao rastreado conta tambem, e e o caso mais traicoeiro: um .cpp novo esquecido fora
# do git entra no build e o `git describe --dirty` nem percebe, porque so olha o que o git
# rastreia. A versao sairia limpa e mentiria. Por isso a conferencia e o `git status
# --porcelain` inteiro, o mesmo que o procedimento de campo ja mandava rodar a mao.
#
# Fica fora do env de desenvolvimento de proposito: la build sujo e o caso normal, e travar
# seria atrito puro sem proteger ninguem.

import subprocess

Import("env")  # noqa: F821 - injetado pelo SCons

# Quantos arquivos listar antes de resumir. O suficiente para reconhecer o que esqueceu.
_MAX_LISTED = 15


def _fail(lines):
    print("")
    for line in lines:
        print("*** " + line)
    print("")
    env.Exit(1)  # noqa: F821


def _check():
    project_dir = env.subst("$PROJECT_DIR")  # noqa: F821
    try:
        out = subprocess.check_output(
            ["git", "status", "--porcelain", "--untracked-files=all"],
            cwd=project_dir,
            stderr=subprocess.STDOUT,
        )
    except (subprocess.CalledProcessError, OSError) as error:
        # Sem git nao ha como provar que a arvore esta limpa, e release sem prova nao sai.
        _fail([
            "release recusado: nao consegui consultar o git (debito 25).",
            "  %s" % error,
        ])
        return

    changed = [line for line in out.decode("utf-8", "replace").splitlines() if line.strip()]
    if not changed:
        return

    lines = ["release recusado: a arvore tem %d alteracao(oes) fora de commit (debito 25)."
             % len(changed)]
    lines += ["  " + line for line in changed[:_MAX_LISTED]]
    if len(changed) > _MAX_LISTED:
        lines.append("  ... e mais %d" % (len(changed) - _MAX_LISTED))
    lines += [
        "A imagem sairia com uma versao que nao identifica codigo nenhum.",
        "Commite ou guarde (git stash -u) e rode de novo. Build de bancada: pio run, sem -e release.",
    ]
    _fail(lines)


_check()
