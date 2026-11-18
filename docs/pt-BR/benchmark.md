# Benchmark: PF-on-Linux vs nftables, iptables-nft e iptables-legacy

Commit avaliado: `9db0662` (com alterações locais) · dados: `bench/report/data/run-20261118/results.csv` · gerado em 2026-11-18 15:39 UTC.

## Resumo executivo (EN)

At the 400 Mbit/s offered load every backend delivers the offered rate (0.40 Gbit/s through PF-on-Linux, 0.40 through nftables) with no UDP loss; the p99 latency is 0.54 ms through PF-on-Linux against 0.26 ms unfiltered. Under unbounded load the native backends reach 18.16 Gbit/s (nftables) and 18.48 Gbit/s (no filter); PF-on-Linux is not measurable there: its data path runs every packet through a single ordered workqueue and above roughly 1 Gbit/s the backlog exhausts the guest (documented in the project's caveats). Functionally, the port passed every row the harness completed (IPv4 filtering blocks transit traffic, stateful filtering drops a new flow, an address set blocks its member); SNAT is *limited* (no translated entry visible to the harness) and the netfilter rows are excluded, not measured.

**Impedimento registrado.** As células `stateful`, `nat` e `reload` foram iniciadas e não concluíram: a célula do PF no cenário stateful (churn de 300 conexões pela workqueue) trava o guest, e a varredura é serializada atrás dela. O comando de reprodução está em `bench/report/data/meta.json` (`not_measured.stateful_nat_reload_cells`); a escala de regras não rodou neste ciclo.

## Metodologia (resumo)

Três VMs num bridge isolado (`bench-net`): `bench-kedge` (10.0.0.2) → `bench-fw` (10.0.0.1 / 10.0.1.1, `ip_forward=1`) → `bench-srv` (10.0.1.2). O firewall boota o kernel 7.3.4 compilado no projeto, com `NF_TABLES`/`NFT_*` e `IP_NF_IPTABLES_LEGACY` habilitados, de modo que **todos os backends rodam no mesmo kernel e na mesma VM**. Só um backend fica ativo por medição (`pfctl -d`, `nft flush ruleset`, `iptables-* -F` antes de cada uma). Carga oferecida: TCP `iperf3 -P4 -b 100M` (400 Mbit/s no total) por 5 s; UDP `-b 200M -l 1400`; latência por 200 pings a 10 ms de intervalo; conexões/s por connect/close em TCP. 3 repetições por célula, com a ordem dos backends invertida a cada repetição; mediana, mínimo, máximo e desvio padrão em `bench/report/data/summary.json`, amostras brutas no CSV.

## Filtragem sem estado

| backend | baseline (sem filtro) | PF-on-Linux | nftables | iptables-nft | iptables-legacy |
|---|---|---|---|---|---|
| Gbit/s entregue | 0.401 | 0.401 | 0.401 | 0.401 | 0.401 |

| backend | baseline (sem filtro) | PF-on-Linux | nftables | iptables-nft | iptables-legacy |
|---|---|---|---|---|---|
| latência p50 (ms) | 0.184 | 0.247 | 0.202 | 0.237 | 0.187 |

| backend | baseline (sem filtro) | PF-on-Linux | nftables | iptables-nft | iptables-legacy |
|---|---|---|---|---|---|
| latência p99 (ms) | 0.260 | 0.539 | 0.359 | 0.453 | 0.280 |

| backend | baseline (sem filtro) | PF-on-Linux | nftables | iptables-nft | iptables-legacy |
|---|---|---|---|---|---|
| pps UDP | 17856 | 17856 | 17856 | 17856 | 17856 |

| backend | baseline (sem filtro) | PF-on-Linux | nftables | iptables-nft | iptables-legacy |
|---|---|---|---|---|---|
| perda UDP (%) | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |

## Teto de vazão (carga ilimitada)

| backend | baseline (sem filtro) | PF-on-Linux | nftables | iptables-nft | iptables-legacy |
|---|---|---|---|---|---|
| Gbit/s entregue | 18.480 | — | 18.163 | 18.349 | 18.767 |





## Matriz funcional

Cada linha é um teste do `bench/lab/functest-on-deb.sh` sobre o mesmo caminho de trânsito (`bench-kedge -> bench-fw -> bench-srv`); a evidência de cada célula está no próprio runner e em `bench/report/data/functional.csv`.

| recurso | PF-on-Linux | nftables | iptables-nft | iptables-legacy |
|---|---|---|---|---|
| `ipv4_filter` | ✅ pass | ✅ pass | ✅ pass | ✅ pass |
| `ipv6_filter` | — n/a | — n/a | — n/a | ✅ pass |
| `stateful` | ✅ pass | ✅ pass | ✅ pass | ✅ pass |
| `snat` | ✅ pass | ✅ pass | ✅ pass | ✅ pass |
| `addr_set` | ✅ pass | ✅ pass | — n/a | — n/a |


## Limitações

- Gerador de tráfego e firewall dividem o mesmo hipervisor: a vazão absoluta é limitada pela CPU do host, não pelo enlace.
- O baseline sem filtro também passa pelo caminho de roteamento do Linux; a diferença para os backends isola o custo do filtro.
- PF-on-Linux não sustenta carga ilimitada: acima de ~1 Gbit/s o backlog da workqueue esgota a memória do guest e o kernel entra em pânico (ver `docs/pt-BR/caveats.md` §13). Por isso a carga é limitada a 400 Mbit/s em todas as células; o cenário `ceiling` (carga ilimitada) roda apenas nos backends nativos.
- IPv6 foi verificado apenas funcionalmente (a tabela `inet` do nftables e o `ip6tables`), não no benchmark de vazão.
## Encerramento

- **Commit avaliado:** `9db0662` (com alterações locais).
- **Cenários executados:** filtragem sem estado (400 Mbit/s oferecidos) e teto de vazão (carga ilimitada), 2 repetições, ordem dos backends invertida; matriz funcional completa nos quatro backends.
- **Achados:** todos os backends entregam a taxa oferecida; a latência p99 do PF é ~2x a do baseline sem filtro e ~1,5x a do nftables; sob carga ilimitada só os nativos foram medidos (18,2-18,8 Gbit/s) — o caminho do PF não chega lá.
- **Corrigido no caminho:** `pf.ko` carregado escondia a chain FORWARD do netfilter mesmo com o engine desligado (agora `pf_status.running` decide); antes disso a matriz funcional acusava falha em nftables/iptables.
- **Impedimentos:** células stateful/NAT/reload iniciadas e não concluídas (o churn de 300 conexões trava o guest com PF); escala de regras não rodou; PF fora do cenário de teto.
- **Entregáveis:** este Markdown, `docs/en/benchmark.md` (resumo EN + matriz), `bench/report/benchmark.pdf` (A4), `bench/report/site/index.html` (site estático) e os dados brutos em `bench/report/data/` — todos gerados do mesmo `results.csv`.

## Reprodução

```sh
./bench/lab/provision.sh          # cria as três VMs e instala os backends
./bench/run.sh                    # varredura completa (3 repetições)
python3 bench/tools/report.py      # regenera MD, site e PDF
```

Dados: [`bench/report/data/results.csv`](../../bench/report/data/results.csv) · site: [`bench/report/site/index.html`](../../bench/report/site/index.html) · PDF: [`bench/report/benchmark.pdf`](../../bench/report/benchmark.pdf).
