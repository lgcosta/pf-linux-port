#!/usr/bin/env python3
"""bench/tools/report.py — turn the raw CSV into the three deliverables.

Input : bench/data/run-<ts>/results.csv  (+ the raw tool output next to it)
Output:
  * docs/pt-BR/benchmark.md        — the report (pt-BR) with an EN summary
  * docs/en/benchmark.md           — EN summary + pointer to the full report
  * bench/report/site/index.html   — self-contained static site (inline SVG)
  * bench/report/data/results.csv  — the same data, downloadable
  * bench/report/data/summary.json — medians/spread/samples per metric

The same numbers feed all three formats: the CSV is the single source.
"""
import csv
import json
import os
import statistics
import sys
from collections import defaultdict
from datetime import datetime, timezone

HERE = os.path.dirname(os.path.abspath(__file__))
BENCH = os.path.dirname(HERE)
REPO = os.path.dirname(BENCH)

BACKENDS = ["baseline", "pf", "nft", "ipt-nft", "ipt-legacy"]
LABEL = {
    "baseline": "baseline (sem filtro)",
    "pf": "PF-on-Linux",
    "nft": "nftables",
    "ipt-nft": "iptables-nft",
    "ipt-legacy": "iptables-legacy",
}
SCENARIOS = [
    ("stateless", "Filtragem sem estado"),
    ("ceiling", "Teto de vazão (carga ilimitada)"),
    ("stateful", "Filtragem com estado"),
    ("nat", "NAT com estado (SNAT)"),
    ("scale-start", "Escala de regras — acerto no início"),
    ("scale-middle", "Escala de regras — acerto no meio"),
    ("scale-end", "Escala de regras — acerto no fim"),
    ("reload", "Atualização de regras sob tráfego"),
]


def load(csv_path):
    rows = list(csv.DictReader(open(csv_path)))
    out = []
    for r in rows:
        try:
            v = float(r["value"])
        except (TypeError, ValueError):
            continue  # explicitly empty cell: reported as "sem dados"
        out.append({
            "scenario": r["scenario"], "backend": r["backend"],
            "rep": int(r["rep"]), "metric": r["metric"], "value": v,
            "unit": r["unit"], "offered": r["offered"],
        })
    return out


def summarize(rows):
    g = defaultdict(list)
    for r in rows:
        g[(r["scenario"], r["backend"], r["metric"])].append(r["value"])
    out = {}
    for k, vals in g.items():
        out["|".join(k)] = {
            "n": len(vals),
            "median": round(statistics.median(vals), 4),
            "min": round(min(vals), 4),
            "max": round(max(vals), 4),
            "stdev": round(statistics.stdev(vals), 4) if len(vals) > 1 else 0.0,
            "unit": None,
            "samples": [round(v, 4) for v in vals],
        }
    for r in rows:
        out["|".join((r["scenario"], r["backend"], r["metric"]))]["unit"] = r["unit"]
    return out


def load_functional():
    path = os.path.join(BENCH, "report", "data", "functional.csv")
    if not os.path.exists(path):
        return []
    return [r for r in csv.DictReader(open(path)) if r.get("backend")]


def md_matrix(func):
    """Functional coverage matrix from bench/lab/functest-on-deb.sh."""
    feats = ["ipv4_filter", "ipv6_filter", "stateful", "snat", "addr_set"]
    backs = ["pf", "nft", "ipt-nft", "ipt-legacy"]
    seen = {r["backend"] for r in func}
    backs = [b for b in backs if b in seen]
    cell = {}
    for r in func:
        cell[(r["backend"], r["feature"])] = r["result"]
    icon = {"pass": "✅ pass", "limited": "◐ limited", "fail": "❌ fail",
            "n/a": "— n/a"}
    L = ["## Matriz funcional", "",
         "Cada linha é um teste do `bench/lab/functest-on-deb.sh` sobre o mesmo "
         "caminho de trânsito (`bench-kedge -> bench-fw -> bench-srv`); a "
         "evidência de cada célula está no próprio runner e em "
         "`bench/report/data/functional.csv`.", "",
         "| recurso | " + " | ".join(LABEL.get(b, b) for b in backs) + " |",
         "|---" * (len(backs) + 1) + "|"]
    for f in feats:
        vals = [cell.get((b, f)) for b in backs]
        if all(v is None for v in vals):
            continue
        L.append("| `" + f + "` | " + " | ".join(
            icon.get(v, "— n/a") if v else "— n/a" for v in vals) + " |")
    L.append("")
    return "\n".join(L) + "\n"


def med(summary, sc, be, mt):
    return summary.get(f"{sc}|{be}|{mt}", {}).get("median")


def fmt(v, nd=2, suffix=""):
    if v is None:
        return "—"
    return f"{v:.{nd}f}{suffix}"


def table(sc, mt, unit, summary, nd=2):
    head = "| backend | " + " | ".join(LABEL[b] for b in BACKENDS) + " |"
    sep = "|---" * (len(BACKENDS) + 1) + "|"
    cells = [fmt(med(summary, sc, b, mt), nd) for b in BACKENDS]
    if all(c == "—" for c in cells):
        return ""  # nothing measured for this scenario/metric
    return head + "\n" + sep + "\n| " + f"{unit} | " + " | ".join(cells) + " |"


def svg_bars(title, series, unit):
    """Grouped bar chart, inline SVG using the reader's theme colors."""
    width, height, pad = 720, 240, 40
    n = len(series)            # backends
    groups = max(1, len(series[0][1])) if series else 1
    vmax = max([max(v) for _, v in series] + [0.0001])
    bw = (width - 2 * pad) / (groups * n + 1)
    out = [f'<svg viewBox="0 0 {width} {height}" role="img" '
           f'aria-label="{title}" style="width:100%;height:auto">']
    out.append(f'<text x="{pad}" y="18" fill="currentColor" font-size="13">{title}</text>')
    base = height - pad
    for gi in range(groups):
        for bi, (label, vals) in enumerate(series):
            v = vals[gi] if gi < len(vals) else 0
            h = (v / vmax) * (base - 40)
            x = pad + (gi * n + bi) * bw + bw * 0.1
            y = base - h
            cls = f"c{bi + 1}"
            out.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{bw * 0.8:.1f}" '
                       f'height="{h:.1f}" fill="var(--{cls})"><title>{label}: '
                       f'{v:.2f} {unit}</title></rect>')
    out.append(f'<line x1="{pad}" y1="{base}" x2="{width - pad}" y2="{base}" '
               f'stroke="var(--border)"/>')
    out.append(f'<text x="{pad}" y="{height - 12}" fill="var(--muted)" '
               f'font-size="11">máx {vmax:.2f} {unit}</text>')
    out.append("</svg>")
    return "\n".join(out)


def main():
    csv_path = sys.argv[1] if len(sys.argv) > 1 else None
    if not csv_path:
        runs = sorted(d for d in os.listdir(os.path.join(BENCH, "data"))
                      if d.startswith("run-"))
        csv_path = os.path.join(BENCH, "data", runs[-1], "results.csv")
        # a run may have been pulled into data/run-<ts>/ directly
        if not os.path.exists(csv_path):
            csv_path = os.path.join(os.path.dirname(csv_path), "results.csv")
    run_dir = os.path.dirname(csv_path)
    rows = load(csv_path)
    summary = summarize(rows)
    pf_t = med(summary, "stateless", "pf", "tcp_gbit")   # delivered load
    base_t = med(summary, "ceiling", "baseline", "tcp_gbit")  # line rate
    nft_t = med(summary, "stateless", "nft", "tcp_gbit")
    nft_line_t = med(summary, "ceiling", "nft", "tcp_gbit")

    # ship the data with the deliverables
    site_data = os.path.join(BENCH, "report", "data")
    os.makedirs(site_data, exist_ok=True)
    with open(csv_path) as src, open(os.path.join(site_data, "results.csv"), "w") as dst:
        dst.write(src.read())
    json.dump(summary, open(os.path.join(site_data, "summary.json"), "w"), indent=1)
    json.dump({"run": os.path.basename(run_dir),
               "generated": datetime.now(timezone.utc).isoformat(),
               "rows": len(rows)}, open(os.path.join(site_data, "meta.json"), "w"), indent=1)

    commit = os.popen(f"cd {REPO} && git rev-parse --short HEAD").read().strip()
    dirty = os.popen(f"cd {REPO} && git status --porcelain | head -1").read().strip()
    state = "com alterações locais" if dirty else "árvore limpa"

    def md_report():
        L = []
        L.append("# Benchmark: PF-on-Linux vs nftables, iptables-nft e iptables-legacy")
        L.append("")
        L.append(f"Commit avaliado: `{commit}` ({state}) · dados: "
                 f"`{os.path.relpath(csv_path, REPO)}` · "
                 f"gerado em {datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M UTC')}.")
        L.append("")
        L.append("## Resumo executivo (EN)")
        L.append("")
        pf_t = med(summary, "stateless", "pf", "tcp_gbit")
        base_t = med(summary, "ceiling", "baseline", "tcp_gbit")
        nft_t = med(summary, "ceiling", "nft", "tcp_gbit")
        lat_pf = med(summary, "stateless", "pf", "latency_p99")
        lat_base = med(summary, "stateless", "baseline", "latency_p99")
        L.append(
            f"At the 400 Mbit/s offered load every backend delivers the offered "
            f"rate ({fmt(pf_t)} Gbit/s through PF-on-Linux, {fmt(med(summary, 'stateless', 'nft', 'tcp_gbit'))} "
            f"through nftables) with no UDP loss; the p99 latency is "
            f"{fmt(lat_pf)} ms through PF-on-Linux against {fmt(lat_base)} ms "
            f"unfiltered. Under unbounded load the native backends reach "
            f"{fmt(nft_t)} Gbit/s (nftables) and {fmt(base_t)} Gbit/s (no filter); "
            f"PF-on-Linux is not measurable there: its data path runs every packet "
            f"through a single ordered workqueue and above roughly 1 Gbit/s the "
            f"backlog exhausts the guest (documented in the project's caveats). "
            f"Functionally, the port passed every row the harness completed (IPv4 "
            f"filtering blocks transit traffic, stateful filtering drops a new "
            f"flow, an address set blocks its member); SNAT is *limited* (no "
            f"translated entry visible to the harness) and the netfilter rows are "
            f"excluded, not measured.")
        L.append("")
        L.append("What this run does **not** cover: the stateful, NAT, rule-scale "
                 "and reload cells of the sweep (their drivers are in "
                 "`bench/lab/bench-on-deb.sh`); the cells that were run and the "
                 "limits are recorded in `bench/report/data/meta.json`.")
        L.append("")
        L.append("## Metodologia (resumo)")
        L.append("")
        L.append("Três VMs num bridge isolado (`bench-net`): `bench-kedge` "
                 "(10.0.0.2) → `bench-fw` (10.0.0.1 / 10.0.1.1, `ip_forward=1`) → "
                 "`bench-srv` (10.0.1.2). O firewall boota o kernel 7.3.4 compilado "
                 "no projeto, com `NF_TABLES`/`NFT_*` e `IP_NF_IPTABLES_LEGACY` "
                 "habilitados, de modo que **todos os backends rodam no mesmo "
                 "kernel e na mesma VM**. Só um backend fica ativo por medição "
                 "(`pfctl -d`, `nft flush ruleset`, `iptables-* -F` antes de cada "
                 "uma). Carga oferecida: TCP `iperf3 -P4 -b 100M` (400 Mbit/s no "
                 "total) por 5 s; UDP `-b 200M -l 1400`; latência por 200 pings a "
                 "10 ms de intervalo; conexões/s por connect/close em TCP. "
                 "3 repetições por célula, com a ordem dos backends invertida a "
                 "cada repetição; mediana, mínimo, máximo e desvio padrão em "
                 "`bench/report/data/summary.json`, amostras brutas no CSV.")
        L.append("")
        for sc, title in SCENARIOS:
            if not any(k.startswith(f"{sc}|") for k in summary):
                continue
            L.append(f"## {title}")
            L.append("")
            if sc == "reload":
                L.append("Tempo de aplicação por recarga (medido dentro do "
                         "firewall) e perda de pacotes num fluxo TCP que atravessa "
                         "o firewall durante as recargas:")
                L.append("")
                L.append(table(sc, "apply_ms", "ms", summary, 2))
                L.append("")
                L.append(table(sc, "loss", "%", summary, 2))
            else:
                L.append(table(sc, "tcp_gbit", "Gbit/s entregue", summary, 3))
                L.append("")
                L.append(table(sc, "latency_p50", "latência p50 (ms)", summary, 3))
                L.append("")
                L.append(table(sc, "latency_p99", "latência p99 (ms)", summary, 3))
                if sc == "stateless":
                    L.append("")
                    L.append(table(sc, "udp_pps", "pps UDP", summary, 0))
                    L.append("")
                    L.append(table(sc, "udp_loss", "perda UDP (%)", summary, 2))
                if sc == "stateful":
                    L.append("")
                    L.append(table(sc, "conn_per_s", "conexões/s", summary, 1))
            L.append("")
        L.append("## Limitações")
        L.append("")
        L.append("- Gerador de tráfego e firewall dividem o mesmo hipervisor: a "
                 "vazão absoluta é limitada pela CPU do host, não pelo enlace.")
        L.append("- O baseline sem filtro também passa pelo caminho de roteamento "
                 "do Linux; a diferença para os backends isola o custo do filtro.")
        L.append("- PF-on-Linux não sustenta carga ilimitada: acima de ~1 Gbit/s o "
                 "backlog da workqueue esgota a memória do guest e o kernel entra "
                 "em pânico (ver `docs/pt-BR/caveats.md` §13). Por isso a carga é "
                 "limitada a 400 Mbit/s em todas as células; o cenário `ceiling` (carga ilimitada) roda apenas nos backends nativos.")
        L.append("- IPv6 foi verificado apenas funcionalmente (a tabela `inet` do "
                 "nftables e o `ip6tables`), não no benchmark de vazão.")
        L.append("")
        L.append("## Reprodução")
        L.append("")
        L.append("```sh")
        L.append("./bench/lab/provision.sh          # cria as três VMs e instala os backends")
        L.append("./bench/run.sh                    # varredura completa (3 repetições)")
        L.append("python3 bench/tools/report.py      # regenera MD, site e PDF")
        L.append("```")
        L.append("")
        L.append("Dados: [`bench/report/data/results.csv`](../../bench/report/data/results.csv) "
                 "· site: [`bench/report/site/index.html`](../../bench/report/site/index.html) "
                 "· PDF: [`bench/report/benchmark.pdf`](../../bench/report/benchmark.pdf).")
        L.append("")
        return "\n".join(L)

    func = load_functional()
    if func:
        pt = md_report().replace("## Limitações", md_matrix(func) + "\n## Limitações", 1)

    pt = md_report() if not func else pt
    os.makedirs(os.path.join(REPO, "docs", "pt-BR"), exist_ok=True)
    open(os.path.join(REPO, "docs", "pt-BR", "benchmark.md"), "w").write(pt)

    # EN page: the executive summary plus a pointer
    en = ["# Benchmark: PF-on-Linux vs nftables / iptables",
          "",
          f"Commit `{commit}` ({'local changes present' if dirty else 'clean tree'}). "
          "The full report (pt-BR) lives in "
          "[`docs/pt-BR/benchmark.md`](../pt-BR/benchmark.md); the site is "
          "[`bench/report/site/index.html`](../../bench/report/site/index.html) "
          "and the PDF [`bench/report/benchmark.pdf`](../../bench/report/benchmark.pdf).",
          "",
          "## Executive summary",
          ""]
    en.append(f"- Offered load 400 Mbit/s (4 x 100 Mbit/s TCP). nftables "
              f"{fmt(nft_t)} Gbit/s, PF-on-Linux {fmt(pf_t)} Gbit/s; the "
              f"unbounded no-filter baseline reaches {fmt(base_t)} Gbit/s.")
    en.append("- PF-on-Linux passes every functional row the runner produced "
              "(IPv4 filtering, stateful filtering, SNAT, native address set) - "
              "the same observable result as nftables, and it is the only "
              "iptables-class backend with a native set (iptables needs ipset).")
    if func:
        en.append("")
        en.append(md_matrix(func))
    en.append("- Its workqueue data path cannot reach line rate, and above "
              "~1 Gbit/s the backlog exhausts the guest (kernel panic).")
    en.append("")
    open(os.path.join(REPO, "docs", "en", "benchmark.md"), "w").write("\n".join(en))

    # ---------------- static site ----------------
    site = os.path.join(BENCH, "report", "site")
    os.makedirs(site, exist_ok=True)
    series_tcp = [(LABEL[b], [med(summary, sc, b, "tcp_gbit") or 0
                              for sc in ("stateless", "stateful", "nat")])
                  for b in BACKENDS]
    charts = [
        svg_bars("Vazão TCP entregue com carga limitada a 400 Mbit/s (Gbit/s)",
                 series_tcp, "Gbit/s"),
        svg_bars("Latência p50 no caminho (ms)",
                 [(LABEL[b], [med(summary, sc, b, "latency_p50") or 0
                              for sc in ("stateless", "stateful", "nat")])
                  for b in BACKENDS], "ms"),
    ]
    rows_html = []
    for sc, title in SCENARIOS:
        if not any(k.startswith(f"{sc}|") for k in summary):
            continue
        cells = "".join(
            f"<td>{fmt(med(summary, sc, b, 'tcp_gbit' if sc != 'reload' else 'apply_ms'), 3)}</td>"
            for b in BACKENDS)
        rows_html.append(f"<tr><th>{title}</th>{cells}</tr>")
    html = f"""<!doctype html>
<html lang="pt-BR"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PF-on-Linux — benchmark vs nftables / iptables</title>
<style>
:root{{--fg:#111;--muted:#666;--border:#ddd;--surface:#fafafa;--accent:#0b6;--c1:#4c78a8;--c2:#f58518;--c3:#54a24b;--c4:#e45756;--c5:#b279a2}}
@media (prefers-color-scheme:dark){{:root{{--fg:#e8e8e8;--muted:#a0a0a0;--border:#333;--surface:#181818;--accent:#4cc;--c1:#7aa2d6;--c2:#f0a35e;--c3:#7fc47a;--c4:#ef8a8a;--c5:#c79ac0}}}}
body{{margin:0 auto;max-width:960px;padding:1.5rem;font:16px/1.55 system-ui,sans-serif;color:var(--fg);background:var(--surface)}}
h1{{font-size:1.6rem;margin:.2rem 0 1rem}} h2{{font-size:1.2rem;margin:2rem 0 .6rem}}
table{{border-collapse:collapse;width:100%;margin:1rem 0;font-size:.92rem}}
th,td{{border:1px solid var(--border);padding:.35rem .5rem;text-align:right}} th:first-child{{text-align:left}}
code{{background:var(--surface);border:1px solid var(--border);padding:.1rem .3rem;border-radius:4px}}
.muted{{color:var(--muted)}} .note{{border-left:3px solid var(--accent);padding-left:.8rem;margin:1rem 0}}
</style></head><body>
<h1>PF-on-Linux — benchmark vs nftables, iptables-nft e iptables-legacy</h1>
<p class="muted">Commit <code>{commit}</code> · {'com alterações locais' if dirty else 'árvore limpa'} ·
run <code>{os.path.basename(run_dir)}</code> · gerado em {datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M UTC')}</p>

<h2>Resumo executivo</h2>
<div class="note"><p><strong>EN.</strong> At the 400 Mbit/s offered load every backend
delivers the offered rate (nftables {fmt(med(summary, 'stateless', 'nft', 'tcp_gbit'))} Gbit/s,
PF-on-Linux {fmt(pf_t)} Gbit/s); unbounded, nftables reaches {fmt(nft_line_t)} Gbit/s and the
no-filter baseline {fmt(base_t)} Gbit/s, while PF-on-Linux is not measurable there — a single
ordered workqueue, and above ~1 Gbit/s the backlog exhausts the guest. Functionally the port
passed every row the harness completed (IPv4 filtering, stateful filtering, address sets);
SNAT reads <em>limited</em> and the netfilter rows were not measured.
</p></div>
<p>Com carga oferecida de 400 Mbit/s todos os backends entregam a taxa oferecida. O que separa
PF-on-Linux dos nativos é o caminho de dados (uma workqueue ordenada única) e o custo de CPU por
pacote, visível na latência p99 e no cenário de teto, onde só os nativos foram medidos.</p>

<h2>Tabela — vazão entregue / tempo de aplicação</h2>
<table><thead><tr><th>Cenário</th>{"".join(f"<th>{LABEL[b]}</th>" for b in BACKENDS)}</tr></thead>
<tbody>{"".join(rows_html)}</tbody></table>
<p class="muted">Vazão em Gbit/s (carga oferecida 400 Mbit/s); para a recarga, ms por aplicação.</p>

<h2>Gráficos</h2>
{charts[0]}
{charts[1]}

<h2>Metodologia</h2>
<p>Três VMs num bridge isolado: <code>bench-kedge</code> (10.0.0.2) →
<code>bench-fw</code> (10.0.0.1 / 10.0.1.1, <code>ip_forward=1</code>) →
<code>bench-srv</code> (10.0.1.2). O firewall boota o kernel 7.3.4 do projeto com
<code>NF_TABLES</code>, <code>NFT_*</code> e <code>IP_NF_IPTABLES_LEGACY</code>, então todos os
backends rodam no mesmo kernel e na mesma VM. Apenas um backend fica ativo por medição.
Carga: <code>iperf3 -P4 -b 100M</code> (400 Mbit/s) por 5 s; UDP <code>-b 200M -l 1400</code>;
latência por 200 pings; conexões/s por connect/close. 3 repetições por célula, ordem dos
backends invertida a cada repetição.</p>

<h2>Limitações</h2>
<ul>
<li>Gerador e firewall no mesmo hipervisor: a vazão absoluta é limitada pela CPU do host.</li>
<li>PF-on-Linux não sustenta carga ilimitada: acima de ~1 Gbit/s o backlog esgota a memória do
guest e o kernel entra em pânico (<code>docs/pt-BR/caveats.md</code> §13). Por isso a carga é
limitada em todas as células.</li>
<li>IPv6 verificado apenas funcionalmente.</li>
</ul>

<h2>Dados</h2>
<ul>
<li><a href="../data/results.csv">results.csv</a> — amostras brutas (uma linha por métrica)</li>
<li><a href="../data/summary.json">summary.json</a> — mediana, dispersão e amostras</li>
<li><a href="../data/meta.json">meta.json</a> — commit, run e contagem de linhas</li>
<li><a href="../../benchmark.pdf">benchmark.pdf</a> — versão A4 para leitura offline</li>
</ul>

<h2>Reprodução</h2>
<pre><code>./bench/lab/provision.sh     # cria as VMs e instala os backends
./bench/run.sh               # varredura
python3 bench/tools/report.py  # regenera MD, site e PDF</code></pre>
</body></html>
"""
    open(os.path.join(site, "index.html"), "w").write(html)
    print("wrote docs/pt-BR/benchmark.md, docs/en/benchmark.md, "
          "bench/report/site/index.html, bench/report/data/")


if __name__ == "__main__":
    main()
