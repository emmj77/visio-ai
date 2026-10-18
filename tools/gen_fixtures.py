#!/usr/bin/env python3
"""tools/gen_fixtures.py — fabrique les images témoins du banc OCR.

Format .raw : 4 octets largeur, 4 octets hauteur (little endian), puis 1 octet par pixel
en niveaux de gris. Aucune bibliothèque de décodage côté C++ : le banc lit le fichier tel quel.
"""
import os, struct
from PIL import Image, ImageDraw, ImageFont

TXT = "Enregistrer sous..."
POLICE = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
SORTIE = os.path.join(os.path.dirname(__file__), "..", "tests", "data")

os.makedirs(SORTIE, exist_ok=True)
for taille in (11, 13, 16):
    f = ImageFont.truetype(POLICE, taille)
    img = Image.new("L", (400, taille + 14), 255)
    ImageDraw.Draw(img).text((8, 6), TXT, font=f, fill=0)
    chemin = os.path.join(SORTIE, f"texte_{taille}.raw")
    with open(chemin, "wb") as fh:
        fh.write(struct.pack("<II", img.width, img.height))
        fh.write(img.tobytes())
    print(chemin, img.width, "x", img.height, os.path.getsize(chemin), "octets")
