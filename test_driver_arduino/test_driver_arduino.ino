#include <Adafruit_NeoPixel.h>

#define PIN         6
#define NUM_LEDS    55    // 49 visibles + 6 perdidos en giros
#define COLUMNAS    7
#define FILAS       7
#define FRAME_SIZE  7

Adafruit_NeoPixel strip(NUM_LEDS, PIN, NEO_RGB + NEO_KHZ800);

int led_index(int columna, int fila) {
    int start = columna * 8;  // 7 LEDs + 1 perdido por giro
    if (columna % 2 == 0)
        return start + fila;
    else
        return start + (6 - fila);
}

uint32_t color_por_fila(int fila) {
    if (fila == 0)      return strip.Color(0, 180, 0);       // Verde
    else if (fila == 1) return strip.Color(50, 180, 0);      // Verde amarillento
    else if (fila == 2) return strip.Color(120, 180, 0);     // Verde limón
    else if (fila == 3) return strip.Color(180, 180, 0);     // Amarillo
    else if (fila == 4) return strip.Color(180, 120, 0);     // Amarillo naranja
    else if (fila == 5) return strip.Color(180, 50, 0);      // Naranja
    else                return strip.Color(180, 0, 0);       // Rojo
}

void mostrar(uint8_t columnas[]) {
    strip.clear();
    for (int col = 0; col < COLUMNAS; col++) {
        for (int fila = 0; fila < columnas[col]; fila++) {
            int idx = led_index(col, fila);
            strip.setPixelColor(idx, color_por_fila(fila));
        }
    }
    strip.show();
}

void setup() {
    Serial.begin(9600);
    strip.begin();
    strip.setBrightness(50);

    uint8_t default_frame[7] = {3, 0, 0, 0, 0, 0, 0};
    mostrar(default_frame);
}

void loop() {
    if (Serial.available() >= FRAME_SIZE) {
        uint8_t columnas[FRAME_SIZE];
        Serial.readBytes(columnas, FRAME_SIZE);
        mostrar(columnas);
        Serial.write((uint8_t)0x01);
        Serial.print("LEDs OK col=[");
        for (int i = 0; i < FRAME_SIZE; i++) {
            Serial.print(columnas[i]);
            if (i < FRAME_SIZE - 1) Serial.print(",");
        }
        Serial.println("]");
    }
}