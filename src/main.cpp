/**
 * M5 CARDPUTER OS - RELEASE 1.0 (I+D EDITION)
 * Features: Scripting, Modular Loader, Crash Recovery, Text Editor, File Browser.
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

// --- CONFIGURACIÓN ---
#define SD_CS_PIN       40
#define LED_PIN         21
#define RECOVERY_BIN    "/recovery.bin"
#define CRASH_LIMIT     3
#define EDITOR_MAX_SIZE 4096 // Limite de 4KB para editar en RAM

// --- OBJETOS ---
Adafruit_NeoPixel statusLed(1, LED_PIN, NEO_GRB + NEO_KHZ800);
Preferences crashTracker;
typedef void (*CmdHandler)(String arg);
std::map<String, CmdHandler> cmdMap;

// --- COLORES ---
const uint32_t LED_BOOT  = statusLed.Color(50, 0, 50);
const uint32_t LED_WORK  = statusLed.Color(0, 0, 255);
const uint32_t LED_OK    = statusLed.Color(0, 255, 0);
const uint32_t LED_ERR   = statusLed.Color(255, 0, 0);

// Declaraciones adelantadas
void executeScript(String filename);
void drawStatusBar(String msg, uint16_t color);

// ==========================================
// MODULO: FILE BROWSER & EDITOR
// ==========================================

// Helper para determinar sistema de archivos
fs::FS* getFS(String path) {
    if (path.startsWith("/sd/") || !path.startsWith("/")) return &SD; // Asumir SD si no empieza por / o explícito
    return &LittleFS;
}

// Explorador de Archivos Simple
String fileBrowser(String path = "/") {
    M5Cardputer.Display.fillScreen(BLACK);
    M5Cardputer.Display.setTextSize(1);
    
    fs::FS* fsPtr = &LittleFS; // Por defecto LittleFS
    // Lógica simple: Si pulsas 'S' vas a SD, si pulsas 'L' a LittleFS (Podría mejorarse)
    
    std::vector<String> files;
    File root = fsPtr->open(path);
    if(!root || !root.isDirectory()){
         return "";
    }

    File file = root.openNextFile();
    while(file){
        String name = String(file.name());
        if(file.isDirectory()) name += "/";
        files.push_back(name);
        file = root.openNextFile();
    }
    
    int selected = 0;
    int offset = 0;
    
    while(true) {
        M5Cardputer.Display.fillScreen(BLACK);
        drawStatusBar("BROWSER: " + path + " (UP/DWN/OK)", TFT_BLUE);
        
        for(int i=0; i<8; i++) {
            int idx = offset + i;
            if(idx >= files.size()) break;
            
            if(idx == selected) M5Cardputer.Display.setTextColor(BLACK, WHITE);
            else M5Cardputer.Display.setTextColor(WHITE, BLACK);
            
            M5Cardputer.Display.setCursor(5, 20 + (i*15));
            M5Cardputer.Display.println(files[idx]);
        }

        M5Cardputer.update();
        if(M5Cardputer.Keyboard.isChange()) {
            if(M5Cardputer.Keyboard.isKeyPressed(';')) { // Up (Flecha arriba es ; con Fn, o usar mapeo directo)
               if(selected > 0) selected--;
               if(selected < offset) offset--;
            }
            if(M5Cardputer.Keyboard.isKeyPressed('.')) { // Down
               if(selected < files.size()-1) selected++;
               if(selected >= offset+8) offset++;
            }
            if(M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
                String choice = files[selected];
                if(choice.endsWith("/")) return fileBrowser(path + choice); // Recursivo
                return path + (path == "/" ? "" : "/") + choice;
            }
            if(M5Cardputer.Keyboard.isKeyPressed(KEY_ESC)) return "";
        }
    }
}

// EDITOR DE TEXTO EN RAM
void runEditor(String filepath) {
    fs::FS* fs = (filepath.startsWith("/sd") || !LittleFS.exists(filepath)) ? (fs::FS*)&SD : (fs::FS*)&LittleFS;
    
    // 1. Cargar Archivo
    String buffer = "";
    if(fs->exists(filepath)) {
        File f = fs->open(filepath, "r");
        while(f.available()) buffer += (char)f.read();
        f.close();
    } else {
        buffer = ""; // Nuevo archivo
    }

    int cursor = buffer.length(); // Empezar al final
    int scrollY = 0;
    bool dirty = false;

    while(true) {
        // Renderizado
        M5Cardputer.Display.fillScreen(BLACK);
        drawStatusBar("EDIT: " + filepath + (dirty?"*":""), dirty ? TFT_ORANGE : TFT_DARKGREY);
        
        M5Cardputer.Display.setTextColor(WHITE);
        M5Cardputer.Display.setCursor(0, 20);
        
        // "Viewport" simple: mostramos los ultimos caracteres si el buffer es largo
        // Para I+D simple, mostramos todo y confiamos en el wrap
        M5Cardputer.Display.print(buffer);
        
        // Dibujar Cursor (un guión bajo parpadeante o bloque)
        if((millis()/500)%2==0) M5Cardputer.Display.print("_");

        // Input Loop
        M5Cardputer.update();
        if(M5Cardputer.Keyboard.isChange()) {
            if(M5Cardputer.Keyboard.isPressed()) {
                Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
                
                for(auto i : status.word) {
                    buffer += i;
                    dirty = true;
                }
                
                if(status.del && buffer.length() > 0) {
                    buffer.remove(buffer.length()-1);
                    dirty = true;
                }
                
                if(status.enter) {
                    buffer += "\n";
                    dirty = true;
                }
            }
            
            // Comandos Especiales (Combinaciones)
            // ESC para salir/guardar
            if(M5Cardputer.Keyboard.isKeyPressed(KEY_ESC)) {
                M5Cardputer.Display.fillScreen(BLUE);
                M5Cardputer.Display.setCursor(10,50);
                M5Cardputer.Display.println("ENTER: Save\nESC: Discard");
                while(true) {
                    M5Cardputer.update();
                    if(M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
                        File f = fs->open(filepath, "w");
                        f.print(buffer);
                        f.close();
                        return;
                    }
                    if(M5Cardputer.Keyboard.isKeyPressed(KEY_ESC)) return;
                }
            }
        }
    }
}

// ==========================================
// MODULO: CORE SYSTEM
// ==========================================

void drawStatusBar(String msg, uint16_t color) {
    M5Cardputer.Display.fillRect(0, 0, 240, 18, color);
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.setCursor(2, 2);
    M5Cardputer.Display.print(msg);
}

void checkSystemHealth() {
    crashTracker.begin("sys_health", false);
    int fails = crashTracker.getInt("fails", 0);
    pinMode(0, INPUT_PULLUP);
    bool manualOverride = (digitalRead(0) == LOW);
    crashTracker.putInt("fails", fails + 1); 
    
    if (manualOverride || fails >= CRASH_LIMIT) {
        statusLed.begin(); statusLed.setPixelColor(0, LED_ERR); statusLed.show();
        M5Cardputer.Display.begin(); M5Cardputer.Display.setRotation(1);
        M5Cardputer.Display.fillScreen(RED); M5Cardputer.Display.setTextColor(WHITE);
        M5Cardputer.Display.setTextSize(2); M5Cardputer.Display.setCursor(10, 20);
        if (manualOverride) M5Cardputer.Display.println("MANUAL RECOVERY");
        else M5Cardputer.Display.println("SYSTEM CRASHED");
        SD.begin(SD_CS_PIN, SPI, 25000000);
        if(SD.exists(RECOVERY_BIN)) {
            crashTracker.putInt("fails", 0); crashTracker.end();
            updateFromFS(SD, RECOVERY_BIN);
        } else {
             while(1); 
        }
    }
    crashTracker.end();
}

void watchdogTask(void * parameter) {
    delay(10000);
    Preferences p; p.begin("sys_health", false); p.putInt("fails", 0); p.end();
    statusLed.setPixelColor(0, 0); statusLed.show();
    vTaskDelete(NULL);
}

// ==========================================
// COMANDOS INTERPRETE
// ==========================================

void cmd_print(String arg) { M5Cardputer.Display.println(arg); }
void cmd_delay(String arg) { delay(arg.toInt()); }
void cmd_color(String arg) { 
    uint32_t c = (uint32_t)strtol(arg.c_str(), NULL, 16);
    M5Cardputer.Display.fillScreen(c); M5Cardputer.Display.setCursor(0,20);
}
void cmd_wait(String arg) {
    while(!M5Cardputer.Keyboard.isChange()) {
        M5Cardputer.update(); delay(50);
        if(M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) break;
    }
}
void cmd_load(String arg) {
    if(!SD.exists(arg)) {
        M5Cardputer.Display.println("ERR: File not found"); return;
    }
    updateFromFS(SD, arg);
}
// NUEVO: Comando EDIT
void cmd_edit(String arg) {
    runEditor(arg);
    // Al salir del editor, restaurar pantalla negra
    M5Cardputer.Display.fillScreen(BLACK);
    M5Cardputer.Display.setCursor(0,0);
}

// ==========================================
// INTERPRETE PRINCIPAL
// ==========================================
void executeScript(String filename) {
    statusLed.setPixelColor(0, LED_BOOT); statusLed.show();
    File f = LittleFS.open(filename, "r");
    if(!f) f = SD.open(filename, "r");
    
    if(!f) { M5Cardputer.Display.println("Script missing: " + filename); return; }
    
    while(f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if(line.length() == 0 || line.startsWith("#")) continue;
        
        int sp = line.indexOf(' ');
        String cmd = (sp == -1) ? line : line.substring(0, sp);
        String arg = (sp == -1) ? "" : line.substring(sp+1);
        cmd.toUpperCase();
        
        if(cmdMap.count(cmd)) cmdMap[cmd](arg);
        else M5Cardputer.Display.printf("Unknown: %s\n", cmd.c_str());
    }
    f.close();
    statusLed.setPixelColor(0, LED_OK); statusLed.show();
}

// ==========================================
// MAIN LOOPS
// ==========================================
void setup() {
    checkSystemHealth();
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    statusLed.begin(); statusLed.setBrightness(20);
    
    if(!LittleFS.begin(true)) M5Cardputer.Display.println("LFS Fail");
    SD.begin(SD_CS_PIN, SPI, 25000000);

    // REGISTRO DE COMANDOS
    cmdMap["PRINT"] = &cmd_print;
    cmdMap["DELAY"] = &cmd_delay;
    cmdMap["COLOR"] = &cmd_color;
    cmdMap["WAIT"]  = &cmd_wait;
    cmdMap["LOAD"]  = &cmd_load;
    cmdMap["EDIT"]  = &cmd_edit; // <--- NUEVO

    xTaskCreatePinnedToCore(watchdogTask, "WDT", 2048, NULL, 1, NULL, 1);

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
        // Fallback si no hay config: Lanzar editor directamente
        M5Cardputer.Display.println("No Config. Starting Editor...");
        delay(1000);
        runEditor("/config.json");
    }
}

void loop() {
    M5Cardputer.update();
}
