# Demo (asciinema)

O lab pode ser gravado como um cast do [asciinema](https://asciinema.org).
Cada byte da gravação é a saída real das VMs do lab (Alpine sobre libvirt,
rodando o kernel compilado do fonte com o `pf.ko`); só o ritmo é roteirizado.

## Gravar

```sh
pip install asciinema            # uma vez
bash scripts/vm-run-tests.sh      # ou: scripts/vm-create.sh + deploy
asciinema rec out/demo-pf-linux.cast -c "bash scripts/demo-session.sh"
```

A sessão percorre:

1. a topologia do lab e o `pf.ko` recém-carregado (prova no `dmesg`),
2. `pfctl -s info` pela ABI de ioctl do OpenBSD,
3. um `pf.conf` real (tabela, regras stateful, drop explícito) e `pfctl -sr`,
4. ping em trânsito + `pfctl -ss` (stateful),
5. `pfctl -t <tabela> -T show` (tabelas vivas),
6. um ping bloqueado (contadores com `pfctl -sr -vv`),
7. `divert-to` com o relay `pdivert` (pacote vai ao userspace e volta),
8. desligamento.

## Reproduzir

```sh
asciinema play out/demo-pf-linux.cast
```

## Publicar

```sh
asciinema upload out/demo-pf-linux.cast
```

`scripts/demo-record.py` é um gravador alternativo que produz o mesmo formato
asciicast v2 sem o binário asciinema (ele conduz o lab por ssh e escreve o
cast diretamente).
