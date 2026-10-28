#!/usr/bin/env python3
"""Run the socsim experiments and draw the charts used in the README.

    make                              # build ./build/socsim first
    python3 tools/experiments.py      # prints tables, writes results/*.csv and docs/images/*.png

Experiments
  1. TLB: time-slice length x (flush on context switch | ASID-tagged TLB)
  2. Thrashing: physical memory size x page-replacement policy (FIFO, LRU, Clock)
  3. Multi-core scaling: 1, 2, 4 cores
"""
import csv
import io
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SIM = ROOT / "build" / "socsim"
WORKLOAD = ROOT / "workloads" / "mixed.txt"
RESULTS = ROOT / "results"
IMAGES = ROOT / "docs" / "images"

# Validated categorical palette (slots 1-3) + text and surface tokens.
BLUE, ORANGE, AQUA = "#2a78d6", "#eb6834", "#1baf7a"
SURFACE, INK, INK2, GRID = "#fcfcfb", "#0b0b0b", "#52514e", "#e4e3df"


def simulate(*args):
    """Run one simulation and return its CSV line as a dict."""
    out = subprocess.run([str(SIM), str(WORKLOAD), "--csv", *map(str, args)],
                         capture_output=True, text=True, check=True).stdout
    header = subprocess.run([str(SIM), "--csv-header"], capture_output=True, text=True,
                            check=True).stdout
    return next(csv.DictReader(io.StringIO(header + out)))


def save(rows, name):
    RESULTS.mkdir(exist_ok=True)
    with open(RESULTS / name, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=rows[0].keys())
        w.writeheader()
        w.writerows(rows)


def exp_tlb():
    quanta = [100, 250, 500, 1000, 2000, 5000, 10000]
    rows = []
    print("\n1. TLB hit rate vs time slice (flush on switch vs ASID)")
    print(f"{'quantum':>8} {'flush':>8} {'ASID':>8} {'time saved':>11}")
    for q in quanta:
        flush = simulate("--quantum", q)
        asid = simulate("--quantum", q, "--asid")
        rows += [flush, asid]
        saved = 1 - int(asid["makespan"]) / int(flush["makespan"])
        print(f"{q:>8} {float(flush['tlb_hit_rate']):>8.1%} {float(asid['tlb_hit_rate']):>8.1%}"
              f" {saved:>10.1%}")
    save(rows, "tlb_asid.csv")
    return quanta, rows


def exp_thrashing():
    frames = [150, 200, 250, 300, 400, 500, 600, 700, 800, 1024]
    policies = ["fifo", "lru", "clock"]
    rows = []
    print("\n2. Page faults and CPU utilization vs physical memory")
    print(f"{'frames':>7}" + "".join(f" {p + ' faults':>13}" for p in policies) +
          "".join(f" {p + ' util':>11}" for p in policies))
    for fr in frames:
        line = [simulate("--frames", fr, "--replace", p) for p in policies]
        rows += line
        print(f"{fr:>7}" + "".join(f" {int(r['page_faults']):>13}" for r in line) +
              "".join(f" {float(r['utilization']):>11.1%}" for r in line))
    save(rows, "thrashing.csv")
    return frames, policies, rows


def exp_cores():
    rows = [simulate("--cores", c) for c in (1, 2, 4)]
    base = int(rows[0]["makespan"])
    print("\n3. Multi-core scaling (5 processes)")
    print(f"{'cores':>6} {'makespan':>12} {'speed-up':>9} {'utilization':>12}")
    for r in rows:
        print(f"{r['cores']:>6} {int(r['makespan']):>12} {base / int(r['makespan']):>8.2f}x"
              f" {float(r['utilization']):>11.1%}")
    save(rows, "cores.csv")


def style(ax, title):
    ax.set_facecolor(SURFACE)
    ax.set_title(title, loc="left", fontsize=10.5, color=INK, pad=10)
    ax.grid(axis="y", color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(GRID)
    ax.tick_params(colors=INK2, labelsize=9)
    ax.xaxis.label.set_color(INK2)
    ax.yaxis.label.set_color(INK2)


def plot(tlb, thrash):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        from matplotlib.ticker import PercentFormatter
    except ImportError:
        print("\n(matplotlib not installed - pip install matplotlib to get the charts)")
        return
    IMAGES.mkdir(parents=True, exist_ok=True)

    # 1. TLB chart
    quanta, rows = tlb
    flush = [float(r["tlb_hit_rate"]) * 100 for r in rows if r["asid"] == "0"]
    asid = [float(r["tlb_hit_rate"]) * 100 for r in rows if r["asid"] == "1"]
    fig, ax = plt.subplots(figsize=(7, 3.8), facecolor=SURFACE)
    style(ax, "TLB hit rate vs. time slice: flushing on every context switch hurts short slices")
    ax.plot(quanta, asid, color=BLUE, lw=2, marker="o", ms=5, label="ASID-tagged TLB")
    ax.plot(quanta, flush, color=ORANGE, lw=2, marker="o", ms=5, label="flush on switch")
    ax.set_xscale("log")
    ax.set_xticks(quanta, [f"{q:,}" for q in quanta])
    ax.minorticks_off()
    ax.set_xlabel("round-robin time slice (ticks, log scale)")
    ax.set_ylabel("TLB hit rate")
    ax.yaxis.set_major_formatter(PercentFormatter(decimals=0))
    ax.set_ylim(0, 100)
    ax.set_xlim(quanta[0] * 0.8, quanta[-1] * 1.25)
    # direct labels where the lines are furthest apart (short slices)
    ax.annotate("ASID tags: entries of all processes coexist", (quanta[0], asid[0]),
                xytext=(0, 10), textcoords="offset points", fontsize=9, color=INK)
    ax.annotate("flush: every switch empties the TLB", (quanta[0], flush[0]),
                xytext=(0, -16), textcoords="offset points", fontsize=9, color=INK)
    ax.legend(frameon=False, fontsize=9, loc="lower right", labelcolor=INK)
    fig.tight_layout()
    fig.savefig(IMAGES / "tlb_asid.png", dpi=130, facecolor=SURFACE)
    plt.close(fig)

    # 2. Thrashing: two panels (small multiples), never a dual axis
    frames, policies, rows = thrash
    colors = {"fifo": ORANGE, "lru": BLUE, "clock": AQUA}
    names = {"fifo": "FIFO", "lru": "LRU", "clock": "Clock"}
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(10, 3.9), facecolor=SURFACE)
    style(a1, "Page faults (log scale)")
    style(a2, "CPU utilization")
    for p in policies:
        pr = [r for r in rows if r["replace"] == p]
        faults = [int(r["page_faults"]) for r in pr]
        util = [float(r["utilization"]) * 100 for r in pr]
        a1.plot(frames, faults, color=colors[p], lw=2, marker="o", ms=4, label=names[p])
        a2.plot(frames, util, color=colors[p], lw=2, marker="o", ms=4, label=names[p])
    a1.set_yscale("log")
    a2.yaxis.set_major_formatter(PercentFormatter(decimals=0))
    a2.set_ylim(0, 100)
    for ax in (a1, a2):
        ax.set_xlabel("physical memory (4 KiB frames)")
        ax.axvspan(frames[0] - 20, 275, color="#f3d9cf", alpha=0.45, lw=0)
    a1.text(165, a1.get_ylim()[0] * 1.15, "thrashing", fontsize=9, color=INK2)
    a2.text(165, 92, "thrashing", fontsize=9, color=INK2)
    a1.legend(frameon=False, fontsize=9, labelcolor=INK)
    # direct labels on the utilization panel, where the policies separate
    fifo500 = next(float(r["utilization"]) * 100 for r in rows
                   if r["replace"] == "fifo" and r["frames"] == "500")
    lru400 = next(float(r["utilization"]) * 100 for r in rows
                  if r["replace"] == "lru" and r["frames"] == "400")
    a2.annotate("FIFO", (500, fifo500), xytext=(8, -4), textcoords="offset points",
                fontsize=9, color=INK)
    a2.annotate("LRU and Clock", (400, lru400), xytext=(-82, 6), textcoords="offset points",
                fontsize=9, color=INK)
    fig.suptitle("Too little memory: page faults explode and the CPUs sit idle waiting for the disk",
                 x=0.01, ha="left", fontsize=11, color=INK)
    fig.tight_layout()
    fig.savefig(IMAGES / "thrashing.png", dpi=130, facecolor=SURFACE)
    plt.close(fig)
    print(f"\ncharts written to {IMAGES.relative_to(ROOT)}/")


def main():
    if not SIM.exists():
        print("build the simulator first: make", file=sys.stderr)
        return 1
    tlb = exp_tlb()
    thrash = exp_thrashing()
    exp_cores()
    plot(tlb, thrash)
    return 0


if __name__ == "__main__":
    sys.exit(main())
