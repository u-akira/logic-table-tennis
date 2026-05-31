#ifndef CONFIG_H
#define CONFIG_H

// ==========================================
// XIAO ESP32C3 Hardware Configuration
// ==========================================

// --- Display (OLED SSD1306) ---
// I2C standard pins for XIAO ESP32C3
#define OLED_SDA 6   // D4
#define OLED_SCL 7   // D5
#define OLED_RESET -1 // Not used

// --- Input (Buttons) ---
#define BUTTON_UP 20   // D7
#define BUTTON_DOWN 8   // D8
#define BUTTON_LEFT 9   // D9
#define BUTTON_RIGHT 10   // D10

// --- Output (Buzzer) ---
#define BUZZER_PIN 5 // D3

// --- Visual (NeoPixels) ---
#define NEOPIXEL_PIN 2 // D0
#define NUM_LEDS 3     // Number of LEDs in your strip/board

// --- Other Settings ---
#define SERIAL_BAUD 115200

#endif // CONFIG_H