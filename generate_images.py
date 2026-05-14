#!/usr/bin/env python3
"""
Arquivo gerado completamente com Claude Code:
Le um arquivo binario .dat persistido pelo BTreeManager (C++) e gera um HTML
single-file com SVG embutido + pan/zoom interativo via JavaScript.

Uso:
    python3 generate_images.py arvore.dat
    python3 generate_images.py arvore.dat --order 5 --max-depth 3
    python3 generate_images.py arvore.dat -o arvore.html --root-idx 4926

Controles do HTML gerado:
    - mouse wheel: zoom (centrado no cursor)
    - drag: pan
    - tecla R: reset
"""

import argparse
import os
import struct
import sys
from dataclasses import dataclass


# -------- Modelo --------

@dataclass
class Node:
    idx: int
    n: int
    keys: list
    filhoIdx: list

    def is_leaf(self) -> bool:
        return self.filhoIdx[0] == 0


# -------- Parser binario --------

def parse_meta(meta_path: str) -> int:
    with open(meta_path, "r", encoding="utf-8") as f:
        return int(f.read().strip())


def parse_dat(dat_path: str, order: int) -> dict:
    record_format = f"<{2 * order}i"
    record_size = struct.calcsize(record_format)

    # Sanity 1: file size deve ser multiplo do record size
    file_size = os.path.getsize(dat_path)
    if file_size % record_size != 0:
        candidatos = [o for o in range(2, 33) if file_size % (8 * o) == 0 and o != order]
        msg = (f"erro: tamanho do arquivo ({file_size}B) nao e multiplo de "
               f"sizeof(NodeRecord<int,{order}>)={record_size}B.")
        msg += f"\n  --order {order} provavelmente esta errado."
        if candidatos:
            msg += f"\n  ORDER possiveis (compativeis com o tamanho): {candidatos}"
            msg += f"\n  Tente: python3 generate_images.py {dat_path} --order {candidatos[0]}"
        sys.exit(msg)

    nodes = {}
    with open(dat_path, "rb") as f:
        idx = 1
        while True:
            data = f.read(record_size)
            if len(data) < record_size:
                break
            up = struct.unpack(record_format, data)
            n = up[0]
            # Sanity 2: n deve estar no intervalo valido
            if not (0 <= n <= order - 1):
                sys.exit(
                    f"erro: no #{idx} tem n={n}, fora do intervalo [0, {order - 1}].\n"
                    f"  Provavel ORDER errado (passou --order {order})."
                )
            keys = list(up[1:order])
            filho = list(up[order:2 * order])
            nodes[idx] = Node(idx, n, keys, filho)
            idx += 1
    return nodes


# -------- Filtro de profundidade --------

def filter_by_depth(root_idx: int, nodes: dict, max_depth: int) -> tuple:
    """Retorna (nodes_visiveis, info_truncados).

    info_truncados: dict idx -> num_descendentes_ocultos
    """
    visible = {}
    truncated = {}

    def walk(idx, depth):
        if idx == 0 or idx not in nodes:
            return 0
        node = nodes[idx]
        # Conta este no
        sub_count = 1

        if max_depth is not None and depth >= max_depth and not node.is_leaf():
            # Conta subtree mas nao adiciona em visible
            sub_count += sum(walk_count(c) for c in node.filhoIdx if c != 0)
            visible[idx] = node
            truncated[idx] = sub_count - 1
            return sub_count

        visible[idx] = node
        for c in node.filhoIdx:
            if c != 0:
                sub_count += walk(c, depth + 1)
        return sub_count

    def walk_count(idx):
        if idx == 0 or idx not in nodes:
            return 0
        node = nodes[idx]
        c = 1
        for ch in node.filhoIdx:
            if ch != 0:
                c += walk_count(ch)
        return c

    walk(root_idx, 0)
    return visible, truncated


# -------- Layout --------

# Tamanhos compactos pra caber arvores grandes
KEY_W = 22
KEY_H = 22
LEAF_SPACING = 28
LEVEL_HEIGHT = 70


def compute_layout(root_idx: int, nodes: dict) -> dict:
    """Layout recursivo: pais centralizados sobre filhos extremos."""
    positions = {}
    cursor = [0]

    def visit(idx: int, depth: int) -> float:
        node = nodes[idx]
        y = depth * LEVEL_HEIGHT

        if node.is_leaf():
            x = cursor[0] + LEAF_SPACING / 2
            cursor[0] += LEAF_SPACING
            positions[idx] = (x, y)
            return x

        children_x = []
        for c in node.filhoIdx:
            if c != 0 and c in nodes:
                children_x.append(visit(c, depth + 1))

        if children_x:
            xc = (children_x[0] + children_x[-1]) / 2
        else:
            xc = cursor[0] + LEAF_SPACING / 2
            cursor[0] += LEAF_SPACING
        positions[idx] = (xc, y)
        return xc

    visit(root_idx, 0)
    return positions


# -------- Render SVG (string) --------

def render_svg_inner(nodes: dict, positions: dict, root_idx: int,
                     truncated: dict) -> tuple:
    """Retorna (svg_inner_str, width, height, shift_x, shift_y).

    O conteudo retornado vai dentro de um <g> ja transformado.
    """
    xs = [p[0] for p in positions.values()]
    ys = [p[1] for p in positions.values()]
    margin = 40
    min_x = min(xs) - margin
    max_x = max(xs) + margin
    min_y = min(ys) - margin
    max_y = max(ys) + KEY_H + margin
    w = max_x - min_x
    h = max_y - min_y
    shift_x = -min_x
    shift_y = -min_y

    parts = []

    # Edges primeiro (vao por baixo)
    for idx, node in nodes.items():
        if idx not in positions:
            continue
        cx, cy = positions[idx]
        node_w = max(KEY_W * node.n, KEY_W)
        x_left = cx - node_w / 2
        for i in range(node.n + 1):
            child = node.filhoIdx[i]
            if child == 0 or child not in positions:
                continue
            xi = x_left + (i / node.n) * node_w if node.n > 0 else cx
            yi = cy + KEY_H / 2
            ccx, ccy = positions[child]
            parts.append(f'<line class="edge" x1="{xi:.1f}" y1="{yi:.1f}" '
                         f'x2="{ccx:.1f}" y2="{ccy - KEY_H/2:.1f}"/>')

    # Nodes
    for idx, node in nodes.items():
        if idx not in positions:
            continue
        cx, cy = positions[idx]
        n = max(node.n, 1)
        node_w = KEY_W * n
        x_left = cx - node_w / 2
        y_top = cy - KEY_H / 2

        is_truncated = idx in truncated
        cls = "node-root" if idx == root_idx else ("node-truncated" if is_truncated else "node")
        parts.append(f'<rect class="{cls}" x="{x_left:.1f}" y="{y_top:.1f}" '
                     f'width="{node_w:.1f}" height="{KEY_H:.1f}" rx="2"/>')

        for i in range(1, node.n):
            xv = x_left + i * (node_w / node.n)
            parts.append(f'<line class="edge" x1="{xv:.1f}" y1="{y_top:.1f}" '
                         f'x2="{xv:.1f}" y2="{y_top + KEY_H:.1f}"/>')

        for i in range(node.n):
            kx = x_left + (i + 0.5) * (node_w / node.n)
            parts.append(f'<text class="key" x="{kx:.1f}" y="{cy:.1f}">{node.keys[i]}</text>')

        # Indicador de truncado
        if is_truncated:
            parts.append(f'<text class="trunc-info" x="{cx:.1f}" y="{cy + KEY_H/2 + 10:.1f}">'
                         f'+{truncated[idx]}</text>')

        parts.append(f'<text class="label" x="{cx:.1f}" y="{y_top - 4:.1f}">#{idx}</text>')

    return "\n  ".join(parts), w, h, shift_x, shift_y


# -------- HTML wrapper com pan/zoom --------

HTML_TEMPLATE = r"""<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<title>B-Tree visualizacao - {filename}</title>
<style>
  * {{ box-sizing: border-box; margin: 0; padding: 0; }}
  html, body {{ width: 100vw; height: 100vh; overflow: hidden; background: #fafafa; }}
  body {{ font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; }}
  svg {{ width: 100vw; height: 100vh; cursor: grab; user-select: none; }}
  svg:active {{ cursor: grabbing; }}
  .node {{ fill: #f4f4f4; stroke: #333; stroke-width: 1.2; }}
  .node-root {{ fill: #ffe599; stroke: #b45f06; stroke-width: 1.6; }}
  .node-truncated {{ fill: #e0e0e0; stroke: #888; stroke-width: 1; stroke-dasharray: 3,2; }}
  .edge {{ stroke: #666; stroke-width: 1; fill: none; }}
  .key {{ font: 9px monospace; text-anchor: middle; dominant-baseline: middle; }}
  .label {{ font: 7px monospace; fill: #999; text-anchor: middle; }}
  .trunc-info {{ font: 8px monospace; fill: #b45f06; text-anchor: middle; }}
  #info {{
    position: fixed; top: 12px; left: 12px;
    background: rgba(255,255,255,0.95);
    padding: 10px 14px; border: 1px solid #ccc; border-radius: 4px;
    font: 12px monospace; line-height: 1.5;
    box-shadow: 0 2px 6px rgba(0,0,0,0.1);
    pointer-events: none;
  }}
  #help {{
    position: fixed; bottom: 12px; left: 12px;
    background: rgba(255,255,255,0.95);
    padding: 8px 12px; border: 1px solid #ccc; border-radius: 4px;
    font: 11px monospace; color: #555;
    pointer-events: none;
  }}
</style>
</head>
<body>
<div id="info">
  <strong>B-Tree</strong> ({filename})<br>
  ORDER={order} &middot; rootIdx=<span style="color:#b45f06">{root_idx}</span><br>
  nos no arquivo: {total_nodes}<br>
  alcancaveis: {reachable}<br>
  visiveis: {visible}{depth_note}
</div>
<div id="help">scroll: zoom &nbsp;|&nbsp; arrasta: pan &nbsp;|&nbsp; R: reset</div>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {svg_w:.1f} {svg_h:.1f}"
     preserveAspectRatio="xMidYMid meet">
  <g id="content" transform="translate({shift_x:.1f}, {shift_y:.1f})">
  {svg_inner}
  </g>
</svg>
<script>
(function() {{
  const svg = document.querySelector('svg');
  const g = document.getElementById('content');
  const baseShiftX = {shift_x:.1f};
  const baseShiftY = {shift_y:.1f};
  let zoom = 1;
  let panX = 0;
  let panY = 0;

  function apply() {{
    g.setAttribute('transform',
      `translate(${{baseShiftX + panX}}, ${{baseShiftY + panY}}) scale(${{zoom}})`);
  }}

  svg.addEventListener('wheel', function(e) {{
    e.preventDefault();
    const rect = svg.getBoundingClientRect();
    const mx = e.clientX - rect.left;
    const my = e.clientY - rect.top;
    const factor = e.deltaY > 0 ? 0.9 : 1.1;
    // Mantem o ponto sob o cursor fixo
    panX = mx - (mx - panX) * factor;
    panY = my - (my - panY) * factor;
    zoom *= factor;
    apply();
  }}, {{ passive: false }});

  let dragging = false, lx = 0, ly = 0;
  svg.addEventListener('mousedown', function(e) {{
    dragging = true; lx = e.clientX; ly = e.clientY;
  }});
  svg.addEventListener('mousemove', function(e) {{
    if (!dragging) return;
    panX += e.clientX - lx;
    panY += e.clientY - ly;
    lx = e.clientX; ly = e.clientY;
    apply();
  }});
  function endDrag() {{ dragging = false; }}
  svg.addEventListener('mouseup', endDrag);
  svg.addEventListener('mouseleave', endDrag);

  document.addEventListener('keydown', function(e) {{
    if (e.key === 'r' || e.key === 'R') {{
      zoom = 1; panX = 0; panY = 0; apply();
    }}
  }});

  apply();
}})();
</script>
</body>
</html>
"""


def render_html(nodes_visible: dict, nodes_total: dict, positions: dict,
                root_idx: int, order: int, max_depth, reachable_count: int,
                truncated: dict, dat_filename: str, out_path: str) -> None:
    svg_inner, svg_w, svg_h, shift_x, shift_y = render_svg_inner(
        nodes_visible, positions, root_idx, truncated
    )

    depth_note = ""
    if max_depth is not None:
        depth_note = f"<br>profundidade max: {max_depth} <span style='color:#b45f06'>(truncado)</span>"

    html = HTML_TEMPLATE.format(
        filename=os.path.basename(dat_filename),
        order=order,
        root_idx=root_idx,
        total_nodes=len(nodes_total),
        reachable=reachable_count,
        visible=len(nodes_visible),
        depth_note=depth_note,
        svg_w=svg_w, svg_h=svg_h,
        shift_x=shift_x, shift_y=shift_y,
        svg_inner=svg_inner,
    )

    with open(out_path, "w", encoding="utf-8") as f:
        f.write(html)


# -------- Auto-deteccao de ORDER --------

def detect_order(dat_path: str, root_idx: int, candidates=range(2, 33)) -> int:
    """Tenta varios ORDERs e retorna o que produz uma B-Tree consistente.

    Criterio: parsing valida E o root_idx do .meta eh alcancavel.
    Em caso de empate (multiplos validos), prefere o que tem maior cobertura
    (mais nos alcancaveis a partir do root).
    """
    file_size = os.path.getsize(dat_path)
    best = []  # lista de (cobertura, order)

    for order in candidates:
        record_size = 8 * order
        if file_size % record_size != 0:
            continue
        total_nodes = file_size // record_size
        if total_nodes == 0 or root_idx > total_nodes:
            continue

        # Tenta parsing com este ORDER (sem rodar parse_dat porque ele aborta)
        try:
            with open(dat_path, "rb") as f:
                ok = True
                nodes_local = {}
                for idx in range(1, total_nodes + 1):
                    data = f.read(record_size)
                    if len(data) < record_size:
                        ok = False
                        break
                    up = struct.unpack(f"<{2 * order}i", data)
                    n = up[0]
                    if not (0 <= n <= order - 1):
                        ok = False
                        break
                    filho = up[order:2 * order]
                    if any(c < 0 or c > total_nodes for c in filho):
                        ok = False
                        break
                    nodes_local[idx] = filho
                if not ok:
                    continue
        except Exception:
            continue

        # Conta cobertura a partir do root
        visited = set()
        stack = [root_idx]
        while stack:
            cur = stack.pop()
            if cur == 0 or cur in visited or cur not in nodes_local:
                continue
            visited.add(cur)
            for c in nodes_local[cur]:
                if c != 0:
                    stack.append(c)

        best.append((len(visited), order))

    if not best:
        return None
    # Maior cobertura vence; empata por menor numero de nos (= maior ORDER)
    best.sort(key=lambda x: (-x[0], -x[1]))
    return best[0][1]


# -------- CLI --------

def main() -> None:
    p = argparse.ArgumentParser(
        description="Gera HTML interativo (pan/zoom) com a B-Tree persistida em disco."
    )
    p.add_argument("dat", nargs="?", default="arvore.dat",
                   help="caminho do .dat (default: arvore.dat)")
    p.add_argument("--order", type=int, default=None,
                   help="ORDER da arvore (default: auto-detecta)")
    p.add_argument("--meta", help="caminho do .meta (default: <dat>.meta)")
    p.add_argument("-o", "--output",
                   help="arquivo HTML de saida (default: <dat sem .dat>.html)")
    p.add_argument("--max-depth", type=int, default=None,
                   help="limita a profundidade exibida (default: sem limite)")
    p.add_argument("--root-idx", type=int, default=None,
                   help="visualiza subtree a partir desse idx (default: root real)")
    args = p.parse_args()

    if not os.path.isfile(args.dat):
        sys.exit(f"erro: nao encontrado: {args.dat}")
    meta_path = args.meta or args.dat + ".meta"
    if not os.path.isfile(meta_path):
        sys.exit(f"erro: meta nao encontrado: {meta_path}")

    out_path = args.output
    if out_path is None:
        base = args.dat[:-4] if args.dat.endswith(".dat") else args.dat
        out_path = base + ".html"

    real_root = parse_meta(meta_path)

    # Auto-detecta ORDER se nao for passado
    order = args.order
    if order is None:
        order = detect_order(args.dat, real_root)
        if order is None:
            sys.exit("erro: nao consegui detectar ORDER automaticamente. "
                     "Passe --order N manualmente.")
        print(f"  ORDER detectado: {order}")

    nodes = parse_dat(args.dat, order)

    root_idx = args.root_idx if args.root_idx else real_root

    if root_idx == 0 or root_idx not in nodes:
        sys.exit(f"erro: root invalido ({root_idx})")

    # Conta alcancaveis a partir da raiz selecionada
    def count_reachable(start):
        seen = set()
        stack = [start]
        while stack:
            cur = stack.pop()
            if cur == 0 or cur in seen or cur not in nodes:
                continue
            seen.add(cur)
            for c in nodes[cur].filhoIdx:
                if c != 0:
                    stack.append(c)
        return seen

    reachable_set = count_reachable(root_idx)
    visible, truncated = filter_by_depth(root_idx, nodes, args.max_depth)
    positions = compute_layout(root_idx, visible)

    render_html(visible, nodes, positions, root_idx, order,
                args.max_depth, len(reachable_set), truncated,
                args.dat, out_path)

    print(f"Gerado: {out_path}")
    print(f"  rootIdx     = {root_idx}{' (real)' if root_idx == real_root else f' (override; real={real_root})'}")
    print(f"  no arquivo  = {len(nodes)}")
    print(f"  alcancaveis = {len(reachable_set)}")
    print(f"  visiveis    = {len(visible)}")
    if truncated:
        oculto_total = sum(truncated.values())
        print(f"  truncados   = {len(truncated)} nos com {oculto_total} descendentes ocultos")
    print(f"\nAbra no browser: open {out_path}")


if __name__ == "__main__":
    main()
