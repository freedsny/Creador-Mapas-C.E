"""Dibuja el icono de Halo Forge Evolved (un dibujo propio: un bloque de
construccion, un martillo y chispas sobre un anillo, nada de Halo) en las
carpetas mipmap de la app: el frente, el monocromo (Android 13) y el fondo.
Se corre desde la raiz del repositorio: python3 herramientas/icono_forja.py"""
import math
import pathlib

from PIL import Image, ImageDraw, ImageFilter

RES = pathlib.Path("port/android/app/src/main/res")
GRANDE = 1728  # 108 dp a 16x: se dibuja grande y se achica
TAMANOS = {"mdpi": 108, "hdpi": 162, "xhdpi": 216, "xxhdpi": 324, "xxxhdpi": 432}


def p(x, y):
    """un punto en unidades de 0 a 108 (los dp del icono) a pixeles"""
    return (x * GRANDE / 108.0, y * GRANDE / 108.0)


def frente(mono=False):
    capa = Image.new("RGBA", (GRANDE, GRANDE), (0, 0, 0, 0))
    d = ImageDraw.Draw(capa)
    blanco = (255, 255, 255, 255)

    # el anillo: su mitad de atras ahora, la de adelante despues del bloque
    def anillo(desde, hasta):
        brillo = Image.new("RGBA", (GRANDE, GRANDE), (0, 0, 0, 0))
        db = ImageDraw.Draw(brillo)
        db.arc([p(20, 50), p(88, 78)], desde, hasta, fill=blanco if mono else (90, 200, 255, 255),
               width=int(GRANDE * 0.022))
        if mono:
            return brillo
        return Image.alpha_composite(brillo.filter(ImageFilter.GaussianBlur(GRANDE * 0.012)), brillo)

    capa = Image.alpha_composite(capa, anillo(180, 360))
    d = ImageDraw.Draw(capa)

    # el bloque, en isometrica
    cx, cy, a = 50.0, 60.0, 15.0
    arriba = [p(cx, cy - a), p(cx + a * 0.87, cy - a * 0.5), p(cx, cy), p(cx - a * 0.87, cy - a * 0.5)]
    izq = [p(cx - a * 0.87, cy - a * 0.5), p(cx, cy), p(cx, cy + a), p(cx - a * 0.87, cy + a * 0.5)]
    der = [p(cx, cy), p(cx + a * 0.87, cy - a * 0.5), p(cx + a * 0.87, cy + a * 0.5), p(cx, cy + a)]
    if mono:
        for cara in (arriba, izq, der):
            d.polygon(cara, fill=blanco)
    else:
        d.polygon(arriba, fill=(150, 225, 255, 255))
        d.polygon(izq, fill=(40, 140, 220, 255))
        d.polygon(der, fill=(22, 88, 165, 255))
        borde = (235, 250, 255, 255)
        w = int(GRANDE * 0.008)
        for cara in (arriba, izq, der):
            d.line(cara + [cara[0]], fill=borde, width=w, joint="curve")

    capa = Image.alpha_composite(capa, anillo(0, 180))
    d = ImageDraw.Draw(capa)

    # el martillo, golpeando el bloque desde arriba a la derecha
    ang = math.radians(-135)

    def rot(x, y, ox, oy):
        return (ox + x * math.cos(ang) - y * math.sin(ang), oy + x * math.sin(ang) + y * math.cos(ang))

    ox, oy = 60.0, 38.0
    mango = [rot(x, y, ox, oy) for x, y in ((-2.2, -2), (2.2, -2), (2.2, 25), (-2.2, 25))]
    cabeza = [rot(x, y, ox, oy) for x, y in ((-11, -9), (11, -9), (11, 1), (-11, 1))]
    d.polygon([p(*q) for q in mango], fill=blanco if mono else (200, 160, 110, 255))
    d.polygon([p(*q) for q in cabeza], fill=blanco if mono else (60, 70, 85, 255))
    if not mono:
        cara = [rot(x, y, ox, oy) for x, y in ((-11, -2), (11, -2), (11, 1), (-11, 1))]
        d.polygon([p(*q) for q in cara], fill=(170, 225, 255, 255))
        d.line([p(*q) for q in cabeza] + [p(*cabeza[0])], fill=(235, 250, 255, 255), width=int(GRANDE * 0.007))

    # chispas
    for x, y, r in ((40, 36, 1.7), (34, 41, 1.2), (45, 30, 1.1), (70, 46, 1.3), (37, 30, 0.8)):
        d.ellipse([p(x - r, y - r), p(x + r, y + r)], fill=blanco if mono else (255, 230, 150, 255))
    return capa


def main():
    for nombre, mono in (("ic_launcher_foreground", False), ("ic_launcher_monochrome", True)):
        grande = frente(mono)
        for carpeta, lado in TAMANOS.items():
            grande.resize((lado, lado), Image.LANCZOS).save(RES / f"mipmap-{carpeta}" / f"{nombre}.png", optimize=True)
    (RES / "drawable" / "ic_launcher_background.xml").write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n'
        "<!-- el fondo del icono de Halo Forge Evolved (herramientas/icono_forja.py) -->\n"
        '<shape xmlns:android="http://schemas.android.com/apk/res/android">\n'
        '    <gradient android:angle="270" android:startColor="#0B1A30" android:endColor="#173F6B" />\n'
        "</shape>\n", encoding="utf-8")
    # una vista previa como la muestra el celular (circulo)
    fondo = Image.new("RGBA", (GRANDE, GRANDE))
    for y in range(GRANDE):
        t = y / GRANDE
        c = tuple(int(a + (b - a) * t) for a, b in zip((0x0B, 0x1A, 0x30), (0x17, 0x3F, 0x6B))) + (255,)
        ImageDraw.Draw(fondo).line([(0, y), (GRANDE, y)], fill=c)
    vista = Image.alpha_composite(fondo, frente(False))
    mascara = Image.new("L", (GRANDE, GRANDE), 0)
    ImageDraw.Draw(mascara).ellipse([p(9, 9), p(99, 99)], fill=255)
    salida = Image.new("RGBA", (GRANDE, GRANDE), (0, 0, 0, 0))
    salida.paste(vista, (0, 0), mascara)
    salida.crop((int(GRANDE * 9 / 108), int(GRANDE * 9 / 108), int(GRANDE * 99 / 108), int(GRANDE * 99 / 108))).resize(
        (512, 512), Image.LANCZOS).save("/tmp/icono_vista.png")


if __name__ == "__main__":
    main()
