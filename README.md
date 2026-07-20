# The Goonies Installer

Instalador de contenido para Nintendo Switch, desarrollado para la comunidad Switch ES — The Goonies OS.

---

## Qué es

The Goonies Installer es una aplicación homebrew para Nintendo Switch con interfaz propia, diseñada para simplificar la gestión de contenido en consolas con CFW. Todo desde una interfaz en español (con opción de inglés).

*   **¡Novedad V2.0! Descarga de Juegos:** Soporte integrado para descargar contenido a través de red (Torrents y enlaces Magnet) directamente desde la consola.
*   **Instalar por MTP:** Conecta el cable USB al PC y transfiere archivos directamente, sin necesidad de software adicional en el PC.
*   **Juegos Instalados:** Visualiza, gestiona y elimina títulos instalados en la consola.
*   **Partidas Guardadas:** Administra las saves de tus juegos.
*   **Explorar microSD:** Navegador de archivos de la tarjeta SD.
*   **Ajustes:** Idioma, información del sistema y capacidad de la SD.

## Componentes de terceros

Este proyecto utiliza los siguientes componentes de código abierto:

*   **yati:** Motor de instalación de NCAs (del proyecto sphaira de ITotalJustice).
*   **libhaze:** Librería MTP del proyecto Atmosphere.
*   **pipensx:** Herramientas y funciones base (de i3sey).
*   **minini:** Librería para archivos de configuración INI.

La interfaz gráfica, el módulo de instalación y la arquitectura general son código original de este proyecto.

## Compilación

### Requisitos

*   devkitPro con el toolchain de Switch (devkitA64)
*   Librerías (instalables con pacman de devkitPro)

### Pasos

```bash
git clone https://github.com/GoodmanBCN10/The-Goonies-Installer.git
cd The-Goonies-Installer
make -j$(nproc)
```

El archivo resultante `TheGooniesInstaller.nro` se genera en la raíz del proyecto.

## Instalación

Copia `TheGooniesInstaller.nro` a la carpeta `/switch/` de tu microSD.

## Créditos

*   **ITotalJustice** — Autor de yati (motor de instalación) y sphaira.
*   **Atmosphere-NX** — Autores de libhaze (librería MTP).
*   **i3sey** — Autor de pipensx.
*   **GoodmanBCN** — Desarrollo, interfaz y mantenimiento.
*   **Comunidad Switch ES — The Goonies OS** — Testing y feedback.

## Licencia

Este proyecto se distribuye bajo la licencia GPLv2+ (debido a la inclusión de pipensx). Consulta el archivo `LICENSE_GPLv2+.md` para los términos completos.
