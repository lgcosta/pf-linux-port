#!/usr/bin/env python3
"""Print the last numeric value of <key> anywhere in a JSON document.

iperf3 -J nests the summary numbers (end.sum_received.bits_per_second,
end.sum.lost_percent, ...) and the keys repeat per stream, so "the last
occurrence" is the summary value.
"""
import json
import sys


def walk(node, key, acc):
    if isinstance(node, dict):
        for k, v in node.items():
            if k == key and isinstance(v, (int, float)):
                acc.append(v)
            else:
                walk(v, key, acc)
    elif isinstance(node, list):
        for v in node:
            walk(v, key, acc)


def main():
    try:
        doc = json.load(open(sys.argv[1]))
    except Exception:
        print("")
        return
    acc = []
    walk(doc, sys.argv[2], acc)
    print(acc[-1] if acc else "")


if __name__ == "__main__":
    main()
