void setup() {
    Serial.begin(9600);
    pinMode(2, OUTPUT);
    digitalWrite(2, LOW);
}

void loop() {
    if (Serial.available() >= 5) {
        uint8_t columnas[5];
        Serial.readBytes(columnas, 5);

        if (columnas[0] > 0)
            digitalWrite(2, HIGH);
        else
            digitalWrite(2, LOW);
    }
}
