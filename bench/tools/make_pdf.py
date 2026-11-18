#!/usr/bin/env python3
"""bench/tools/make_pdf.py — render the benchmark PDF (A4) from the same data.

Reads bench/report/data/{summary.json,meta.json,results.csv} — i.e. exactly
what the Markdown report and the site use — and writes
bench/report/benchmark.pdf.
"""
import json
import os

from reportlab.graphics.shapes import Drawing, Rect, String, Line
from reportlab.lib import colors
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.platypus import (KeepTogether, PageBreak, Paragraph,
                                SimpleDocTemplate, Spacer, Table, TableStyle)

HERE = os.path.dirname(os.path.abspath(__file__))
BENCH = os.path.dirname(HERE)
DATA = os.path.join(BENCH, "report", "data")
PDF = os.path.join(BENCH, "report", "benchmark.pdf")

BACKENDS = ["baseline", "pf", "nft", "ipt-nft", "ipt-legacy"]
LABEL = {
    "baseline": "baseline",
    "pf": "PF-on-Linux",
    "nft": "nftables",
    "ipt-nft": "iptables-nft",
    "ipt-legacy": "iptables-legacy",
}
SCENARIOS = [
    ("stateless", "Filtragem sem estado"),
    ("stateful", "Filtragem com estado"),
    ("nat", "NAT com estado (SNAT)"),
    ("scale-start", "Escala de regras — acerto no início"),
    ("scale-middle", "Escala de regras — acerto no meio"),
    ("scale-end", "Escala de regras — acerto no fim"),
    ("reload", "Atualização de regras sob tráfego"),
]


def med(summary, sc, be, mt):
    return summary.get(f"{sc}|{be}|{mt}", {}).get("median")


def f(v, nd=2):
    return "—" if v is None else f"{v:.{nd}f}"


def tbl(rows, colw, header_bg=colors.HexColor("#e8eef5")):
    t = Table(rows, colWidths=colw, hAlign="LEFT")
    t.setStyle(TableStyle([
        ("FONT", (0, 0), (-1, -1), "Helvetica", 8),
        ("FONT", (0, 0), (-1, 0), "Helvetica-Bold", 8),
        ("BACKGROUND", (0, 0), (-1, 0), header_bg),
        ("GRID", (0, 0), (-1, -1), 0.4, colors.HexColor("#999999")),
        ("ALIGN", (1, 0), (-1, -1), "RIGHT"),
        ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
        ("TOPPADDING", (0, 0), (-1, -1), 2),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 2),
    ]))
    return t


def bars(values, labels, title, unit, width=430, height=110):
    d = Drawing(width, height)
    d.add(String(0, height - 10, title, fontName="Helvetica", fontSize=8))
    vmax = max(values + [0.0001])
    n = len(values)
    bw = (width - 60) / n
    base = 18
    for i, v in enumerate(values):
        h = (v / vmax) * (height - 40)
        d.add(Rect(40 + i * bw, base, bw * 0.7, max(h, 0.5),
                   fillColor=colors.HexColor(["#4c78a8", "#f58518", "#54a24b",
                                              "#e45756", "#b279a2"][i % 5]),
                   strokeColor=None))
        d.add(String(40 + i * bw, base - 9, labels[i][:12],
                     fontName="Helvetica", fontSize=6))
    d.add(Line(40, base, width - 10, base, strokeColor=colors.HexColor("#999999")))
    d.add(String(width - 90, base - 9, f"max {vmax:.2f} {unit}",
                 fontName="Helvetica", fontSize=6))
    return d


def main():
    summary = json.load(open(os.path.join(DATA, "summary.json")))
    meta = json.load(open(os.path.join(DATA, "meta.json")))

    ss = getSampleStyleSheet()
    h1 = ParagraphStyle("h1", parent=ss["Heading1"], fontSize=15, spaceAfter=4)
    h2 = ParagraphStyle("h2", parent=ss["Heading2"], fontSize=11, spaceBefore=8,
                        spaceAfter=3)
    body = ParagraphStyle("body", parent=ss["BodyText"], fontSize=8.5,
                          leading=11.5, spaceAfter=4)
    small = ParagraphStyle("small", parent=body, fontSize=7.5, leading=9.5,
                           textColor=colors.HexColor("#555555"))

    doc = SimpleDocTemplate(PDF, pagesize=A4, leftMargin=16 * mm,
                            rightMargin=16 * mm, topMargin=14 * mm,
                            bottomMargin=14 * mm,
                            title="PF-on-Linux benchmark",
                            author="PF-on-Linux project")
    E = []
    E.append(Paragraph("Benchmark: PF-on-Linux vs nftables, iptables-nft e "
                       "iptables-legacy", h1))
    E.append(Paragraph(
        f"Commit <b>{meta.get('run', '?')}</b> · run <b>{meta.get('run')}</b> · "
        f"gerado em {meta.get('generated', '')[:19]} · "
        f"{meta.get('rows', 0)} amostras", small))
    E.append(Spacer(1, 4))

    pf = med(summary, "stateless", "pf", "tcp_gbit")
    nft = med(summary, "stateless", "nft", "tcp_gbit")
    base = med(summary, "ceiling", "baseline", "tcp_gbit")
    nft_line = med(summary, "ceiling", "nft", "tcp_gbit")
    lat_pf = med(summary, "stateless", "pf", "latency_p99")
    lat_base = med(summary, "stateless", "baseline", "latency_p99")
    E.append(Paragraph("Resumo executivo (EN)", h2))
    E.append(Paragraph(
        f"At the 400 Mbit/s offered load (4 x 100 Mbit/s TCP) every backend "
        f"delivers the offered rate: nftables {f(nft)} Gbit/s, PF-on-Linux "
        f"{f(pf)} Gbit/s, no UDP loss. The p99 latency is {f(lat_pf)} ms "
        f"through PF-on-Linux against {f(lat_base)} ms unfiltered. Under "
        f"unbounded load the native backends reach {f(nft_line)} Gbit/s "
        f"(nftables) and {f(base)} Gbit/s (no filter); PF-on-Linux is not "
        "measurable there - its data path runs every packet through a single "
        "ordered workqueue, and above roughly 1 Gbit/s the backlog exhausts "
        "the guest (see the project caveats). Functionally the port passed "
        "every row the harness completed (IPv4 filtering, stateful filtering, "
        "address sets); SNAT reads <i>limited</i> and the netfilter rows were "
        "not measured.", body))

    E.append(Paragraph("Metodologia", h2))
    E.append(Paragraph(
        "Três VMs num bridge isolado: bench-kedge (10.0.0.2) → bench-fw "
        "(10.0.0.1 / 10.0.1.1, ip_forward=1) → bench-srv (10.0.1.2). O firewall "
        "boota o kernel 7.3.4 do projeto com NF_TABLES, NFT_* e "
        "IP_NF_IPTABLES_LEGACY, de modo que todos os backends rodam no mesmo "
        "kernel e na mesma VM. Apenas um backend ativo por medição. Carga "
        "oferecida: TCP iperf3 -P4 -b 100M (400 Mbit/s) por 5 s; UDP -b 200M "
        "-l 1400; latência por 200 pings a 10 ms; conexões/s por connect/close "
        "TCP. 3 repetições por célula, com a ordem dos backends invertida a "
        "cada repetição.", body))

    for sc, title in SCENARIOS:
        if not any(k.startswith(sc + "|") for k in summary):
            continue
        block = [Paragraph(title, h2)]
        if sc == "reload":
            rows = [["backend", "aplicação (ms)", "perda (%)"]]
            for b in BACKENDS:
                rows.append([LABEL[b], f(med(summary, sc, b, "apply_ms"), 2),
                             f(med(summary, sc, b, "loss"), 2)])
            block.append(tbl(rows, [70, 60, 60]))
        else:
            rows = [["backend", "Gbit/s", "p50 (ms)", "p99 (ms)",
                     "conn/s" if sc == "stateful" else "pps UDP",
                     "perda UDP (%)" if sc == "stateless" else "—"]]
            for b in BACKENDS:
                rows.append([
                    LABEL[b],
                    f(med(summary, sc, b, "tcp_gbit"), 3),
                    f(med(summary, sc, b, "latency_p50"), 3),
                    f(med(summary, sc, b, "latency_p99"), 3),
                    f(med(summary, sc, b, "conn_per_s"), 1) if sc == "stateful"
                    else f(med(summary, sc, b, "udp_pps"), 0),
                    f(med(summary, sc, b, "udp_loss"), 2) if sc == "stateless"
                    else "—",
                ])
            block.append(tbl(rows, [70, 45, 45, 45, 50, 50]))
        E.append(KeepTogether(block))

    E.append(PageBreak())
    E.append(Paragraph("Vazão TCP entregue (carga limitada a 400 Mbit/s)", h2))
    E.append(bars([med(summary, "stateless", b, "tcp_gbit") or 0
                   for b in BACKENDS], [LABEL[b] for b in BACKENDS],
                  "Filtragem sem estado — Gbit/s", "Gbit/s"))
    E.append(Spacer(1, 6))
    E.append(bars([med(summary, "nat", b, "tcp_gbit") or 0 for b in BACKENDS],
                  [LABEL[b] for b in BACKENDS], "NAT com estado — Gbit/s",
                  "Gbit/s"))

    E.append(Paragraph("Limitações", h2))
    for b in [
        "Gerador de tráfego e firewall dividem o mesmo hipervisor: a vazão "
        "absoluta é limitada pela CPU do host, não por um enlace físico.",
        "O baseline sem filtro também atravessa o caminho de roteamento do "
        "Linux; a diferença para os backends isola o custo do filtro.",
        "PF-on-Linux não sustenta carga ilimitada: acima de ~1 Gbit/s o "
        "backlog da workqueue esgota a memória do guest e o kernel entra em "
        "pânico (docs/pt-BR/caveats.md §13). Por isso a carga é limitada.",
        "IPv6 foi verificado apenas funcionalmente, não no benchmark.",
    ]:
        E.append(Paragraph("• " + b, body))

    E.append(Paragraph("Reprodução e dados", h2))
    E.append(Paragraph(
        "Scripts e amostras: <b>bench/</b>. "
        "<font face='Courier'>./bench/lab/provision.sh</font> cria as VMs; "
        "<font face='Courier'>./bench/run.sh</font> varre; "
        "<font face='Courier'>python3 bench/tools/report.py</font> regenera "
        "Markdown, site e PDF a partir do mesmo CSV. Amostras brutas em "
        "bench/report/data/results.csv.", body))

    doc.build(E)
    print("wrote", PDF)


if __name__ == "__main__":
    main()
