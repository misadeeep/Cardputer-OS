Flujo de trabajo:

Editas el código (main.cpp) directamente en la web de GitHub desde tu móvil.

Guardas (Commit).

Esperas 2 minutos.

Vas a la pestaña Actions en GitHub, entras en la última ejecución y descargas el zip firmware-bin.


Archivo C: El Robot de Compilación (.github/workflows/build.yml)
Pulsa Add file > Create new file.

En la caja del nombre escribe: .github/workflows/build.yml

Ojo a los puntos y las barras.

Pega este código (le dice a GitHub cómo compilar):

name: Build Cardputer OS

on: [push, workflow_dispatch]

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      
      - name: Cache pip
        uses: actions/cache@v3
        with:
          path: ~/.cache/pip
          key: ${{ runner.os }}-pip-${{ hashFiles('**/platformio.ini') }}
          restore-keys: |
            ${{ runner.os }}-pip-

      - name: Set up Python
        uses: actions/setup-python@v4
        with:
          python-version: '3.9'

      - name: Install PlatformIO
        run: |
          python -m pip install --upgrade pip
          pip install platformio

      - name: Run PlatformIO
        run: pio run

      - name: Upload Firmware
        uses: actions/upload-artifact@v4
        with:
          name: firmware-bin
          path: .pio/build/m5stack-cardputer/firmware.bin
