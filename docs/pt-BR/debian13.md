# PF no Linux — implementação do zero no Debian 13 (trixie)

Este documento é o caminho completo, do zero, para rodar o **PF-on-Linux**
(port do PF do OpenBSD 8.0 como módulo de kernel out-of-tree) em uma máquina
**Debian 13 (trixie)**: preparar o host, compilar o kernel 7.3.4, compilar o
módulo `pf.ko` e o `pfctl`, subir o laboratório libvirt, rodar a suíte de 16
gates e o **teste de stress**.

Cada comando aqui foi executado no Debian 13.7 (`trixie`) do laboratório e a
saída real está indicada. Onde o número importa, ele é o número medido — não
uma estimativa.

---

## 0. Antes de começar: o que precisa ficar claro

| Fato | Consequência prática |
|---|---|
| `pf.ko` é compilado contra a árvore do **Linux 7.3.4** e carrega o `vermagic` desse kernel | Ele **não** carrega no kernel do Debian 13 (6.12). Não tente `insmod pf.ko` no 6.12: o `vermagic` diverge e o `insmod` falha. |
| O módulo registra hooks **netfilter** em `init_net` (IPv4 e IPv6, 5 pontos cada) | PF convive com `nftables`/`iptables`; ele não substitui o firewall do Debian. |
| PF roda em **um namespace de rede só** (`init_net`) | Não há instância de PF por netns. |
| O OpenBSD `pfctl` fala com o módulo pelo ioctl ABI de `/dev/pf` | O `pfctl` desta árvore é o **OpenBSD 8.0** compilado para Linux; não é o do Debian. |

Três papéis cabem na mesma máquina Debian 13 — e é assim que o laboratório
foi validado:

| Papel | O que faz | Onde |
|---|---|---|
| **build host** | compila o kernel 7.3.4, o `pf.ko` e o userland | `~/lab/linux_kernel/pf_port` |
| **hypervisor** | hospeda as 4 VMs de teste (libvirt/KVM) | mesmo host, `libvirtd` |
| **host PF** (opcional) | roda o próprio PF no Debian 13, depois de bootar com o kernel 7.3.4 | mesmo host, com aviso no §9 |

---

## 1. Passo 1 — preparar o host

```sh
git clone <url-do-repo> ~/lab/linux_kernel/pf_port   # ou: cp -a /caminho/pf_port ~/lab/linux_kernel/
cd ~/lab/linux_kernel/pf_port
sudo ./scripts/debian13/prepare-host.sh
```

O script é idempotente: instala os pacotes e imprime o estado do toolchain.
Saída real no Debian 13.7:

```
== toolchain
gcc          14
make         GNU Make 4.4.1
bison        bison (GNU Bison) 3.8.2
flex         flex 2.6.4
bc           bc 1.07.1
rsync        rsync  version 3.5.0  protocol version 32
libvirt      11.3.0
qemu         QEMU emulator version 10.0.13 (Debian 1:10.0.13+ds-0+deb13u1)
geniso       genisoimage 1.1.11 (Linux)

PREPARE_HOST_OK
```

**Sobre o compilador.** O Debian 13 traz `gcc-14` como padrão e **não tem
`gcc-15` em trixie** (`apt-cache policy gcc-15` → nenhum candidato). O
`docs/en/install.md` pede "gcc ≥ 15" porque o host de desenvolvimento
original é Fedora. No Debian 13 o build é feito com **gcc 14.2.0** — o kernel
7.3.4 e o `pf.ko` compilam sem patch (verificado, §4).

Se preferir separar os papéis, use `--build-only` ou `--lab-only`.

---

## 2. Passo 2 — obter as fontes

### 2.1 Árvore do kernel 7.3.4

O repositório **não** traz o kernel: ele espera a árvore em
`~/lab/linux_kernel/linux-7.3.4` (irmã do repositório). Do zero:

```sh
cd ~/lab/linux_kernel
curl -fLO https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-7.3.4.tar.xz
curl -fLO https://cdn.kernel.org/pub/linux/kernel/v7.x/sha256sums.asc
sha256sum -c --ignore-missing sha256sums.asc     # confere só o tarball baixado
tar xf linux-7.3.4.tar.xz
```

O hash confere com o publicado pelo kernel.org:

```
b11b6df8295e8e991ff040b71aaa4994a8b55caa23b6b092907f195b5840fd1f  linux-7.3.4.tar.xz
```

### 2.2 Fontes do OpenBSD 8.0 (o PF em si)

```sh
cd ~/lab/linux_kernel/pf_port
./scripts/fetch-sources.sh     # baixa sys.tar.gz + src.tar.gz, confere sha256 (pins no VERSIONS)
./scripts/vendorize.sh         # extrai só os arquivos portados -> _vendor/upstream + IDS.txt
```

Espere as linhas `OK   sys.tar.gz` e `OK   src.tar.gz`. O `fetch-sources.sh`
tenta o mirror `openbsd.c3sl.ufpr.br` e cai para `cdn.openbsd.org`.

---

## 3. Passo 3 — compilar o kernel 7.3.4

```sh
cd ~/lab/linux_kernel/pf_port
KERNELDIR=~/lab/linux_kernel/linux-7.3.4 ./scripts/build-kernel.sh
```

O que o script faz, em ordem:

1. `scripts/kernel-config.sh` — parte de `make defconfig` e liga
   `VIRTIO_BLK/PCI/NET=y`, `EXT4_FS=y`, `NETFILTER=y`, `NET_SCHED=y`,
   `INET=y`, `IPV6=y`, `SERIAL_8250(+CONSOLE)=y` (é o que permite bootar a VM
   `pf0` direto no kernel, **sem initrd**) e roda `make olddefconfig`;
2. `make -j$(nproc-2) bzImage modules_prepare` — compila o kernel e prepara a
   árvore para módulos externos;
3. copia `vmlinux.symvers` → `Module.symvers` (o kbuild não cria o segundo) e
   garante `scripts/module.lds`;
4. publica em `out/`: `linux-7.3.4-bzImage`, `System.map`, `.config`,
   `kernelversion`, `bzImage.sha256`.

Termina com:

```
KERNEL_BUILD_OK 7.3.4
```

Tempo de referência no host do lab (Debian 13, 12 threads): **~4 min** para o
build do kernel em si (`bzImage` + `modules_prepare`) em uma árvore que já
tinha objetos, e o script inteiro — configurar, compilar, publicar os
artefatos — em menos de 3 minutos. Uma árvore vinda direto do tarball
começa vazia; aí o passo `make` domina o tempo.

---

## 4. Passo 4 — compilar o módulo `pf.ko`

```sh
cd ~/lab/linux_kernel/pf_port
./scripts/build-module.sh          # == make -C $KERNELDIR M=$PWD/module
```

Saída: `MODULE_BUILD_OK` e o artefato `module/pf.ko`.

**Verifique o `vermagic`** — é ele que decide onde o módulo carrega:

```sh
modinfo module/pf.ko | grep vermagic
# vermagic:       7.3.4 SMP preempt mod_unload
```

Esse é o motivo de o kernel e o módulo terem de sair **da mesma árvore**: um
`pf.ko` compilado contra outra árvore 7.3.4 pode ter `vermagic` subtly
diferente (`SMP preempt mod_unload modversions` + `CONFIG_*` do build) e o
`insmod` recusa.

### 4.1 Verificação do ABI userspace ↔ kernel

O `pfctl` fala com o kernel por structs compartilhadas. O harness `abitest`
compara o layout das duas pontas:

```sh
make -C tools abi      # compila abitest (userspace) e abitest-module (kernel),
                       # dumpa as tabelas de tamanho/offset e faz o diff
```

Sem saída de diff = ABI intacto.

---

## 5. Passo 5 — compilar o userland

```sh
make -C tools          # -> tools/out/{pfctl,pdivert,pflogd} + abitest
```

O `pfctl` sai **estático** (`-static`), para poder ser copiado para as VMs
Alpine sem dependências. Checagem offline (não precisa de `/dev/pf`):

```sh
(cd tools && ./out/pfctl -nf tests/01_basic.conf)    # parse-only: sem saída = ok
make -C tools test                                   # roda todos os .conf de teste
make -C tools abi                                    # tabelas de ABI userspace vs kernel
```

---

### 5.1 O que o build no Debian 13 produziu (verificado)

Tudo abaixo foi compilado e executado no Debian 13.7 com **gcc 14.2.0** — sem
patch, sem backport:

```
$ ./scripts/build-module.sh
...
MODULE_BUILD_OK
$ modinfo module/pf.ko | grep vermagic
vermagic:       7.3.4 SMP preempt mod_unload
$ make -C tools test
PASS tests/01_basic.conf (exit 0)
PASS tests/02_translations.conf (exit 0)
PASS tests/03_queues_scrub.conf (exit 0)
PASS tests/broken.conf rejected (non-zero exit)
$ make -C tools abi
ABI OK: identical tables
```

O `vermagic` impresso pelo build no Debian é idêntico ao que a VM do lab
aceita. Verificado de ponta a ponta: o módulo compilado no Debian 13 foi
copiado para a VM do lab e carregado lá —

```
INSMOD_DEBIAN_MODULE_OK
Status: Enabled
crw-------    1 root     root       10, 259 /dev/pf
[  119.057040] pfk: netfilter hooks registered (10, netisr wq data path)
```

— desde que ele tenha sido compilado a partir da mesma árvore do kernel que
a VM sobe.

---

## 6. Passo 6 — o laboratório libvirt no Debian 13

O lab são 4 VMs Alpine (`pf0` = o firewall sob teste, `cli`, `srv`, `gw2`),
rede de gerência com DHCP e três bridges isolados `pf-lan`/`pf-wan`/`pf-wan2`.
A topologia está em `lab/ips.env`.

```sh
cd ~/lab/linux_kernel/pf_port
./scripts/vm-create.sh --pf0-kernel=out/linux-7.3.4-bzImage
```

Esse comando é idempotente: destrói e recria os domínios `pftest-*`, gera as
seeds cloud-init, sincroniza a imagem base Alpine e sobe tudo. Sem
`--pf0-kernel=`, a `pf0` sobe com o kernel da própria imagem Alpine — e aí o
`pf.ko` **não** carrega (vermagic). **Sempre** passe `--pf0-kernel=` quando a
intenção é testar PF.

Deploy dos artefatos para a VM:

```sh
./scripts/vm-deploy.sh       # laptop -> deb ($DEPLOY_DIR) -> pf0:/opt/pf/
```

Verifique a VM:

```sh
ssh pf0 'uname -r; lsmod | grep -w pf; ls -l /opt/pf/'
```

Esperado: `7.3.4`, o módulo `pf` carregado e `pf.ko`/`pfctl`/`pdivert`/`pflogd`
em `/opt/pf/`.

---

## 7. Passo 7 — a suíte de 16 gates

```sh
./scripts/vm-run-tests.sh          # ou: make test
```

O runner reinicia a `pf0` antes de cada gate (estado limpo), re-deploya o
módulo e as ferramentas, e roda o gate a partir do build host. Saída esperada:

```
PASS=16 FAIL=0
```

Logs por gate em `out/test-logs/<gate>.sh.log`.

---

## 8. Passo 8 — o teste de stress

O projeto não tinha gate de desempenho (o `docs/en/vm-lab.md` registrava
explicitamente essa lacuna). O stress é um gate novo, `tests/vm/16-stress.sh`,
que pergunta coisas diferentes das gates funcionais: *"o motor continua
correto, limitado e sem vazamento enquanto tráfego, estados, regras e tabelas
giram?"*.

```sh
./scripts/stress/run-stress.sh                 # perfil padrão
./scripts/stress/run-stress.sh --quick         # perfil curto (smoke)
./scripts/stress/run-stress.sh --seconds 30 --connections 2000 --reloads 500
```

Opções: `--seconds`, `--parallel`, `--rate`, `--connections`, `--reloads`,
`--rules`, `--table-entries`, `--max-overhead`. O runner reset da `pf0`, faz o
deploy, roda o gate e grava o relatório em `out/stress/<timestamp>/report.md`.

> **As fases de tráfego rodam com taxa limitada** (`--rate`, padrão
> 100 Mbit/s por fluxo = 400 Mbit/s no total). Não é conveniência: o PF
> rouba cada skb para uma única workqueue ordenada e roda o engine em
> contexto de processo, então o backlog — e a memória que ele consome —
> cresce com a taxa oferecida. Na VM de 1 GB do lab, 400 Mbit/s é estável e
> **~1 Gbit/s dá OOM-panic no kernel** ("System is deadlocked on memory").
> Por isso o teste de stress trabalha no regime que o port suporta e reporta
> o teto separadamente, em vez de matar o firewall sob teste. Detalhes em
> [`caveats.md` §13](caveats.md).

### 8.1 O que cada fase mede

| Fase | O que faz | Como é julgado |
|---|---|---|
| 0 — baseline | throughput `cli -> srv` na taxa limitada com o **módulo fora do caminho** (`rmmod pf`) | precisa de > 0 Mbit/s (sem isso o resto não tem sentido) |
| 1 — overhead | o mesmo tráfego com PF ligado e regras stateful | `%` de overhead = (baseline − com PF) / baseline, contra o limite (padrão 30%) — na taxa com teto isso significa "o PF sustenta a taxa oferecida" |
| 2 — state churn | 300 conexões TCP concorrentes contra um listener dedicado no servidor | os estados **aparecem** na tabela (`pfctl -s states`) e **desaparecem** depois de `pfctl -sr` → sem vazamento |
| 3 — rule churn | 200 recargas de um ruleset de 200 regras | zero falhas de carga, tempo de recarga medido **dentro** da VM (min/méd/máx), contagem de regras estável |
| 4 — table churn | 2000 entradas em uma `pfr` table | entradas somem após o flush → sem vazamento de tabela |
| 5 — integridade | 2 MB atravessando `nat-to` **enquanto** um stream satura o link | sha256 do arquivo recebido = sha256 da origem |
| 6 — saúde | `dmesg`, contadores do PF, e um ping final | nenhum `BUG/WARNING/Oops`; contadores mexem; o firewall ainda roteia |

### 8.2 Como ler o relatório

Uma rodada real no lab (perfil completo, `out/stress/<timestamp>/report.md`):

```
STRESS phase=baseline baseline_mbit=400 seconds=10 parallel=4 rate_per_stream=100M
STRESS phase=overhead pf_mbit=400 overhead_pct=0 limit_pct=30 rate_per_stream=100M
STRESS phase=state_churn connections=300 states_before=0 states_created=300 states_after_reset=0 established_on_srv=214 rules_loaded=3 pf_status=Enabled
STRESS phase=rule_churn reloads=200 rules_per_load=200 reload_fails=0 reload_ms_avg=11 reload_ms_max=20 rules_final=202
STRESS phase=table_churn entries_requested=2000 entries_added=2000 add_errors=0 entries_after_flush=0
STRESS phase=integrity bytes=2000000 sha256_match=yes
STRESS phase=health dmesg_anomalies=0 match=5855 state_entries=7
```

Como ler: o PF sustenta os 400 Mbit/s limitados com **0% de overhead** contra
o baseline sem módulo, acompanha **300/300** fluxos concorrentes como estados
e esvazia a tabela de volta a 0, recarrega um ruleset de 202 regras **200
vezes** sem falhar (11 ms de média, 20 ms o pior caso), esvazia **2000**
entradas de tabela de volta a 0, move 2 MB por NAT byte a byte enquanto um
stream corre e não deixa nenhum `BUG/WARNING/Oops` para trás.

O runner imprime `STRESS RESULT: PASS` ou `FAIL` e sai com o status do gate.

> **Nota de honestidade:** os números de throughput são de um lab virtualizado
> (KVM, 2 vCPU por VM, rede `virtio` com bridge no host). Servem para
> *comparar* baseline contra PF no mesmo ambiente, não como número de
> produção. O overhead medido inclui a cópia de pacote e o caminho de software
> do PF — é o preço do filter stateful no userspace-space do kernel.

---

## 9. Passo 9 (opcional) — rodar o PF no próprio Debian 13

Isto **não** é necessário para o laboratório, e tem um custo: o host do lab é
também o hypervisor — reiniciar com outro kernel derruba as VMs.

```sh
cd ~/lab/linux_kernel/linux-7.3.4
sudo make modules_install                       # pf.ko -> /lib/modules/7.3.4/
sudo cp arch/x86/boot/bzImage /boot/vmlinuz-7.3.4
sudo update-initramfs -u -k 7.3.4               # ou: update-grub
sudo reboot                                     # ⚠ derruba as VMs do lab
```

Depois do boot (com o 7.3.4 ativo):

```sh
uname -r                                        # 7.3.4
sudo insmod /lib/modules/7.3.4/pf.ko            # cria /dev/pf (0600) e registra os hooks
ls -l /dev/pf
sudo cp ~/lab/linux_kernel/pf_port/tools/out/pfctl /usr/local/sbin/
sudo pfctl -e
sudo pfctl -f /etc/pf.conf
sudo pfctl -s info
```

`/etc/pf.conf` mínimo (sintaxe OpenBSD 8.0):

```
set skip on lo
block return
pass on egress
```

Para deixar o PF ligado no boot, uma unit simples:

```ini
# /etc/systemd/system/pf.service
[Unit]
Description=PF-on-Linux engine
After=network-pre.target
Before=multi-user.target

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/sbin/modprobe pf
ExecStart=/usr/local/sbin/pfctl -e
ExecStop=/usr/local/sbin/pfctl -d
ExecStop=/sbin/modprobe -r pf

[Install]
WantedBy=multi-user.target
```

E o `pflogd` (escreve pcap `DLT_PFLOG`, layout OpenBSD `pflog(4)`):

```ini
# /etc/systemd/system/pflogd.service
[Unit]
Description=PF log daemon
After=pf.service

[Service]
ExecStart=/usr/local/sbin/pflogd /var/log/pf/pflog.pcap

[Install]
WantedBy=multi-user.target
```

> `pflogd` lê `/dev/pflog` (criado pelo módulo, 0600). Este `pfctl` **não**
> tem o modificador `-s pflog`: o log é ligado por regra, com a palavra-chave
> do OpenBSD (`pass log on egress ...`, `block log ...`). O pcap em disco fica
> onde você apontar o `pflogd`.

---

## 10. Checklist de verificação

```sh
# 1. módulo carregado e device criado
lsmod | grep -w pf && ls -l /dev/pf

# 2. PF habilitado
pfctl -s info | head -2                     # Status: Enabled

# 3. parser do OpenBSD 8.0 no binário do Debian
pfctl -nf /etc/pf.conf

# 4. hooks registrados
dmesg | grep pfk                           # "pfk: netfilter hooks registered (10, netisr wq data path)"

# 5. o firewall filtra de verdade
pfctl -sr; pfctl -s states

# 6. suíte completa
./scripts/vm-run-tests.sh                  # PASS=16 FAIL=0

# 7. stress
./scripts/stress/run-stress.sh             # STRESS RESULT: PASS
```

---

## 11. Troubleshooting

| Sintoma | Causa | Ação |
|---|---|---|
| `insmod: ERROR: could not insert 'pf.ko': Invalid module format` | `vermagic` diferente do kernel em execução | o kernel e o módulo precisam sair da **mesma** árvore; recompile os dois |
| `insmod` falha no kernel do Debian (6.12) | o módulo é para 7.3.4 | bootar com o kernel 7.3.4 (§9) ou testar dentro da VM do lab |
| `pfctl: Permission denied` | `/dev/pf` é 0600 root | rodar como root |
| `pfctl: Device not configured` | módulo carregado mas `pfctl -e` não foi feito | `sudo pfctl -e` |
| `virsh: cannot connect to qemu:///system` | libvirt parado | `sudo systemctl enable --now libvirtd` |
| `Cannot find device 'eth2'` | nomes de interface mudam | resolver com `ip route get <ip> | grep -o 'dev [a-z0-9]*'` (o gate 15 faz isso) |
| gate falha logo depois do deploy | `pf.ko` não copiado | o runner verifica `test -s /opt/pf/pf.ko`; ver `out/test-logs/<gate>.sh.log` |
| stress: `baseline throughput unavailable` | `iperf3` não está no servidor | `srv 'pkill -x iperf3; iperf3 -s -D'` e conferir `netstat -lnt \| grep 5201` |
| stress: `state churn` falha | o `bash` do guest não tem `/dev/tcp` | o gate usa `nc` como fallback; verificar `bash --version` na VM |
| a VM reinicia durante o stress | a taxa oferecida esgotou a memória do guest (backlog da workqueue) | baixar o `--rate`; ver [caveats.md §13](caveats.md) — o teto medido no lab é ~1 Gbit/s em um guest de 1 GB |
| `gcc-15` não encontrado | não existe no trixie | usar o gcc 14 padrão (§1) |

---

## 12. Limites conhecidos (leia antes de usar em produção)

- O port **não** é o PF do OpenBSD: há divergências documentadas em
  [`docs/pt-BR/caveats.md`](caveats.md) e na matriz
  [`docs/pt-BR/feature-matrix.md`](feature-matrix.md).
- Não há `pfsync`/CARP, IPsec/pfkey, `ftp-proxy`, PF por netns nem os
  schedulers `cbq`/`wfq`/`red` (removidos no upstream antes do OpenBSD 8.0).
- PF registra hooks netfilter em `init_net` — em um host que já use
  `nftables`, o tráfego passa pelos dois. O `bypass` do PF só vale para
  tráfego que entra pelo gancho do PF.
- O IPv6, o OS fingerprinting, `set state-policy`/`allow-opts` e os syncookies
  compilam, mas **não** são cobertos por gate (lacuna conhecida, §5 do
  `docs/pt-BR/vm-lab.md`).

---

## 13. Referências

| Documento | Conteúdo |
|---|---|
| [`docs/pt-BR/install.md`](install.md) | receita de build genérica (sem o passo a passo do Debian) |
| [`docs/pt-BR/vm-lab.md`](vm-lab.md) | o laboratório, topologia e gates |
| [`docs/pt-BR/feature-matrix.md`](feature-matrix.md) | o que está verificado e o que não está |
| [`docs/pt-BR/caveats.md`](caveats.md) | divergências conhecidas do OpenBSD 8.0 |
| [`docs/pt-BR/pf-guide.md`](pf-guide.md) | regrasets do dia a dia e avisos |
| English mirror | [`docs/en/debian13.md`](../en/debian13.md) |
