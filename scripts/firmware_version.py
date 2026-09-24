# Mantem version.txt igual ao `git describe`, para o PROJECT_VER do ESP-IDF nao envelhecer.
#
# O problema que isto resolve: o IDF resolve PROJECT_VER na CONFIGURACAO do CMake, nao a
# cada build (`__project_get_revision`, em tools/cmake/project.cmake). O PlatformIO so
# reconfigura quando o cache do CMake some, entao um build incremental depois de trocar de
# branch ou de commitar mantem a versao antiga gravada no esp_app_desc_t. Medido em
# 23/09/2026: estando em 7d3a4f3, um build incremental gravou 98fd940 — um commit que nem e
# ancestral daquele. A data e a hora do descritor congelam junto, pelo mesmo motivo.
#
# Isso nao e cosmetico. O passo 2 de docs/ATUALIZACAO_EM_PRODUCAO.md manda o operador anotar
# a versao antes da atualizacao porque e o que permite dizer depois se ela pegou. Com o campo
# congelado essa conferencia mente em silencio, e mente com confianca — diferente do build
# sujo do debito 25, onde o "-dirty" avisa que o numero nao vale.
#
# Escrevemos version.txt porque o IDF prefere esse arquivo ao git describe quando ele existe,
# e forcamos a reconfiguracao apagando o CMakeCache.txt — o CMAKE_CONFIGURE_DEPENDS que o
# proprio IDF registra no arquivo nao adianta aqui, porque quem decide reconfigurar e o
# PlatformIO, e ele nao consulta essa propriedade.
#
# O custo so aparece quando a versao muda de verdade: ~13 s de reconfiguracao mais relink,
# nao um rebuild inteiro, porque os objetos seguem em cache. Build sem troca de commit nao
# paga nada, porque o arquivo nao e reescrito quando o conteudo ja esta certo.

import os
import subprocess

Import("env")

project_dir = env["PROJECT_DIR"]


def git_describe():
    try:
        out = subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            cwd=project_dir,
            stderr=subprocess.DEVNULL,
        )
    except (subprocess.CalledProcessError, OSError):
        # Fora de um repositorio git, ou sem commits: deixa o IDF seguir com o que ele
        # decidir sozinho, em vez de gravar uma versao inventada.
        return ""
    return out.decode("utf-8", "replace").strip()


version = git_describe()
if version:
    version_path = os.path.join(project_dir, "version.txt")

    current = ""
    if os.path.isfile(version_path):
        with open(version_path, "r", encoding="utf-8") as handle:
            current = handle.read().strip()

    if current != version:
        with open(version_path, "w", encoding="utf-8") as handle:
            handle.write(version + "\n")

        # Sem isto o arquivo novo so valeria no proximo build que reconfigurasse por outro
        # motivo — ou seja, a correcao chegaria atrasada de um build, que e pior do que nao
        # existir: acertaria as vezes.
        cmake_cache = os.path.join(env.subst("$BUILD_DIR"), "CMakeCache.txt")
        if os.path.isfile(cmake_cache):
            os.remove(cmake_cache)

        print("firmware_version: PROJECT_VER agora e %s (reconfigurando)" % version)
