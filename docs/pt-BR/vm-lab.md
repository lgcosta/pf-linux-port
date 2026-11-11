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

| Gate | Cenário (verificado com tráfego real, não com código de saída) |
|---|---|
| `01-state.sh` | `pass in/out quick proto icmp keep state` → cli pinga srv **através** do pf0 (trânsito); depois o pf0 lista um estado icmp via `pfctl -s states` |
| `02-block.sh` | `block drop in quick proto icmp` → o ping do cli falha; `block return-rst in quick proto tcp … port 8080` → o `nc` do cli para srv:8080 é recusado (RST), checado pelo código de saída do nc |
| `03-tables.sh` | `table <blk> { 10.0.1.10 }` + `block in quick from <blk>` → `pfctl -t blk -T show` lista; o ping do cli é bloqueado; `-T delete` ao vivo libera o fluxo |
| `04-anchors.sh` | regra de anchor (`-a restricted -f`) liberando icmp é avaliada **antes** do `block` do ruleset principal (posicionamento top/quick); `pfctl -s Anchors` lista o anchor |

Gates planejados (documento de plano, T11–T18): 05-nat, 06-rdr, 07-binat,
08-scrub (scapy), 09-routeto, 10-dup-fail, 11-divert, 12-queues (tolerância
de ±15% no rate do iperf), 13-pflog, 14-soak (reload sob tráfego + ciclo de
rmmod + kmemleak). IPv6 é smoke test para state/block/nat66/scrub. O
`bench-basic.sh` (planejado, ainda não está na árvore) é informativo; não
existe gate numérico de performance.

## 6. Status atual da suíte (2026-11-11, com honestidade)

Os gates 01–04 do M2 existem e rodam, mas **ainda não estão todos verdes** —
o caminho de dados (modelo netisr/workqueue) e a visibilidade de estados
estão sendo corrigidos no trabalho do gate M2. A última rodada capturada
(`out/test-logs/`) mostra o estado misto: ping de trânsito OK / listagem de
estado falhando no 01, icmp bloqueado OK no 02, resultados parciais no 03, 04
abortado. Trate "VERIFIED-in-VM" na
[matriz de features](feature-matrix.md) como reservado a gates que passam
numa rodada completa e limpa; nada além do M1 é declarado VERIFIED até lá.

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
