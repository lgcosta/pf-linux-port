# Bancada de validação em VMs (libvirt / Alpine)

English: [docs/en/vm-lab.md](../en/vm-lab.md).

Todos os comandos abaixo foram lidos dos scripts em `scripts/` (eles são a
fonte de verdade); a bancada já foi criada e executada durante M0–M2 — uma
rodada completa e verde da suíte é o gate do M2+ e ainda está em andamento
(ver §6).

## 1. Onde ela mora

| Host | Papel |
|---|---|
| laptop (este repo) | compila kernel/módulo/ferramentas; comanda tudo por ssh |
| **deb** — `root@192.168.1.242` | hipervisor libvirt; roda as 4 VMs Alpine `pftest-*` |

A bancada não precisa de NAT para a internet; o único acesso externo é a
rede libvirt `default` (192.168.122.0/24, mgmt+DHCP) usada para o cloud-init
instalar pacotes e para o ssh dos testes. A `sms-vm` existente no deb nunca é
tocada.

## 2. Topologia (fonte única: `lab/ips.env`)

| VM | Papel | NICs | Kernel |
|---|---|---|---|
| `pftest-pf0` | filtro sob teste (roda o `pf.ko`) | `mgmt` (dhcp, rede default), `lan` 10.0.1.1/24, `wan` 10.0.2.1/24, `wan2` 10.0.3.1/24 | Alpine padrão via boot direto do kernel extraído da imagem; **ou** o `bzImage` 7.3.4 compilado com `--pf0-kernel=` (`root=/dev/vda2 ro console=ttyS0,115200n8`, sem initrd) |
| `pftest-cli` | cliente | `mgmt` dhcp (rotas desabilitadas), `lan` 10.0.1.10/24, gw 10.0.1.1 (pf0) | Alpine padrão |
| `pftest-srv` | servidor | `mgmt` dhcp (rotas desabilitadas), `wan` 10.0.2.20/24, gw 10.0.2.1 (pf0), 10.0.3.0/24 via gw2 | Alpine padrão |
| `pftest-gw2` | gateway do segundo caminho (ip_forward=1) | `wan2` 10.0.3.2/24, `wan` 10.0.2.2/24 (sem NIC mgmt) | Alpine padrão |

Todas as VMs recebem `net.ipv4.ip_forward=1` e `rp_filter=0` via cloud-init,
senha de root `pftest` mais um par de chaves de laboratório injetado
(`/root/.ssh/pflab_ed` no deb, cópia no laptop para ProxyJump), e os pacotes
`iperf3 tcpdump python3 scapy bash iptables` (o nome no Alpine é `scapy`; a
imagem também precisa de `iptables` para os experimentos de masquerade do
pf0).

Redes libvirt: `default` (mantida como está, iniciada+autostart para os
pacotes) e bridges isoladas `pf-lan` 10.0.1.0/24, `pf-wan` 10.0.2.0/24,
`pf-wan2` 10.0.3.0/24 — `forward="none"`, sem DHCP, mais IPs de bridge no
host `10.0.x.254` no deb para que o deb consiga ssh/icmp na NIC de dados de
cada guest. Dimensionamento: 2 vCPU / 1 GB por VM. Os discos são qcow2 por
VM com **backing** na imagem compartilhada imutável
`/var/lib/libvirt/images/alpine-base.qcow2` (sincronizada de
`_vendor/images/alpine-3.22-x86_64-cloud.qcow2` por sha256).

> Nota de boot: a imagem cloud do Alpine não é inicializável em BIOS/UEFI no
> QEMU como entregue (ESP vazia), então *todas* as VMs usam boot direto do
> kernel extraído da própria imagem; `--pf0-kernel=` apenas troca o kernel
> do pf0 pelo compilado.

## 3. Recriar / deployar / destruir

```sh
./scripts/vm-create.sh                       # idempotente: derruba e recria domínios + seeds pftest-*
./scripts/vm-create.sh --pf0-kernel=out/linux-7.3.4-bzImage   # pf0 sobe com o kernel compilado (T6)
./scripts/vm-create.sh --recreate-base       # força re-sync da imagem base
./scripts/vm-deploy.sh                       # laptop→deb ($DEPLOY_DIR)→pf0:/opt/pf:
                                             #   out/*bzImage, module/*.ko, tools/out/*
./scripts/vm-destroy.sh                      # domínios, discos, seeds, redes pf-* e fragmentos ssh
./scripts/vm-destroy.sh --purge-base --purge-keys   # + imagem base + chaves do lab
```

O `vm-create.sh` termina validando `ssh pf0 'ip -br a'` e deixa todas as VMs
ligadas. Os aliases de ssh do laptop (`pf0|cli|srv|gw2`, ProxyJump via deb)
são gerados em `~/.ssh/config.d/pflab`.

**Do pf0 para as outras VMs.** O firewall tem seu próprio par de chaves e os
pares confiam nele, então uma sessão no pf0 pode pilotar o cliente e o
servidor diretamente — é isso que os screencasts fazem
(`ssh cli 'ping …'`):

```sh
# uma vez por lab, a partir do hypervisor
ssh pf0 'ssh-keygen -t ed25519 -N "" -C pf0 -f /root/.ssh/id_ed25519'
# depois autorizar essa chave pública no cli, no srv e no gw2
```

O `/root/.ssh/config` do pf0 mapeia os nomes curtos `cli`, `srv` e `gw2`
para 10.0.1.10, 10.0.2.20 e 10.0.3.2, e o `/etc/hosts` traz o mesmo
mapeamento.

## 4. Rodando a suíte

```sh
./scripts/vm-run-tests.sh        # ou: make test
```

Determinístico por projeto: **antes de cada gate** o runner destrói e
reinicia o `pftest-pf0` (delta de disco e módulo zerados), espera o ssh no
endereço DHCP de mgmt `192.168.122.8`, re-deploya `pf.ko`/`pfctl`/`*.conf` no
guest e então roda o script do gate a partir do **laptop** (os gates falam
com as VMs através do deb via ssh aninhado — `tests/vm/lib.sh`). A saída de
cada gate é gravada em `out/test-logs/<gate>.sh.log`; o runner imprime
`PASS <gate>` / `FAIL <gate> (rc=...)` por teste, um resumo `PASS=n FAIL=m`
e sai com código diferente de zero se algo falhou.

## 5. O que cada gate prova hoje

Cada gate afirma **tráfego real e estado observável**, nunca apenas um código
de saída: o pacote chega, é descartado ou é traduzido, e a prova é lida de
volta nos contadores do próprio firewall, na tabela de estados, em capturas de
pacote ou no par.

A lista completa está na §6 (15 gates, todos verdes). Cada gate é um script
shell em `tests/vm/` que fala com as VMs através do deb com ssh aninhado,
usando os helpers de `tests/vm/lib.sh` (`pf0`, `cli`, `srv`, `gw2`, `pfput`,
`pfload`, `ok`, `bad`). O `scripts/vm-run-tests.sh` roda tudo em ordem e
guarda `out/test-logs/<gate>.sh.log`.

O que **não** tem gate (lacunas honestas): o caminho IPv6, o fingerprint de
OS, as opções `set` (`state-policy`, `allow-opts`) e os syncookies — o código
compila dentro do `pf.ko`, mas nenhum gate os exercita. O FQ-CoDel compila e
compartilha o header de estatísticas com o `pfctl`, mas só a disciplina
**hfsc** é exercitada.

**Não existe gate numérico de performance**: o `bench-basic.sh` não está na
árvore e nenhuma meta de vazão é reivindicada.

## 6. Status atual da suíte (2026-11-12)

**15/15 gates verdes.** Uma rodada completa e limpa
(`./scripts/vm-run-tests.sh`, ~9 min) recria e reinicia o `pftest-pf0` antes
de cada gate, reimplanta o módulo e as ferramentas, e termina com
`PASS=15 FAIL=0`. Os logs capturados ficam em
`out/test-logs/<gate>.sh.log`.

| Gate | O que prova hoje |
|---|---|
| `01-state.sh` | `keep state` em um ping de trânsito; o estado aparece no `pfctl -ss` |
| `02-block.sh` | `block drop` mata o ping; `block return-rst` devolve RST ao cliente |
| `03-tables.sh` | uma tabela `pfr` bloqueia um host; um `-T delete` ao vivo libera o mesmo fluxo |
| `04-anchors.sh` | a regra de um anchor é avaliada antes de um `block` posterior do ruleset principal |
| `05-nat.sh` | NAT de origem: o pacote sai com o endereço do pool e a resposta é des-NATeada |
| `06-rdr.sh` | redirect de porta de ponta a ponta, mais 2 MB passando com integridade byte a byte |
| `07-binat.sh` | `binat-to`: o par vê o endereço mapeado e o original nunca vaza |
| `08-routeto.sh` | `route-to` sai pelo link do gateway; `dup-to` copia e mantém o fluxo |
| `09-scrub.sh` | `min-ttl` e `max-mss` medidos no egresso do próprio firewall |
| `10-pools.sh` | seleção do pool de NAT (`round-robin`, `source-hash`) |
| `11-divert.sh` | `divert-to` para o relay `pdivert`: o pacote vai para o userspace e volta |
| `12-pflog.sh` | os pacotes logados chegam a `/dev/pflog` e viram um pcap DLT_PFLOG |
| `13-replyto.sh` | `reply-to` força a resposta para o link do gateway |
| `14-soak.sh` | reload de ruleset sob tráfego, ciclo de `rmmod`, checagem de vazamento |
| `15-queue.sh` | filas hfsc: anexação, estatísticas da classe, um fluxo de 2 MB contado pela classe |

Os screencasts ([screencast.md](screencast.md)) mostram boa parte desses
gates sendo executados, com o diagrama de rede do lab desenhado sobre o
terminal.

## 7. Inspeção manual

```sh
ssh pf0                       # via alias do laptop (ProxyJump deb)
insmod /opt/pf/pf.ko
/opt/pf/pfctl -e
/opt/pf/pfctl -f /tmp/g1.conf
/opt/pf/pfctl -s info ; /opt/pf/pfctl -s states -v
dmesg | grep pfk
```

Artefatos de rodadas completas futuras (log + pcaps) devem ir junto com o
release no Gitea (spec §7).
