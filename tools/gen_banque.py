#!/usr/bin/env python3
"""tools/gen_banque.py — la banque de formes : un bitmap d'intensités par (typo, taille).

Pour une typo et une taille données, un caractère a toujours la même allure. On rend
donc chaque caractère à chaque taille utile, on garde ses pixels **en niveaux de gris**
(pas en noir/blanc : à 11 px, un glyphe n'est presque que du dégradé, et le sous-pixel
d'un mot change les bits), et à l'exécution on compare des empreintes de même taille.

Format binaire (src/perceive/banque.bin, embarqué dans le binaire par objcopy) :
    "VSB1" | uint32 nombre d'entrées
    puis, pour chaque entrée : char, uint8 largeur, uint8 hauteur, w*h octets d'intensité
"""
import os
import struct
from PIL import Image, ImageDraw, ImageFont

POLICES = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf",
]
TAILLES = range(8, 23)
CARACTERES = [chr(c) for c in range(33, 127)]
W_MAX, H_MAX = 24, 24


def empreinte(car, police, taille):
    """Rend le caractère, coupe à l'encre, renvoie (l, h, pixels d'intensité)."""
    f = ImageFont.truetype(police, taille)
    img = Image.new("L", (taille * 3, taille * 3), 255)
    ImageDraw.Draw(img).text((taille, taille), car, font=f, fill=0)
    px = img.load()
    w, h = img.size
    xs, ys = [], []
    for y in range(h):
        for x in range(w):
            if px[x, y] < 230:
                xs.append(x)
                ys.append(y)
    if not xs:
        return None
    x0, x1, y0, y1 = min(xs), max(xs) + 1, min(ys), max(ys) + 1
    bw, bh = x1 - x0, y1 - y0
    if bw > W_MAX or bh > H_MAX:
        return None
    pix = bytearray()
    for y in range(y0, y1):
        for x in range(x0, x1):
            pix.append(255 - px[x, y])     # intensité d'encre, comme le C++ la mesure
    return bw, bh, bytes(pix)


entrees = []
vus = set()
for police in POLICES:
    if not os.path.exists(police):
        print("absent:", police)
        continue
    nom = os.path.basename(police)
    avant = len(entrees)
    for taille in TAILLES:
        for c in CARACTERES:
            r = empreinte(c, police, taille)
            if not r:
                continue
            bw, bh, pix = r
            cle = (pix, bw, bh)
            if cle in vus:
                continue
            vus.add(cle)
            entrees.append((c, bw, bh, pix))
    print(" ", nom, "->", len(entrees) - avant, "formes")

ici = os.path.dirname(os.path.abspath(__file__))
sortie = os.path.join(ici, "..", "src", "perceive", "banque.bin")
with open(sortie, "wb") as fh:
    fh.write(b"VSB1")
    fh.write(struct.pack("<I", len(entrees)))
    for c, bw, bh, pix in entrees:
        fh.write(struct.pack("<cBB", c.encode(), bw, bh))
        fh.write(pix)
print(sortie, os.path.getsize(sortie) // 1024, "Ko,", len(entrees), "formes")
