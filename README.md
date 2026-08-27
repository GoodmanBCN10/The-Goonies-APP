# The Goonies Ports 🏴‍☠️

The Goonies Ports es una aplicación Homebrew personalizada para Nintendo Switch diseñada para explorar, descargar e instalar ports de juegos directamente en tu consola. Construida con una interfaz de usuario nativa, fluida y elegante utilizando el motor gráfico Borealis.

## ✨ Características

- 🎮 **Descargas e Instalaciones Directas:** Descarga y extrae automáticamente los ports de juegos directamente en tu tarjeta SD (`/switch/`).
- 🗂️ **Categorías Dinámicas:** Los juegos se agrupan automáticamente en categorías (con contadores en tiempo real) obtenidas directamente desde el catálogo en la nube.
- 🎵 **Música de Fondo:** Disfruta de música ambiental inmersiva mientras exploras la tienda.
- 🌍 **Soporte Bilingüe:** Interfaz nativa completamente traducida al Castellano y al Inglés.
- ⚡ **Procesamiento Inteligente:** Extracción segura en segundo plano que evita que la consola entre en suspensión durante descargas pesadas.
- 🎨 **Interfaz Nativa de Switch:** Diseño limpio con modo oscuro perfectamente integrado en el ecosistema de Switch, utilizando la navegación y botones nativos.

## 🚀 Instalación

1. Descarga el último archivo `TheGooniesPorts.nro` desde la pestaña de [Releases](#).
2. Cópialo en la ruta `/switch/TheGooniesPorts/` de la tarjeta SD de tu Nintendo Switch.
3. Inicia la aplicación a través del menú Homebrew (HBL). *(Nota: Se recomienda iniciarlo usando el acceso completo a la memoria abriendo un juego manteniendo pulsado 'R' para instalaciones grandes).*

## 🛠️ Instrucciones de Compilación

Para compilar este proyecto desde el código fuente, necesitarás configurar el entorno de desarrollo habitual para Nintendo Switch.

### Requisitos:
- [devkitPro](https://devkitpro.org/) con `devkitA64` y `libnx`
- [Borealis](https://github.com/natinusala/borealis) (Ya incluido en la carpeta `vendor/borealis`)
- Librerías estándar (`sdl2`, `curl`, etc.)

### Compilación:
```bash
# Clonar el repositorio
git clone https://github.com/GoodmanBCN10/The-Goonies-Ports-Shop.git
cd The-Goonies-Ports-Shop

# Compilar el proyecto
make -j8
```
El archivo ejecutable `TheGooniesPorts.nro` aparecerá en la carpeta `build/`.

## 📦 Sistema de Catálogo

La aplicación carga su lista de juegos desde un archivo `catalog.json` remoto alojado en GitHub. Utiliza un gestor de descargas robusto capaz de manejar archivos zip grandes, zips divididos en partes, y descargas directas de archivos `.nro`.

## 📄 Créditos y Licencia

Este proyecto ha sido creado por GoodmanBCN10 para la comunidad homebrew. Por favor, respeta los derechos y licencias de los desarrolladores originales de los ports y del motor Borealis utilizado en este proyecto.
