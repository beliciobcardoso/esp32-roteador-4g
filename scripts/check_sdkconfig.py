# Falha o build quando o sdkconfig gerado diverge do sdkconfig.defaults (debito 26).
#
# O build nao le o sdkconfig.defaults: le o sdkconfig.<env> gerado a partir dele, e so o
# regenera quando o gerado nao existe. Opcao nova no defaults nao chega ao firmware e o
# build sai verde do mesmo jeito. Em 24/09/2026 isso gravou placa sem CORE_LOCKING e sem
# rollback de OTA, e so apareceu porque uma medicao de descarte deu o numero errado.
#
# Falhar, e nao regenerar sozinho: apagar o gerado joga fora qualquer ajuste feito por
# menuconfig sem aviso, e quem roda o build precisa saber que o firmware anterior saiu
# diferente do que o defaults pedia.

import os
import re

Import("env")  # noqa: F821 - injetado pelo SCons

_SET = re.compile(r"^(CONFIG_[A-Z0-9_]+)=(.*)$")
_UNSET = re.compile(r"^# (CONFIG_[A-Z0-9_]+) is not set$")


def _read_options(path):
    # "# CONFIG_X is not set" e o jeito do Kconfig de gravar um bool desligado; vira "n"
    # para comparar com defaults que escrevam "CONFIG_X=n".
    options = {}
    with open(path, encoding="utf-8") as f:
        for raw in f:
            line = raw.strip()
            match = _SET.match(line)
            if match:
                options[match.group(1)] = match.group(2)
                continue
            match = _UNSET.match(line)
            if match:
                options[match.group(1)] = "n"
    return options


def _check():
    project_dir = env.subst("$PROJECT_DIR")  # noqa: F821
    pioenv = env.subst("$PIOENV")  # noqa: F821
    defaults_path = os.path.join(project_dir, "sdkconfig.defaults")
    generated_name = "sdkconfig." + pioenv
    generated_path = os.path.join(project_dir, generated_name)

    # Sem o gerado, o build o cria agora a partir do defaults, entao nao ha o que divergir.
    if not os.path.isfile(defaults_path) or not os.path.isfile(generated_path):
        return

    wanted = _read_options(defaults_path)
    actual = _read_options(generated_path)
    diverging = [
        (key, value, actual.get(key, "ausente"))
        for key, value in wanted.items()
        if actual.get(key) != value
    ]
    if not diverging:
        return

    print("")
    print("*** %s diverge do sdkconfig.defaults (debito 26):" % generated_name)
    for key, value, got in diverging:
        print("***   %s: defaults pede %s, gerado tem %s" % (key, value, got))
    print("*** O firmware sairia sem essas opcoes. Regenere o gerado e rode de novo:")
    print("***   rm -f %s && rm -rf .pio && pio run" % generated_name)
    print("")
    env.Exit(1)  # noqa: F821


_check()
