#!/usr/bin/env python
"""Generate simplified, brand-accurate film-stock boxart SVGs (one per stock).

Layout: brand band + wordmark on top, big 1-2 line stock name below, brand-colour
field. Kept legible down to ~44px swatch size. Output -> frontend/public/boxart/.

Brand palettes (researched): Kodak gold #FAB617 / red #E30613; Fujifilm green
#00A652; Ilford orange #F4862A; CineStill black band.
"""
import html
import os

# Brand band presets: (band bg, band text)
KODAK = ("#FAB617", "#E30613")
FUJI = ("#00A652", "#ffffff")
ILFORD = ("#F4862A", "#161616")
CINE = ("#141414", "#ffffff")

# id -> brand, band, l1, l2, bg, fg, [s1 override]
STOCKS = {
    # Kodak colour negative
    "portra_160":  ("Kodak", KODAK, "Portra", "160", "#6a3a92", "#ffffff"),
    "portra_400":  ("Kodak", KODAK, "Portra", "400", "#5b2a86", "#ffffff"),
    "portra_800":  ("Kodak", KODAK, "Portra", "800", "#45206a", "#ffffff"),
    "ektar_100":   ("Kodak", KODAK, "Ektar", "100", "#b3121f", "#ffffff"),
    "gold_200":    ("Kodak", KODAK, "Gold", "200", "#c2922e", "#2a1e02"),
    "colorplus_200": ("Kodak", KODAK, "ColorPlus", "200", "#17509e", "#ffffff", 46),
    "ultramax_400": ("Kodak", KODAK, "UltraMax", "400", "#2f8f4e", "#ffffff", 48),
    "vision3_250d": ("Kodak", KODAK, "Vision3", "250D", "#2b303a", "#ffffff"),
    "vision3_500t": ("Kodak", KODAK, "Vision3", "500T", "#333a45", "#ffffff"),
    # Kodak B&W
    "tri_x_400":   ("Kodak", KODAK, "Tri-X", "400", "#1a1a1d", "#ffffff"),
    "tmax_100":    ("Kodak", KODAK, "T-Max", "100", "#15171c", "#ffffff"),
    "tmax_400":    ("Kodak", KODAK, "T-Max", "400", "#15171c", "#ffffff"),
    "eastman_double_x": ("Eastman", KODAK, "Double-X", "5222", "#2a2a2e", "#ffffff", 46),
    # Kodak reversal
    "ektachrome_100": ("Kodak", ("#161616", "#FAB617"), "Ektachrome", "E100", "#f4c518", "#161616", 40),
    "kodachrome_64": ("Kodak", KODAK, "Kodachrome", "64", "#a5111a", "#ffffff", 40),
    # Fuji colour negative
    "superia_400": ("Fujifilm", FUJI, "Superia", "400", "#17643a", "#ffffff"),
    "fujicolor_c200": ("Fujifilm", FUJI, "C200", "", "#2a8f52", "#ffffff"),
    "pro_400h":    ("Fujifilm", FUJI, "Pro", "400H", "#3a9c74", "#ffffff"),
    "fuji_eterna_250d": ("Fujifilm", FUJI, "Eterna", "250D", "#24343a", "#ffffff"),
    # Fuji reversal
    "velvia_50":   ("Fujifilm", FUJI, "Velvia", "50", "#0c4d34", "#f2e9c8"),
    "velvia_100":  ("Fujifilm", FUJI, "Velvia", "100", "#0e5a3e", "#f2e9c8"),
    "provia_100f": ("Fujifilm", FUJI, "Provia", "100F", "#2e2d7a", "#f2e9c8"),
    "astia_100":   ("Fujifilm", FUJI, "Astia", "100F", "#46588f", "#f2e9c8"),
    # Fuji B&W
    "neopan_acros_100": ("Fujifilm", FUJI, "Acros", "100", "#17171a", "#ffffff"),
    # Ilford (B&W)
    "hp5_plus":    ("Ilford", ILFORD, "HP5", "400", "#1c1c1e", "#ffffff"),
    "fp4_plus_125": ("Ilford", ILFORD, "FP4", "125", "#1c1c1e", "#ffffff"),
    "pan_f_plus_50": ("Ilford", ILFORD, "Pan F", "50", "#1c1c1e", "#ffffff"),
    "delta_100":   ("Ilford", ILFORD, "Delta", "100", "#202027", "#ffffff"),
    "delta_400":   ("Ilford", ILFORD, "Delta", "400", "#202027", "#ffffff"),
    "delta_3200":  ("Ilford", ILFORD, "Delta", "3200", "#202027", "#ffffff"),
    # CineStill
    "cinestill_50d":  ("CineStill", CINE, "50D", "", "#0092c8", "#ffffff"),
    "cinestill_400d": ("CineStill", CINE, "400D", "", "#e2523b", "#ffffff"),
    "cinestill_800t": ("CineStill", CINE, "800T", "", "#d81f26", "#ffffff"),
}

FONT = "Geist,Helvetica,Arial,sans-serif"


def tile(brand, band, l1, l2, bg, fg, s1=None):
    band_bg, band_fg = band
    if l2:
        s = s1 or 58
        names = (
            f'<text x="26" y="204" font-family="{FONT}" font-size="{s}" font-weight="600" letter-spacing="-1.5" fill="{fg}">{html.escape(l1)}</text>'
            f'<text x="26" y="272" font-family="{FONT}" font-size="{s}" font-weight="600" letter-spacing="-1.5" fill="{fg}">{html.escape(l2)}</text>'
        )
    else:
        s = s1 or 84
        names = f'<text x="26" y="248" font-family="{FONT}" font-size="{s}" font-weight="600" letter-spacing="-2" fill="{fg}">{html.escape(l1)}</text>'
    return (
        f'<svg viewBox="0 0 320 320" xmlns="http://www.w3.org/2000/svg">'
        f'<rect width="320" height="320" fill="{bg}"/>'
        f'<rect width="320" height="104" fill="{band_bg}"/>'
        f'<text x="26" y="70" font-family="{FONT}" font-size="44" font-weight="700" letter-spacing="-0.5" fill="{band_fg}">{html.escape(brand)}</text>'
        f'{names}</svg>'
    )


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "..", "public", "boxart")
    os.makedirs(out, exist_ok=True)
    for sid, spec in STOCKS.items():
        svg = tile(*spec)
        with open(os.path.join(out, f"{sid}.svg"), "w", encoding="utf-8") as f:
            f.write(svg)
    print(f"wrote {len(STOCKS)} boxart tiles to {os.path.normpath(out)}")


if __name__ == "__main__":
    main()
