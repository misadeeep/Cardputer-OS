/**
 * M5 CARDPUTER OS - RELEASE 1.2 (STABLE & FAST)
 * Mejoras:
 * - Fix de congelamiento (eliminado redibujado constante).
 * - Watchdog simplificado (sin tareas paralelas).
 * - Editor optimizado (solo actualiza si hay cambios).
 */

#include <M5Cardputer.h>
#include <M5StackUpdater.h>
#include <LittleFS.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <map>
#include <vector>

// --- FIX TECLA ESCAPE ---
#define KEY_ESC 0x1B    
#ifndef KEY_ENTER
#define KEY_ENTER 13
#endif

// --- CONFIG ---
#define SD_CS_PIN       40
#define LED_PIN         21
#define RECOVERY_BIN    "/recovery.bin"
#define CRASH_LIMIT     3

// --- GLOBALES ---
Adafruit_NeoPixel statusLed(1, LED_PIN, NEO_GRB + NEO_KHZ800);
Preferences crashTracker;
typedef void (*CmdHandler)(String arg);
std::map<String, CmdHandler> cmdMap;
unsigned long bootTime = 0;
bool systemStabilized = false;

// --- PROTOTIPOS ---
void checkSystemHealth();
void markSystemStable();
void executeScript(String filename);
void runEditor(String filepath);
void drawStatusBar(String msg, uint16_t color);
void cmd_print(String arg);
void cmd_delay(String arg);
void cmd_color(String arg);
void cmd_wait(String arg);
void cmd_load(String arg);
void cmd_edit(String arg);

// ==========================================
// FILE SYSTEM HELPER
// ==========================================
fs::FS* getFS(String path) {
    if (path.startsWith("/sd/") || !path.startsWith("/")) return &SD; 
    return (fs::FS*)&LittleFS; 
}

// ==========================================
// EDITOR OPTIMIZADO (NO SE BLOQUEA)
// ==========================================
void runEditor(String filepath) {
    fs::FS* fs = getFS(filepath);
    String buffer = "";
    
    // Cargar archivo si existe
    if(fs->exists(filepath)) {
        File f = fs->open(filepath, "r");
        while(f.available()) buffer += (char)f.read();
        f.close();
    }

    bool dirty = true;     // Redibujar la primera vez
    bool blinkState = false;
    unsigned long lastBlink = 0;

    M5Cardputer.Display.fillScreen(BLACK); // Limpiar fondo UNA VEZ

    while(true) {
        // 1. CHEQUEO DE ESTABILIDAD (Dentro del editor también)
        markSystemStable(); 

        M5Cardputer.update(); // Leer hardware

        // 2. DETECTAR TECLADO
        if(M5Cardputer.Keyboard.isChange()) {
            if(M5Cardputer.Keyboard.isPressed()) {
                Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
                
                for(auto i : status.word) { buffer += i; dirty = true; }
                
                if(status.del && buffer.length() > 0) {
                    buffer.remove(buffer.length()-1); dirty = true;
                }
                if(status.enter) {
                    buffer += "\n"; dirty = true;
                }
            }
            
            // GUARDAR / SALIR
            if(M5Cardputer.Keyboard.isKeyPressed(KEY_ESC)) {
                M5Cardputer.Display.fillRect(20, 40, 200, 60, BLUE);
                M5Cardputer.Display.setTextColor(WHITE);
                M5Cardputer.Display.setCursor(30, 50);
                M5Cardputer.Display.println("ENTER: Guardar");
                M5Cardputer.Display.setCursor(30, 70);
                M5Cardputer.Display.println("ESC: Cancelar");
                
                while(true) {
                    M5Cardputer.update();
                    if(M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
                        File f = fs->open(filepath, "w");
                        f.print(buffer); f.close();
                        M5Cardputer.Display.fillScreen(BLACK);
                        return;
                    }
                    if(M5Cardputer.Keyboard.isKeyPressed(KEY_ESC)) {
                        dirty = true; // Volver a dibujar editor
                        M5Cardputer.Display.fillScreen(BLACK);
                        break; 
                    }
                }
            }
        }

        // 3. RENDERIZADO INTELIGENTE (Solo si cambia algo)
        if (dirty) {
            M5Cardputer.Display.fillRect(0, 18, 240, 117, BLACK); // Borrar solo zona texto
            drawStatusBar("EDIT: " + filepath, TFT_ORANGE);
            M5Cardputer.Display.setTextColor(WHITE);
            M5Cardputer.Display.setCursor(0, 20);
            M5Cardputer.Display.print(buffer);
            dirty = false;
        }

        // 4. CURSOR PARPADEANTE (Sin borrar todo)
        if (millis() - lastBlink > 500) {
            lastBlink = millis();
            blinkState = !blinkState;
            // Dibujamos el cursor al final del texto (aprox)
            // Nota: Para un cursor real necesitamos coordenadas, aquí usamos un indicador simple
            M5Cardputer.Display.fillRect(230, 0, 10, 10, blinkState ? WHITE : TFT_ORANGE);
        }

        delay(10); // Pausa corta para dejar respirar al I2C
    }
}

// ==========================================
// SISTEMA DE SEGURIDAD (CORE 0)
// ==========================================
void checkSystemHealth() {
    crashTracker.begin("sys_health", false);
    int fails = crashTracker.getInt("fails", 0);
    
    // Check boton fisico GO (GPIO0)
    pinMode(0, INPUT_PULLUP);
    bool manualOverride = (digitalRead(0) == LOW);
    
    crashTracker.putInt("fails", fails + 1); 
    
    if (manualOverride || fails >= CRASH_LIMIT) {
        statusLed.begin(); statusLed.setPixelColor(0, statusLed.Color(255,0,0)); statusLed.show();
        M5Cardputer.Display.begin(); M5Cardputer.Display.setRotation(1);
        M5Cardputer.Display.fillScreen(RED); M5Cardputer.Display.setTextColor(WHITE);
        M5Cardputer.Display.setTextSize(2); M5Cardputer.Display.setCursor(10, 20);
        
        if(manualOverride) M5Cardputer.Display.println("RECOVERY MANUAL");
        else M5Cardputer.Display.println("SYSTEM CRASHED");
        
        SD.begin(SD_CS_PIN, SPI, 25000000);
        if(SD.exists(RECOVERY_BIN)) {
            crashTracker.putInt("fails", 0); crashTracker.end();
            updateFromFS(SD, RECOVERY_BIN);
        } else {
             M5Cardputer.Display.println("\nNO recovery.bin");
             while(1); 
        }
    }
    crashTracker.end();
}

void markSystemStable() {
    if (!systemStabilized && millis() - bootTime > 10000) {
        Preferences p; p.begin("sys_health", false);
        p.putInt("fails", 0); // Reset contador
        p.end();
        systemStabilized = true;
        statusLed.setPixelColor(0, 0); statusLed.show(); // Apagar LED
    }
}

// ==========================================
// COMANDOS & UTILIDADES
// ==========================================
void drawStatusBar(String msg, uint16_t color) {
    M5Cardputer.Display.fillRect(0, 0, 240, 18, color);
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.setCursor(2, 2);
    M5Cardputer.Display.print(msg);
}

void cmd_print(String arg) { M5Cardputer.Display.println(arg); }
void cmd_delay(String arg) { delay(arg.toInt()); }
void cmd_color(String arg) { 
    uint32_t c = (uint32_t)strtol(arg.c_str(), NULL, 16);
    M5Cardputer.Display.fillScreen(c); M5Cardputer.Display.setCursor(0,20);
}
void cmd_wait(String arg) {
    while(!M5Cardputer.Keyboard.isChange()) {
        M5Cardputer.update(); markSystemStable(); delay(20);
        if(M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) break;
    }
}
void cmd_load(String arg) {
    if(!SD.exists(arg)) { M5Cardputer.Display.println("ERR: No File"); return; }
    updateFromFS(SD, arg);
}
void cmd_edit(String arg) {
    runEditor(arg);
    M5Cardputer.Display.fillScreen(BLACK); M5Cardputer.Display.setCursor(0,0);
}

// ==========================================
// INTERPRETE
// ==========================================
void executeScript(String filename) {
    statusLed.setPixelColor(0, statusLed.Color(50,0,50)); statusLed.show();
    File f = LittleFS.open(filename, "r");
    if(!f) f = SD.open(filename, "r");
    
    if(!f) { M5Cardputer.Display.println("Script missing: " + filename); return; }
    
    while(f.available()) {
        markSystemStable(); // Check estabilidad durante script
        String line = f.readStringUntil('\n');
        line.trim();
        if(line.length() == 0 || line.startsWith("#")) continue;
        
        int sp = line.indexOf(' ');
        String cmd = (sp == -1) ? line : line.substring(0, sp);
        String arg = (sp == -1) ? "" : line.substring(sp+1);
        cmd.toUpperCase();
        
        if(cmdMap.count(cmd)) cmdMap[cmd](arg);
    }
    f.close();
    statusLed.setPixelColor(0, statusLed.Color(0,255,0)); statusLed.show();
}

// ==========================================
// MAIN
// ==========================================
void setup() {
    checkSystemHealth(); // Check inicial
    bootTime = millis(); // Marcar hora de inicio

    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    statusLed.begin(); statusLed.setBrightness(20);
    
    if(!LittleFS.begin(true)) M5Cardputer.Display.println("LFS Init");
    SD.begin(SD_CS_PIN, SPI, 25000000);

    // Mapeo
    cmdMap["PRINT"] = &cmd_print;
    cmdMap["DELAY"] = &cmd_delay;
    cmdMap["COLOR"] = &cmd_color;
    cmdMap["WAIT"]  = &cmd_wait;
    cmdMap["LOAD"]  = &cmd_load;
    cmdMap["EDIT"]  = &cmd_edit;

    // Cargar Config
    String autoScript = "";
    File cfgFile = LittleFS.open("/config.json", "r");
    if(cfgFile) {
        JsonDocument doc; deserializeJson(doc, cfgFile);
        autoScript = doc["auto_exec"].as<String>();
        cfgFile.close();
    }

    if(autoScript.length() > 0) executeScript(autoScript);
    else {
        M5Cardputer.Display.println("No Config. Editor...");
        delay(1000);
        runEditor("/config.json");
    }
}

void loop() {
    M5Cardputer.update();
    markSystemStable(); // Check recurrente
}
