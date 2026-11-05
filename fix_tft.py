# Pre-build patch: TFT_eSPI's C3 header sets SPI_PORT to the enum SPI2_HOST (=1),
# but the Arduino core's REG_SPI_BASE(i) only maps i==2 to the SPI2 base (else 0),
# causing a null-base register write in tft.init(). Force the port to 2.
Import("env")
import os

def patch(*_args, **_kwargs):
    path = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"),
                        "TFT_eSPI", "Processors", "TFT_eSPI_ESP32_C3.h")
    if not os.path.isfile(path):
        return
    with open(path) as f:
        src = f.read()
    old = "#define SPI_PORT SPI2_HOST"
    if old in src:
        with open(path, "w") as f:
            f.write(src.replace(old, "#define SPI_PORT 2  // patched by fix_tft.py"))
        print("fix_tft.py: patched SPI_PORT in", path)

patch()
