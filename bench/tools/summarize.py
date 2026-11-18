#!/usr/bin/env python3
"""Summarize bench data: CSV results -> per-scenario medians + raw samples.
Usage: summarize.py <results.csv> [--json <out.json>]"""
import csv, json, statistics, sys, collections

def load(path):
    rows = list(csv.DictReader(open(path)))
    return rows

def summarize(rows):
    # group by (scenario, backend, metric)
    g = collections.defaultdict(list)
    for r in rows:
        g[(r["scenario"], r["backend"], r["metric"])].append(
            {"rep": int(r["rep"]), "value": float(r["value"]), "unit": r["unit"]})
    out = {}
    for k, v in sorted(g.items()):
        vals = [x["value"] for x in v]
        med = statistics.median(vals)
        spread = (max(vals) - min(vals)) / 2 if len(vals) > 1 else 0.0
        out[k] = {"n": len(vals), "median": round(med, 4),
                  "min": round(min(vals), 4), "max": round(max(vals), 4),
                  "spread": round(spread, 4), "unit": v[0]["unit"],
                  "samples": vals}
    return out

def main():
    rows = load(sys.argv[1])
    data = summarize(rows)
    if "--json" in sys.argv:
        json.dump(data, open(sys.argv[sys.argv.index("--json") + 1], "w"), indent=1)
    print(json.dumps(data, indent=1))

if __name__ == "__main__":
    main()
