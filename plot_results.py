#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
plot_results.py — gera os graficos do estudo da Arvore-B a partir do CSV
emitido por benchmark.cpp (CSV "tidy", uma linha por fase).

Uso:
    python3 plot_results.py [--csv out/results.csv] [--outdir out/plots]

Dependencias: pandas, matplotlib.
    pip install pandas matplotlib

Cada experimento (coluna 'exp') vira um conjunto de graficos:
    order_sweep    -> I/O por operacao e altura vs ordem m
    size_sweep     -> I/O por operacao vs N (escala log)
    family_compare -> reativa x preemptiva: I/O por operacao (insert/delete)
    reuse_compare  -> ocupacao do arquivo COM x SEM reaproveitamento de nos
"""
import argparse
import os
import sys

import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

PHASES = ["insert", "search", "delete", "reinsert"]


def savefig(fig, outdir, name):
    os.makedirs(outdir, exist_ok=True)
    path = os.path.join(outdir, name)
    fig.tight_layout()
    fig.savefig(path, dpi=130)
    plt.close(fig)
    print("  ->", path)


def plot_order_sweep(df, outdir):
    sub = df[df.exp == "order_sweep"]
    if sub.empty:
        return
    # I/O por operacao vs ordem, uma curva por (fase, padrao)
    for pattern in sorted(sub.pattern.unique()):
        fig, ax = plt.subplots(figsize=(7, 4.5))
        for phase in PHASES:
            d = sub[(sub.phase == phase) & (sub.pattern == pattern)].sort_values("order")
            if d.empty:
                continue
            ax.plot(d.order, d.io_per_op, marker="o", label=phase)
        ax.set_xscale("log", base=2)
        ax.set_xlabel("ordem m (escala log2)")
        ax.set_ylabel("acessos a disco por operacao")
        ax.set_title(f"I/O por operacao vs ordem (insercao {pattern})")
        ax.grid(True, alpha=0.3)
        ax.legend()
        savefig(fig, outdir, f"order_sweep_io_{pattern}.png")

    # Altura vs ordem (fase insert)
    fig, ax = plt.subplots(figsize=(7, 4.5))
    for pattern in sorted(sub.pattern.unique()):
        d = sub[(sub.phase == "insert") & (sub.pattern == pattern)].sort_values("order")
        if d.empty:
            continue
        ax.plot(d.order, d.height, marker="s", label=pattern)
    ax.set_xscale("log", base=2)
    ax.set_xlabel("ordem m (escala log2)")
    ax.set_ylabel("altura da arvore")
    ax.set_title("Altura vs ordem")
    ax.grid(True, alpha=0.3)
    ax.legend()
    savefig(fig, outdir, "order_sweep_height.png")


def plot_size_sweep(df, outdir):
    sub = df[df.exp == "size_sweep"]
    if sub.empty:
        return
    for phase in ["insert", "search", "delete"]:
        fig, ax = plt.subplots(figsize=(7, 4.5))
        for order in sorted(sub.order.unique()):
            for pattern in sorted(sub.pattern.unique()):
                d = sub[(sub.phase == phase) & (sub.order == order)
                        & (sub.pattern == pattern)].sort_values("n")
                if d.empty:
                    continue
                ax.plot(d.n, d.io_per_op, marker="o",
                        label=f"m={order} {pattern}")
        ax.set_xscale("log")
        ax.set_xlabel("N (numero de chaves, escala log)")
        ax.set_ylabel("acessos a disco por operacao")
        ax.set_title(f"I/O por operacao vs N — fase {phase}")
        ax.grid(True, alpha=0.3)
        ax.legend(fontsize=8)
        savefig(fig, outdir, f"size_sweep_io_{phase}.png")


def plot_family_compare(df, outdir):
    sub = df[df.exp == "family_compare"]
    if sub.empty:
        return
    for phase in ["insert", "delete"]:
        fig, ax = plt.subplots(figsize=(7, 4.5))
        for fam in sorted(sub.family.unique()):
            for pattern in sorted(sub.pattern.unique()):
                d = sub[(sub.phase == phase) & (sub.family == fam)
                        & (sub.pattern == pattern)].sort_values("order")
                if d.empty:
                    continue
                ax.plot(d.order, d.io_per_op, marker="o",
                        label=f"{fam} {pattern}")
        ax.set_xscale("log", base=2)
        ax.set_xlabel("ordem m (escala log2)")
        ax.set_ylabel("acessos a disco por operacao")
        ax.set_title(f"Familias reativa x preemptiva — fase {phase}")
        ax.grid(True, alpha=0.3)
        ax.legend(fontsize=8)
        savefig(fig, outdir, f"family_compare_{phase}.png")


def plot_reuse_compare(df, outdir):
    sub = df[df.exp == "reuse_compare"]
    if sub.empty:
        return
    # Ocupacao do arquivo (slots fisicos) apos o churn (fase reinsert),
    # com reuso ligado x desligado, por ordem.
    d = sub[sub.phase == "reinsert"]
    if d.empty:
        return
    orders = sorted(d.order.unique())
    on = [d[(d.order == o) & (d.reuse == 1)].slots.mean() for o in orders]
    off = [d[(d.order == o) & (d.reuse == 0)].slots.mean() for o in orders]
    x = range(len(orders))
    w = 0.38
    fig, ax = plt.subplots(figsize=(7, 4.5))
    ax.bar([i - w / 2 for i in x], off, width=w, label="sem reuso")
    ax.bar([i + w / 2 for i in x], on, width=w, label="com reuso")
    ax.set_xticks(list(x))
    ax.set_xticklabels([str(o) for o in orders])
    ax.set_xlabel("ordem m")
    ax.set_ylabel("slots fisicos no arquivo (apos churn)")
    ax.set_title("Reaproveitamento de nos: ocupacao do arquivo")
    ax.grid(True, axis="y", alpha=0.3)
    ax.legend()
    savefig(fig, outdir, "reuse_compare_slots.png")

    # Economia percentual
    fig, ax = plt.subplots(figsize=(7, 4.5))
    pct = [100.0 * (o - n) / o if o else 0 for o, n in zip(off, on)]
    ax.bar([str(o) for o in orders], pct, color="seagreen")
    ax.set_xlabel("ordem m")
    ax.set_ylabel("economia de slots (%)")
    ax.set_title("Economia de ocupacao com reaproveitamento de nos")
    ax.grid(True, axis="y", alpha=0.3)
    savefig(fig, outdir, "reuse_compare_savings.png")


def plot_cpu_vs_io(df, outdir):
    # Decomposicao do tempo: CPU-usuario x CPU-sistema x espera de I/O,
    # na fase insert do order_sweep (rand).
    sub = df[(df.exp == "order_sweep") & (df.phase == "insert")
             & (df.pattern == "rand")].sort_values("order")
    if sub.empty:
        return
    fig, ax = plt.subplots(figsize=(7, 4.5))
    x = [str(o) for o in sub.order]
    ax.bar(x, sub.cpu_user_s, label="CPU usuario")
    ax.bar(x, sub.cpu_sys_s, bottom=sub.cpu_user_s, label="CPU sistema")
    bottom2 = sub.cpu_user_s.values + sub.cpu_sys_s.values
    ax.bar(x, sub.io_wait_s, bottom=bottom2, label="espera I/O")
    ax.set_xlabel("ordem m")
    ax.set_ylabel("tempo (s)")
    ax.set_title("Decomposicao do tempo de insercao (rand)")
    ax.grid(True, axis="y", alpha=0.3)
    ax.legend()
    savefig(fig, outdir, "cpu_vs_io_insert.png")


def plot_cache_sweep(df, outdir):
    sub = df[df.exp == "cache_sweep"]
    if sub.empty:
        return
    for phase in ["insert", "search"]:
        fig, ax = plt.subplots(figsize=(7, 4.5))
        for order in sorted(sub.order.unique()):
            for pattern in sorted(sub.pattern.unique()):
                d = sub[(sub.phase == phase) & (sub.order == order)
                        & (sub.pattern == pattern)].sort_values("cache")
                if d.empty:
                    continue
                ax.plot(d.cache, d.io_per_op, marker="o",
                        label=f"m={order} {pattern}")
        ax.set_xscale("log", base=2)
        ax.set_xlabel("tamanho do cache (nos, escala log2)")
        ax.set_ylabel("acessos a disco por operacao")
        ax.set_title(f"I/O por operacao vs cache — fase {phase}")
        ax.grid(True, alpha=0.3)
        ax.legend(fontsize=8)
        savefig(fig, outdir, f"cache_sweep_io_{phase}.png")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", default="out/results.csv")
    ap.add_argument("--outdir", default="out/plots")
    args = ap.parse_args()

    if not os.path.exists(args.csv):
        sys.exit(f"CSV nao encontrado: {args.csv}\nRode antes: ./benchmark --out {args.csv}")

    df = pd.read_csv(args.csv)
    print(f"lido {args.csv}: {len(df)} linhas, experimentos = {sorted(df.exp.unique())}")

    plot_order_sweep(df, args.outdir)
    plot_size_sweep(df, args.outdir)
    plot_family_compare(df, args.outdir)
    plot_reuse_compare(df, args.outdir)
    plot_cache_sweep(df, args.outdir)
    plot_cpu_vs_io(df, args.outdir)
    print("graficos em", args.outdir)


if __name__ == "__main__":
    main()
