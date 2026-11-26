[README.md](https://github.com/user-attachments/files/35483918/README.md)
# TM Metering

Plugin VST3 (Windows y macOS) de análisis de audio inspirado en el MultiMeter de Logic Pro:

- **Analyzer**: espectro por bandas (1/3, 1/6, 1/12 de octava), modos Peak / RMS Fast / RMS Slow, peak hold y velocidad de caída (dB/s).
- **Goniometer**: Lissajous M/S con persistencia y medidor de correlación de fase.
- **Medidor de nivel L/R**: Peak, RMS o Peak & RMS, con lectura numérica y indicador de clip (clic para resetear).

El audio pasa sin ser modificado (bypass transparente), así que es seguro en cualquier DAW.

## Compilar en GitHub (recomendado)

1. Crea un repositorio vacío en tu cuenta de GitHub (por ejemplo `TM-Metering`).
2. Desde esta carpeta:

```bash
git init
git add .
git commit -m "TM Metering v1.0.0"
git branch -M main
git remote add origin https://github.com/TU_USUARIO/TM-Metering.git
git push -u origin main
```

3. En la pestaña **Actions** del repositorio se compilará automáticamente en Windows y macOS.
   Los `.vst3` quedan como *Artifacts* de cada ejecución.
4. Para publicar una versión descargable: `git tag v1.0.0 && git push origin v1.0.0`
   (se crea una *Release* con los .zip de Windows y macOS).

## Compilar en local

Requisitos: CMake 3.22+, Visual Studio 2022 (Windows) o Xcode (macOS).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

El plugin queda en `build/TMMetering_artefacts/.../VST3/TM Metering.vst3`.

## Instalación

- **Windows**: copiar `TM Metering.vst3` a `C:\Program Files\Common Files\VST3\`
- **macOS**: copiar a `~/Library/Audio/Plug-Ins/VST3/` y, al no estar firmado, ejecutar
  `xattr -cr "~/Library/Audio/Plug-Ins/VST3/TM Metering.vst3"`

> Nota: en macOS el plugin es universal (Apple Silicon + Intel). Para distribuirlo públicamente conviene firmarlo y notarizarlo con una cuenta de Apple Developer.
