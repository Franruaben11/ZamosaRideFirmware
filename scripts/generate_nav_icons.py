#!/usr/bin/env python3
"""
Generador de íconos de navegación para ZamosaRideFirmware (LVGL 9 - LV_COLOR_FORMAT_RGB565A8).

Genera `include/icons/nav_icons.h` con color principal `#edebeb` (RGB: 237, 235, 235):
  - icon_chevron_pilot (64x64 px, relleno #edebeb con borde negro #000000 de 5 px)
  - icon_turn_left     (64x64 px, #edebeb)
  - icon_turn_right    (64x64 px, #edebeb)
  - icon_straight      (64x64 px, #edebeb)
  - icon_uturn         (64x64 px, #edebeb)
  - icon_finish        (64x64 px, #edebeb)

Si existen archivos PNG personalizados en `assets/icons/<nombre>.png` (y Pillow está instalado),
el script los convierte automáticamente a RGB565A8 (Little Endian) redimensionándolos al tamaño esperado.
"""

import math
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parent.parent
ASSETS_DIR = ROOT_DIR / "assets" / "icons"
OUTPUT_HEADER = ROOT_DIR / "include" / "icons" / "nav_icons.h"

# Color principal unificado: #edebeb (RGB: 237, 235, 235)
PRIMARY_RGB = (237, 235, 235)


def rgb888_to_rgb565_le(r: int, g: int, b: int) -> tuple[int, int]:
    """Convierte RGB888 a un par de bytes RGB565 en formato Little Endian (low_byte, high_byte)."""
    r5 = (max(0, min(255, r)) >> 3) & 0x1F
    g6 = (max(0, min(255, g)) >> 2) & 0x3F
    b5 = (max(0, min(255, b)) >> 3) & 0x1F
    val = (r5 << 11) | (g6 << 5) | b5
    return (val & 0xFF, (val >> 8) & 0xFF)


def dist_point_to_segment(px: float, py: float, ax: float, ay: float, bx: float, by: float) -> float:
    dx = bx - ax
    dy = by - ay
    len_sq = dx * dx + dy * dy
    if len_sq <= 1e-9:
        return math.hypot(px - ax, py - ay)
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / len_sq))
    proj_x = ax + t * dx
    proj_y = ay + t * dy
    return math.hypot(px - proj_x, py - proj_y)


def point_in_polygon(px: float, py: float, poly: list[tuple[float, float]]) -> bool:
    inside = False
    n = len(poly)
    j = n - 1
    for i in range(n):
        xi, yi = poly[i]
        xj, yj = poly[j]
        if ((yi > py) != (yj > py)) and (px < (xj - xi) * (py - yi) / (yj - yi + 1e-12) + xi):
            inside = not inside
        j = i
    return inside


def signed_dist_polygon(px: float, py: float, poly: list[tuple[float, float]]) -> float:
    min_d = min(
        dist_point_to_segment(px, py, poly[i][0], poly[i][1], poly[(i + 1) % len(poly)][0], poly[(i + 1) % len(poly)][1])
        for i in range(len(poly))
    )
    return -min_d if point_in_polygon(px, py, poly) else min_d


def render_sdf_rgba_monochrome(width: int, height: int, sdf_fn, rgb: tuple[int, int, int] = PRIMARY_RGB) -> list[tuple[int, int, int, int]]:
    """Evalúa una SDF monocromática (#edebeb) con antialiasing subpíxel en el canal alfa."""
    pixels = []
    r, g, b = rgb
    for y in range(height):
        for x in range(width):
            d = sdf_fn(x + 0.5, y + 0.5)
            cov = max(0.0, min(1.0, 0.5 - d / 1.2))
            a = int(round(cov * 255))
            pixels.append((r, g, b, a))
    return pixels


def sdf_chevron_pilot_inner(x: float, y: float) -> float:
    # Puntero triangular/chevron (64x64) apuntando hacia arriba; polígono interior antes del borde de 5 px
    poly_inset = [
        (32.0, 13.0),  # Punta superior
        (50.0, 49.0),  # Ala derecha
        (32.0, 39.5),  # Escotadura central
        (14.0, 49.0),  # Ala izquierda
    ]
    return signed_dist_polygon(x, y, poly_inset) - 2.5


def render_chevron_pilot_rgba(width: int = 64, height: int = 64) -> list[tuple[int, int, int, int]]:
    """
    Renderiza icon_chevron_pilot (64x64):
      - Interior (d <= 0): relleno #edebeb (alfa 255)
      - Contorno (0 < d <= 5.0 px): borde negro #000000 (alfa 255)
      - Exterior (d > 5.0 px): transparente (alfa 0)
    """
    border_width = 5.0
    r_fill, g_fill, b_fill = PRIMARY_RGB
    pixels = []
    for y in range(height):
        for x in range(width):
            d_inner = sdf_chevron_pilot_inner(x + 0.5, y + 0.5)
            d_outer = d_inner - border_width

            # Cobertura total (relleno + borde negro) frente al exterior transparente
            outer_cov = max(0.0, min(1.0, 0.5 - d_outer / 1.2))
            alpha = int(round(outer_cov * 255))

            # Transición suave entre el relleno #edebeb (d_inner < 0) y el borde #000000 (d_inner > 0)
            fill_mix = max(0.0, min(1.0, 0.5 - d_inner / 1.2))
            r = int(round(r_fill * fill_mix))
            g = int(round(g_fill * fill_mix))
            b = int(round(b_fill * fill_mix))

            pixels.append((r, g, b, alpha))
    return pixels


def sdf_straight(x: float, y: float) -> float:
    stroke_r = 4.2
    d_stem = dist_point_to_segment(x, y, 32.0, 54.0, 32.0, 14.0) - stroke_r
    d_left = dist_point_to_segment(x, y, 32.0, 11.0, 17.0, 26.0) - stroke_r
    d_right = dist_point_to_segment(x, y, 32.0, 11.0, 47.0, 26.0) - stroke_r
    return min(d_stem, d_left, d_right)


def sdf_turn_left(x: float, y: float) -> float:
    stroke_r = 4.2
    d_stem = dist_point_to_segment(x, y, 42.0, 54.0, 42.0, 36.0) - stroke_r
    dx = x - 30.0
    dy = y - 36.0
    if dx >= 0.0 and dy <= 0.0:
        d_arc = abs(math.hypot(dx, dy) - 12.0) - stroke_r
    else:
        d_arc = min(
            math.hypot(x - 42.0, y - 36.0),
            math.hypot(x - 30.0, y - 24.0),
        ) - stroke_r
    d_horiz = dist_point_to_segment(x, y, 30.0, 24.0, 14.0, 24.0) - stroke_r
    d_head_top = dist_point_to_segment(x, y, 12.0, 24.0, 25.0, 11.0) - stroke_r
    d_head_bot = dist_point_to_segment(x, y, 12.0, 24.0, 25.0, 37.0) - stroke_r
    return min(d_stem, d_arc, d_horiz, d_head_top, d_head_bot)


def sdf_turn_right(x: float, y: float) -> float:
    return sdf_turn_left(64.0 - x, y)


def sdf_uturn(x: float, y: float) -> float:
    stroke_r = 4.0
    d_right = dist_point_to_segment(x, y, 45.0, 54.0, 45.0, 25.0) - stroke_r
    dx = x - 32.0
    dy = y - 25.0
    if dy <= 0.0:
        d_arc = abs(math.hypot(dx, dy) - 13.0) - stroke_r
    else:
        d_arc = min(
            math.hypot(x - 45.0, y - 25.0),
            math.hypot(x - 19.0, y - 25.0),
        ) - stroke_r
    d_left = dist_point_to_segment(x, y, 19.0, 25.0, 19.0, 46.0) - stroke_r
    d_head_l = dist_point_to_segment(x, y, 19.0, 48.0, 8.0, 37.0) - stroke_r
    d_head_r = dist_point_to_segment(x, y, 19.0, 48.0, 30.0, 37.0) - stroke_r
    return min(d_right, d_arc, d_left, d_head_l, d_head_r)


def sdf_finish(x: float, y: float) -> float:
    dist_center = math.hypot(x - 32.0, y - 32.0)
    d_outer_ring = abs(dist_center - 21.0) - 4.0
    d_inner_dot = dist_center - 7.5
    return min(d_outer_ring, d_inner_dot)


def load_png_or_fallback(name: str, width: int, height: int, fallback_rgba_fn) -> list[tuple[int, int, int, int]]:
    png_path = ASSETS_DIR / f"{name}.png"
    if png_path.is_file():
        try:
            from PIL import Image
            img = Image.open(png_path).convert("RGBA").resize((width, height), Image.Resampling.LANCZOS)
            return list(img.getdata())
        except Exception as exc:
            print(f"[WARN] No se pudo cargar {png_path} ({exc}), usando generador vectorial.")
    return fallback_rgba_fn(width, height)


def format_c_icon(var_name: str, width: int, height: int, rgba_pixels: list[tuple[int, int, int, int]]) -> str:
    rgb_bytes: list[int] = []
    alpha_bytes: list[int] = []

    for r, g, b, a in rgba_pixels:
        lo, hi = rgb888_to_rgb565_le(r, g, b)
        rgb_bytes.append(lo)
        rgb_bytes.append(hi)
        alpha_bytes.append(max(0, min(255, a)))

    all_bytes = rgb_bytes + alpha_bytes
    lines = []
    chunk_size = 24
    for i in range(0, len(all_bytes), chunk_size):
        chunk = ", ".join(f"0x{b:02X}" for b in all_bytes[i : i + chunk_size])
        lines.append(f"    {chunk},")
    data_str = "\n".join(lines)
    stride = width * 2
    total_size = len(all_bytes)

    return f"""// {var_name} ({width}x{height} px, LV_COLOR_FORMAT_RGB565A8)
static const LV_ATTRIBUTE_MEM_ALIGN uint8_t {var_name}_map[{total_size}] = {{
{data_str}
}};

static const lv_image_dsc_t {var_name} = {{
    .header = {{
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = {width},
        .h = {height},
        .stride = {stride},
        .reserved_2 = 0,
    }},
    .data_size = sizeof({var_name}_map),
    .data = {var_name}_map,
    .reserved = nullptr,
    .reserved_2 = nullptr,
}};
"""


def main():
    OUTPUT_HEADER.parent.mkdir(parents=True, exist_ok=True)
    icons = [
        ("icon_chevron_pilot", 64, 64, render_chevron_pilot_rgba),
        ("icon_turn_left", 64, 64, lambda w, h: render_sdf_rgba_monochrome(w, h, sdf_turn_left)),
        ("icon_turn_right", 64, 64, lambda w, h: render_sdf_rgba_monochrome(w, h, sdf_turn_right)),
        ("icon_straight", 64, 64, lambda w, h: render_sdf_rgba_monochrome(w, h, sdf_straight)),
        ("icon_uturn", 64, 64, lambda w, h: render_sdf_rgba_monochrome(w, h, sdf_uturn)),
        ("icon_finish", 64, 64, lambda w, h: render_sdf_rgba_monochrome(w, h, sdf_finish)),
    ]

    blocks = []
    for name, w, h, fn in icons:
        rgba = load_png_or_fallback(name, w, h, fn)
        blocks.append(format_c_icon(name, w, h, rgba))

    header_content = """#ifndef ZAMOSA_NAV_ICONS_H
#define ZAMOSA_NAV_ICONS_H

#include <lvgl.h>

/**
 * Íconos de navegación minimalistas estilo Beeline Moto II (LV_COLOR_FORMAT_RGB565A8, color #edebeb).
 *
 * Cómo reemplazar con PNGs personalizados:
 * 1. Coloca tus archivos PNG con canal alfa en `assets/icons/`:
 *    - `assets/icons/icon_chevron_pilot.png` (64x64 px)
 *    - `assets/icons/icon_turn_left.png`     (64x64 px)
 *    - `assets/icons/icon_turn_right.png`    (64x64 px)
 *    - `assets/icons/icon_straight.png`      (64x64 px)
 *    - `assets/icons/icon_uturn.png`         (64x64 px)
 *    - `assets/icons/icon_finish.png`        (64x64 px)
 * 2. Ejecuta `python3 scripts/generate_nav_icons.py` para regenerar este archivo `include/icons/nav_icons.h`.
 */

""" + "\n".join(blocks) + "\n#endif // ZAMOSA_NAV_ICONS_H\n"

    OUTPUT_HEADER.write_text(header_content, encoding="utf-8")
    print(f"[OK] Generado {OUTPUT_HEADER} ({len(header_content)} bytes)")


if __name__ == "__main__":
    main()
