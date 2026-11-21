# Halo Forge Evolved

**Map Creation Studio.** App para **Android** hecha solo para **crear mapas de Halo CE y exportarlos**: la Forja sacada a una app aparte, más simple y **táctil** (tocar para elegir, arrastrar con el dedo, girar con dos dedos).

**Creador: freddVelp ([@freedsny](https://github.com/freedsny)).**

> Proyecto no oficial de un aficionado. No tiene relación con Microsoft, Xbox Game Studios, Halo Studios ni Bungie. **No incluye el juego**: necesitas tu propia imagen de disco de Xbox (`.iso` o `.xiso`).

## La idea

- **Tocar un objeto** para elegirlo, sin apuntar con la mira.
- **Arrastrarlo con el dedo**: se desliza por el suelo y queda parado.
- **Barra junto al objeto**: mover, girar, copiar, borrar, ajustes.
- **Botón +**: paleta grande con armas, vehículos, escenario, spawns, banderas y teletransportes.
- **Vista desde arriba**: mover el mapa con un dedo, acercar pellizcando, acomodar objetos como fichas.
- **Deshacer, rehacer, guardar y probar** siempre a la mano.
- **Exportar** el mapa como un `.map` de Custom Edition y compartirlo (WhatsApp, Drive) o abrirlo en Halo C.E y Halo C.E Evolved.
- **Modo simple y avanzado**: lo básico a la vista; IA, Firefight y lo demás en avanzado.
- Arranca directo en la Forja: **Mis mapas, Nuevo mapa, Probar**. Nada de campaña ni en línea.

## Cómo se usa (táctil)

Al abrirla, el menú empieza en **FORJA**. Elige el mapa y entras volando como el Monitor.

| Gesto | Qué hace |
|---|---|
| Tocar un objeto | Lo eliges: abajo salen **MOVER, GIRAR, COPIAR, AJUSTES, BORRAR** |
| Arrastrar un objeto | Lo llevas por el suelo bajo el dedo; al levantar el dedo queda ahí |
| Arrastrar donde no hay nada | Miras alrededor |
| Con un objeto agarrado, tocar | Lo pones donde tocaste |
| Con un objeto agarrado, girar dos dedos | Lo giras |
| Palanca | Volar |
| **SUBIR / BAJAR** | Altura |
| **+ OBJETOS** | La paleta: armas, vehículos, escenario, spawns, banderas, teletransportes, enemigos |
| **DESHACER / REHACER** | Lo último que hiciste |
| **GUARDAR** | Guarda el mapa (la primera vez le pones nombre) |
| **EXPORTAR** | Guarda una copia del `.map` donde elijas (Descargas, Drive…) |
| **MAPA** | Los ajustes del mapa |
| **PROBAR** | Juegas el mapa como Spartan; vuelves a la Forja con la cruceta **DERECHA** |
| **?** | La ayuda en pantalla |
| **START** | El menú de pausa |

En los menús de la Forja (paleta, ajustes) tocas la fila que quieres, arrastras para bajar o subir, y **VOLVER** regresa. El control (por ejemplo el GameSir) sigue funcionando igual.

### Jugar el mapa en Halo C.E

1. En Halo Forge Evolved: **GUARDAR** y luego **EXPORTAR**, y eliges dónde dejar el `.map`.
2. En Halo C.E (o Halo C.E Evolved): en la lista de mapas, **IMPORTAR**, y eliges ese `.map`.

## En qué se basa

Este repositorio tiene el código completo de Content Evolved (rama `watanoge`, sin tocar) con los cambios de la Forja encima (rama `main`). Es la Forja (Faber) de [Halo Content Evolved](https://github.com/Watanoge/halo-content-evolved) de Watanoge, que guarda mapas de Custom Edition, hecha sobre [Content Extended Additions](https://github.com/SutlabSol/Content-Extended-Additions) de MotaLev y sobre [OpenCE](https://github.com/OpenCommunityEdition/OpenCE). Ideas tomadas también de la Forge de [Chupathingy](https://github.com/ChupathingyCE/chupathingyce).

Los mapas exportados se juegan en cualquier port con mapas de Custom Edition (OpenCE, Content Evolved, Chupathingy) y en Halo PC / Custom Edition.

## Plan

1. Táctil: tocar para elegir, arrastrar por el suelo, barra flotante, botones grandes.
2. Vista desde arriba.
3. App propia que arranca directo en la Forja.
4. Exportar y compartir.
5. Pulir: modo simple y avanzado, todo en español.

## Cómo conseguir el APK

En **Releases** de este repositorio. Se llama **Halo Forge Evolved** y se instala al lado de Halo C.E y de Halo C.E Evolved. La primera vez pide tu imagen de disco de Halo y saca los mapas.

Las versiones de prueba salen de la rama `pruebas` como borrador privado y son otra app, **Forge PRUEBA**.

## Licencia

AGPL-3.0, la de Content Evolved (ver `LICENSE`). El README original de Watanoge está en `README-ContentEvolved.md`.
