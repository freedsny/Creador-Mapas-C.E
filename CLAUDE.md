# Notas para Claude: Halo Forge Evolved (Map Creation Studio)

## Qué es

- App de Android de Freed para crear mapas de Halo CE con la Forja (Faber) de Halo Content Evolved, táctil, y exportarlos como mapas de Custom Edition.
- El código completo de Content Evolved (Watanoge, AGPL-3.0) está en este repositorio. No es un parche.
  - Rama `watanoge`: el original sin tocar (hoy build-144). Nunca se cambia a mano.
  - Rama `main`: la versión oficial, con los cambios de la Forja encima.
  - Rama `pruebas`: los cambios nuevos, primero.
- `README-ContentEvolved.md` es el README de Watanoge; `AGENTS.md` explica su código (cómo compilar, qué hay en cada carpeta).

## Cómo se compila

- `.github/workflows/compilar.yml`:
  - En `main` sale la oficial: Release "Latest", app "Halo Forge Evolved", paquete `com.halo.contentevolved.forja`.
  - En `pruebas` sale un borrador privado: app "Forge PRUEBA", paquete `com.halo.contentevolved.forja.pruebas`.
- El código queda en inglés en el repositorio. `espanol/traducir.py` lo pasa al español solo en la copia que se compila (traducciones tomadas de Halo C.E Evolved), para poder mezclar sin conflictos las versiones nuevas de Watanoge.
- El `build.yml` de Watanoge está deshabilitado en Actions (compila todas sus plataformas). No volver a habilitarlo.

## Traer una versión nueva de Content Evolved

1. Clonar la versión nueva (`git clone --depth 1 --branch build-N https://github.com/Watanoge/halo-content-evolved.git`).
2. En la rama `watanoge`, reemplazar todo por esa versión (con `git archive`, que no pierde archivos ignorados; luego `git add -A` y `git add -f` de lo ignorado), quitar los `.s` y `.d` sueltos de la raíz y hacer un commit "Content Evolved build-N".
3. Mezclar `watanoge` en `pruebas` (`git merge watanoge`) y resolver los conflictos llevando la intención de nuestros cambios al código nuevo. Si vuelve a aparecer `.github/workflows/build.yml`, borrarlo.
4. Compilar en `pruebas`, que Freed pruebe, y luego a `main`.

## Reglas

- Lo nuevo va a `pruebas`. **Nunca a `main`** hasta que Freed pruebe el APK de pruebas y diga "pásala".
- Los textos del juego en español van sin tildes ni eñes: las fuentes del juego no las tienen.
- Nada de datos de Bungie ni de Microsoft en el repositorio: ni mapas, ni la ISO, ni archivos del juego.
- Los secretos de firma (`ANDROID_KEYSTORE_*`) nunca se escriben, se muestran ni se guardan.
- Nunca quitar ni aflojar los controles de `espanol/codigo_es.py` o `espanol/traducir.py` para que algo pase.
- No copiar ni respaldar los mapas del juego hacia o desde un celular: la app los saca de la ISO sola.
- Mensajes de commit en español, sin tildes, explicando qué se hizo.
- Crédito a Watanoge (Content Evolved) y a MotaLev (Content Extended Additions); el repositorio sigue público (AGPL).
- GitHub GraphQL no funciona en estas sesiones: usar `gh api` (REST). Para el log: `gh api repos/freedsny/Creador-Mapas-C.E/actions/runs/<id>/jobs` y luego `.../actions/jobs/<job_id>/logs`.
