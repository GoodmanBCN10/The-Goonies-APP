# GooniesInstaller

Instalador de contenido para Nintendo Switch, desarrollado para la comunidad **Switch ES — The Goonies OS**.

Basado en [sphaira](https://github.com/ITotalJustice/sphaira) de [ITotalJustice](https://github.com/ITotalJustice).

---

## Qué es

GooniesInstaller es un fork de sphaira con una interfaz propia pensada para los usuarios de The Goonies OS. Simplifica la experiencia con un menú directo en español (con opción de inglés) que da acceso rápido a las funciones más usadas, eliminando opciones avanzadas:

- **Instalar por MTP** — Conecta el cable USB al PC y transfiere archivos directamente.
- **Juegos Instalados** — Gestiona y elimina títulos instalados en la consola.
- **Partidas Guardadas** — Administra las saves de tus juegos.
- **Explorar microSD** — Navegador de archivos de la tarjeta SD.
- **Ajustes** — Idioma, información del sistema y capacidad de la SD.

## Cambios respecto a sphaira

- Menú principal propio con diseño visual de The Goonies OS (fondo con degradados, logo con glow animado, botones tipo pastilla con selección animada).
- Pantalla de ajustes propia con toggle de idioma español/inglés, info de firmware, versión y capacidad SD.
- Pantalla MTP personalizada.
- Interfaz por defecto en español.
- Branding y créditos de la comunidad Switch ES.
- Versión simplificada sin funciones avanzadas. 

Todo el motor interno (instalación de NSP/XCI, MTP vía libhaze, montaje de filesystems, gestión de NCAs, cifrado) es código original de sphaira sin modificaciones funcionales.

## Compilación

### Requisitos

- [devkitPro](https://devkitpro.org/) con el toolchain de Switch (devkitA64)
- CMake 3.13+

### Pasos

```bash
git clone https://github.com/goodmanbcn/GooniesInstaller.git
cd GooniesInstaller
cmake --preset default
cmake --build build
```

El archivo resultante `GooniesInstaller.nro` se genera en `build/`.

### Instalación

Copia `GooniesInstaller.nro` a la carpeta `/switch/` de tu microSD.

## Créditos

- **[ITotalJustice](https://github.com/ITotalJustice)** — Autor de [sphaira](https://github.com/ITotalJustice/sphaira), la base sobre la que se construye este proyecto.
- **GoodmanBCN** — Adaptación, interfaz y mantenimiento para The Goonies OS.
- **Comunidad Switch ES — The Goonies OS** — Testing y feedback.

## Licencia

Este proyecto se distribuye bajo la licencia **GPL-3.0**, la misma que sphaira. Consulta el archivo [LICENSE](LICENSE) para los términos completos.
