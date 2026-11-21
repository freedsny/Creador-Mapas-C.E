"""Halo Forja: pasa al espanol los menus y los textos del juego (de Halo C.E
Evolved, sin lo de los trucos). Se ejecuta en la compilacion, desde la raiz del
repositorio: python3 espanol/traducir.py. Cambia los archivos de la copia que se
compila; en el repositorio el codigo queda en ingles, como el de Watanoge, para
poder mezclar sus versiones nuevas.

Las traducciones estan en esta misma carpeta (textos_es.py, textos_evolved.py,
textos_juego.txt, textos_codigo.txt, menu_es.py). Los textos se cambian por coincidencia exacta
(nunca por su orden: Content Evolved agrega opciones en las listas del juego),
con las tablas de textos_es.py y las de textos_evolved.py. Lo que no esta en ninguna tabla queda en ingles y
se cuenta al final."""
import bisect
import glob
import json
import pathlib
import re
import shutil
import sys

sys.path.insert(0, "tools")
import port_settings as ps

aqui = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(aqui))
import textos_es
import textos_evolved

pe = ps.PE
menus = pathlib.Path("port/assets/menus")
ce = menus / "ce"

# 1) las pantallas de ajustes (incluidas TRUCOS y GAMEPLAY con su fila TRUCOS)
nuevos = []
for nombre, lineas in ps.settings_files().items():
    ruta = ce / nombre
    if not ruta.exists():
        nuevos.append(nombre)
    ruta.write_text("\n".join(lineas), encoding="utf-8", newline="")

# 3) menus.json: las pantallas nuevas y los titulos
mj = menus / "menus.json"
crudo = mj.read_text(encoding="utf-8")
datos = json.loads(crudo)
archivos = datos["files"]
ancla = "ce/main_menu.settings_select.player_setup.player_profile_edit.network_setup.in_game.xml"
for nombre in nuevos:
    entrada = "ce/" + nombre
    if entrada not in archivos:
        if ancla in archivos:
            archivos.insert(archivos.index(ancla), entrada)
        else:
            archivos.append(entrada)
mj.write_text(json.dumps(datos, indent=1, ensure_ascii=False) + ("\n" if crudo.endswith("\n") else ""),
              encoding="utf-8")

# 4) los menus siempre con los textos de la app (en espanol), nunca los del
# loc.map de Custom Edition
config_c = pathlib.Path("port/linux/src/port_config.c")
fuente_c = config_c.read_text(encoding="utf-8")
viejo = '{ "display.menu_language", _config_string, "\\"maps\\""'
if viejo in fuente_c:
    fuente_c = fuente_c.replace(viejo, '{ "display.menu_language", _config_string, "\\"english\\""')
else:
    print("AVISO: no se encontro display.menu_language en port_config.c")
config_c.write_text(fuente_c, encoding="utf-8", newline="")
tags_c = pathlib.Path("port/linux/game/menu_tags.c")
fuente_c = tags_c.read_text(encoding="utf-8")
viejo = '\tif (!csstrcasecmp(value, "maps"))\n\t\treturn TRUE;\n'
if viejo in fuente_c:
    tags_c.write_text(fuente_c.replace(viejo,
        '\t/* port (Halo Forja): los menus en espanol de la app, nunca los textos del loc.map */\n'
        '\tif (!csstrcasecmp(value, "maps"))\n\t\treturn FALSE;\n'), encoding="utf-8", newline="")
else:
    print("AVISO: no se encontro menus_language_maps en menu_tags.c")

# 5) los textos al espanol, por coincidencia exacta
# (los bloques de la version de OpenCE van por orden: se usan solo los que aqui
# tienen los mismos textos que alla, contados; los demas estan en textos_evolved)
pares = {}
original = (ce / "strings.xml").read_text(encoding="utf-8")
for nombre, (_, traducidos) in textos_es.BLOQUES.items():
    if f'<strings name="{nombre}"' not in original:
        continue
    i = original.index(f'<strings name="{nombre}"')
    ingles = re.findall(r'<string text="([^"]*)"', original[i:original.index("</strings>", i)])
    if len(ingles) != len(traducidos):
        continue
    for en, es in zip(ingles, traducidos):
        if es is not None:
            pares[en] = es.replace("\n", "\\n")
pares.update(textos_es.AJUSTES)
pares.update(textos_evolved.TEXTOS)
valores = dict(textos_es.VALORES)
valores.update(textos_evolved.VALORES)
# (control: un texto traducido de los menus no rompe su XML ni pide letras
# que la fuente del juego no tiene; si no, no se compila)
_malos = [f"{en!r} -> {es!r}" for en, es in list(pares.items()) + list(valores.items())
          if '"' in es or "<" in es or re.search(r"&(?!amp;|lt;|gt;|quot;|apos;|#)", es)
          or any(ord(letra) > 126 for letra in es)]
if _malos:
    print("ERROR: textos de los menus que romperian el XML o la fuente del juego:")
    for _malo in _malos:
        print("  " + _malo)
    sys.exit(1)


def _texto(m):
    return m.group(1) + pares.get(m.group(2), m.group(2)) + '"'


def _valores(m):
    return 'strings="' + "|".join(valores.get(p, pares.get(p, p)) for p in m.group(1).split("|")) + '"'


pendientes = set()
for archivo in sorted(glob.glob(str(ce / "*.xml"))):
    if "network_setup.cheats" in archivo:
        continue
    ruta = pathlib.Path(archivo)
    contenido = ruta.read_text(encoding="utf-8")
    nuevo = re.sub(r'(<string text="|\stext=")([^"]*)"', _texto, contenido)
    nuevo = re.sub(r'strings="([^"]*)"', _valores, nuevo)
    if nuevo != contenido:
        ruta.write_text(nuevo, encoding="utf-8", newline="")
    for t in re.findall(r'(?:<string text="|\stext=")([^"]*)"', nuevo):
        if re.search(r"[a-z]{3}", t) and t not in pares.values():
            pendientes.add(t)

# 7) el menu de los tres dedos (overlay_menu.c): sus textos al espanol (sin
# los trucos de Halo C.E Evolved, que esta app no tiene)
overlay_c = pathlib.Path("port/linux/game/overlay_menu.c")
if overlay_c.exists():
    fuente_c = overlay_c.read_text(encoding="utf-8")
    traducidos = 0
    for ingles, espanol in textos_evolved.OVERLAY.items():
        literal = '"%s"' % ingles.replace("\\", "\\\\")
        if literal in fuente_c:
            traducidos += 1
            fuente_c = fuente_c.replace(literal, '"%s"' % espanol)
    overlay_c.write_text(fuente_c, encoding="utf-8", newline="")
    print("menu de los tres dedos: %d de %d textos en espanol" % (traducidos, len(textos_evolved.OVERLAY)))
else:
    print("AVISO: no esta overlay_menu.c (menu de los tres dedos)")

# 9) los textos del juego (strings.xml) por bloque y numero (textos_juego.txt),
# sin guardar el ingles
juego = {}
_bloque = None
for _linea in (aqui / "textos_juego.txt").read_text(encoding="utf-8").split("\n"):
    if _linea.startswith("## "):
        _nombre, _cuantos = _linea[3:].rsplit("|", 1)
        _bloque = (_nombre.strip(), int(_cuantos))
        juego[_bloque] = {}
    elif _linea[:1].isdigit() and _bloque:
        _numero, _texto = _linea.split("=", 1)
        juego[_bloque][int(_numero)] = _texto
strings_xml = ce / "strings.xml"
contenido = strings_xml.read_text(encoding="utf-8")
cambiados, distintos = 0, []
for (nombre, cuantos), textos in juego.items():
    patron = re.compile(r'(<strings name="' + re.escape(nombre) + r'"[^>]*>)(.*?)(</strings>)', re.S)
    m = patron.search(contenido)
    if not m:
        distintos.append(nombre + " (no esta)")
        continue
    partes = re.split(r'(<string text=")([^"]*)(")', m.group(2))
    # (partes: texto, '<string text="', el texto, '"', ...)
    if (len(partes) - 1) // 4 != cuantos:
        distintos.append(f"{nombre} ({(len(partes) - 1) // 4} textos, no {cuantos})")
        continue
    for numero, texto in textos.items():
        # (control: los mismos % que el texto del juego, que el codigo llena;
        # nada que rompa el XML ni letras que la fuente no tiene)
        import codigo_es
        problema = codigo_es.revisar_par("strings.xml", partes[2 + 4 * numero], texto.replace('"', "&quot;"))
        if problema or "<" in texto or '"' in texto:
            sys.exit(f"ERROR: textos_juego.txt, {nombre} {numero}: {problema or 'rompe el XML'}")
        partes[2 + 4 * numero] = texto
        cambiados += 1
    contenido = contenido[:m.start(2)] + "".join(partes) + contenido[m.end(2):]
strings_xml.write_text(contenido, encoding="utf-8", newline="")
print(f"textos del juego en espanol: {cambiados}; bloques distintos: {len(distintos)}")
for nombre in distintos:
    print("  AVISO: bloque distinto, se deja en ingles:", nombre)

# 8) los textos que el codigo escribe en pantalla (textos_codigo.txt)
import codigo_es
hechos, faltan = codigo_es.aplicar()
print(f"textos del codigo en espanol: {hechos}, no encontrados: {len(faltan)}")
for falta in faltan:
    print("  no encontrado:", falta)

# 6) las palabras del menu principal y los titulos, dibujados en espanol
# (si faltan pillow, numpy o scipy, quedan en ingles y solo se avisa)
try:
    import menu_es
    fuente = "port/assets/fonts/OpenCE-Regular.ttf"
    principal = ce / "shell/main_menu"
    for archivo, palabra in (("load_campaign", "CAMPAÑA"), ("multiplayer", "MULTIJUGADOR"),
                             ("profile", "PERFILES"), ("settings", "AJUSTES"), ("quit", "SALIR")):
        for estado in (0, 1):
            menu_es.item(palabra, estado, fuente).save(principal / f"menu_{archivo}__{estado}.png", optimize=True)
    menu_es.header("AJUSTES DEL PERFIL", fuente).save(
        ce / "shell/main_menu/settings_select/player_setup/player_profile_edit/header_edit_profile_settings__0.png",
        optimize=True)
    for carpeta, nombre, palabra in textos_es.ITEMS:
        if (principal / carpeta).is_dir():
            for estado in (0, 1):
                menu_es.item(palabra, estado, fuente).save(principal / carpeta / f"{nombre}__{estado}.png",
                                                           optimize=True)
    for ruta, titulo in textos_es.TITULOS + textos_es.TITULOS_AJUSTES:
        if (principal / ruta).parent.is_dir():
            menu_es.header(titulo, fuente).save(principal / ruta, optimize=True)
    # (las de Content Evolved: en ce/port/main_menu, cada una de su ancho)
    propias = ce / "port/main_menu"
    for nombre, palabra in textos_evolved.ITEMS_EVOLVED:
        for estado in (0, 1):
            archivo = propias / f"{nombre}__{estado}.png"
            if archivo.exists():
                from PIL import Image
                ancho = Image.open(archivo).size[0]
                menu_es.item(palabra, estado, fuente, ancho).save(archivo, optimize=True)
            else:
                print("AVISO: no esta", archivo)
    for ruta, titulo in textos_evolved.TITULOS_EVOLVED:
        if (propias / ruta).exists():
            menu_es.header(titulo, fuente).save(propias / ruta, optimize=True)
        else:
            print("AVISO: no esta", propias / ruta)
    print("menu en espanol: dibujado")
except ImportError as error:
    print("AVISO: el menu queda en ingles, falta:", error)
print("pantallas nuevas:", nuevos)
print("textos de menu que siguen en ingles:", len(pendientes))
