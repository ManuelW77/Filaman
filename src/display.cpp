#include "display.h"
#include "api.h"
#include <vector>
#include "icons.h"
#include "main.h"

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

bool wifiOn = false;
bool iconToggle = false;

void setupDisplay() {
    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        Serial.println(F("SSD1306 allocation failed"));
        for (;;); // Stoppe das Programm, wenn das Display nicht initialisiert werden kann
    }
    display.setTextColor(WHITE);
    display.clearDisplay();


    oledShowTopRow();
    oledShowProgressBar(0, 7, DISPLAY_BOOT_TEXT, "Display init");
}

void oledclearline() {
    int x, y;
    for (y = 0; y < 16; y++) {
        for (x = 0; x < SCREEN_WIDTH; x++) {
            display.drawPixel(x, y, BLACK);
        }
    }
    //display.display();
}

void oledcleardata() {
    int x, y;
    for (y = OLED_DATA_START; y < OLED_DATA_END; y++) {
        for (x = 0; x < SCREEN_WIDTH; x++) {
            display.drawPixel(x, y, BLACK);
        }
    }
    //display.display();
}

int oled_center_h(const String &text) {
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    return (SCREEN_WIDTH - w) / 2;
}

int oled_center_v(const String &text) {
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(text, 0, OLED_DATA_START, &x1, &y1, &w, &h);
    // Zentrierung nur im Datenbereich zwischen OLED_DATA_START und OLED_DATA_END
    return OLED_DATA_START + ((OLED_DATA_END - OLED_DATA_START - h) / 2);
}

std::vector<String> splitTextIntoLines(const String &text, uint8_t textSize) {
    std::vector<String> lines;
    display.setTextSize(textSize);
    
    // Text in Wörter aufteilen
    std::vector<String> words;
    int pos = 0;
    while (pos < text.length()) {
        // Überspringe Leerzeichen am Anfang
        while (pos < text.length() && text[pos] == ' ') pos++;
        
        // Finde nächstes Leerzeichen
        int nextSpace = text.indexOf(' ', pos);
        if (nextSpace == -1) {
            // Letztes Wort
            if (pos < text.length()) {
                words.push_back(text.substring(pos));
            }
            break;
        }
        // Wort hinzufügen
        words.push_back(text.substring(pos, nextSpace));
        pos = nextSpace + 1;
    }
    
    // Wörter zu Zeilen zusammenfügen
    String currentLine = "";
    for (size_t i = 0; i < words.size(); i++) {
        String testLine = currentLine;
        if (currentLine.length() > 0) testLine += " ";
        testLine += words[i];
        
        // Prüfe ob diese Kombination auf die Zeile passt
        int16_t x1, y1;
        uint16_t lineWidth, h;
        display.getTextBounds(testLine, 0, OLED_DATA_START, &x1, &y1, &lineWidth, &h);
        
        if (lineWidth <= SCREEN_WIDTH) {
            // Passt noch in diese Zeile
            currentLine = testLine;
        } else {
            // Neue Zeile beginnen
            if (currentLine.length() > 0) {
                lines.push_back(currentLine);
                currentLine = words[i];
            } else {
                // Ein einzelnes Wort ist zu lang
                lines.push_back(words[i]);
            }
        }
    }
    
    // Letzte Zeile hinzufügen
    if (currentLine.length() > 0) {
        lines.push_back(currentLine);
    }
    
    return lines;
}

void oledShowMultilineMessage(const String &message, uint8_t size) {
    std::vector<String> lines;
    int maxLines = 3;  // Maximale Anzahl Zeilen für size 2
    
    // Erste Prüfung mit aktueller Größe
    lines = splitTextIntoLines(message, size);
    
    // Wenn mehr als maxLines Zeilen, reduziere Textgröße
    if (lines.size() > maxLines && size > 1) {
        size = 1;
        lines = splitTextIntoLines(message, size);
    }
    
    // Ausgabe
    display.setTextSize(size);
    int lineHeight = size * 8;
    int totalHeight = lines.size() * lineHeight;
    int startY = OLED_DATA_START + ((OLED_DATA_END - OLED_DATA_START - totalHeight) / 2);
    
    uint8_t lineDistance = (lines.size() == 2) ? 5 : 0;
    for (size_t i = 0; i < lines.size(); i++) {
        display.setCursor(oled_center_h(lines[i]), startY + (i * lineHeight) + (i == 1 ? lineDistance : 0));
        display.print(lines[i]);
    }
    
    display.display();
}

void oledShowMessage(const String &message, uint8_t size) {
    oledcleardata();
    display.setTextSize(size);
    display.setTextWrap(false);
    
    // Prüfe ob Text in eine Zeile passt
    int16_t x1, y1;
    uint16_t textWidth, h;
    display.getTextBounds(message, 0, 0, &x1, &y1, &textWidth, &h);
   
    // Text passt in eine Zeile?
    if (textWidth <= SCREEN_WIDTH) {
        display.setCursor(oled_center_h(message), oled_center_v(message));
        display.print(message);
        display.display();
    } else {
        oledShowMultilineMessage(message, size);
    }
}

void oledShowTopRow() {
    oledclearline();

    display.setTextSize(1);
    display.setCursor(0, 4);
    display.print("v");
    display.print(VERSION);

    iconToggle = !iconToggle;

    // Do not show status indicators during boot
    if(!booting){
        if(bambuDisabled == false) {
            if (bambu_connected == 1) {
                display.drawBitmap(50, 0, bitmap_bambu_on , 16, 16, WHITE);
            } else {
                if(iconToggle){
                    display.drawBitmap(50, 0, bitmap_bambu_on , 16, 16, WHITE);
                    display.drawLine(50, 15, 66, 0, WHITE);
                    display.drawLine(51, 15, 67, 0, WHITE);
                }
            }
        }

        if (spoolmanConnected) {
            display.drawBitmap(80, 0, bitmap_spoolman_on , 16, 16, WHITE);
        } else {
            if(iconToggle){
                display.drawBitmap(80, 0, bitmap_spoolman_on , 16, 16, WHITE);
                display.drawLine(80, 15, 96, 0, WHITE);
                display.drawLine(81, 15, 97, 0, WHITE);
            }
        }

        if (wifiOn == 1) {
            display.drawBitmap(107, 0, wifi_on , 16, 16, WHITE);
        } else {
            if(iconToggle){
                display.drawBitmap(107, 0, wifi_on , 16, 16, WHITE);
                display.drawLine(107, 15, 123, 0, WHITE);
                display.drawLine(108, 15, 124, 0, WHITE);
            }
        }
    }
    
    display.display();
}

void oledShowIcon(const char* icon) {
    oledcleardata();

    uint16_t iconSize = OLED_DATA_END-OLED_DATA_START;
    uint16_t iconStart = (SCREEN_WIDTH - iconSize) / 2;

    if (icon == "failed") {
        display.drawBitmap(iconStart, OLED_DATA_START, icon_failed , iconSize, iconSize, WHITE);
    }
    else if (icon == "success") {
        display.drawBitmap(iconStart, OLED_DATA_START, icon_success , iconSize, iconSize, WHITE);
    }
    else if (icon == "transfer") {
        display.drawBitmap(iconStart, OLED_DATA_START, icon_transfer , iconSize, iconSize, WHITE);
    }
    else if (icon == "loading") {
        display.drawBitmap(iconStart, OLED_DATA_START, icon_loading , iconSize, iconSize, WHITE);
    }

    display.display();
}

void oledShowProgressBar(const uint8_t step, const uint8_t numSteps, const char* largeText, const char* statusMessage) {
    assert(step <= numSteps);

    // clear data and bar area
    display.fillRect(0, OLED_DATA_START, SCREEN_WIDTH, SCREEN_HEIGHT-16, BLACK);

    
    display.setTextWrap(false);
    display.setTextSize(2);
    display.setCursor(0, OLED_DATA_START+4);
    display.print(largeText);
    display.setTextSize(1);
    display.setCursor(0, OLED_DATA_END-SCREEN_PROGRESS_BAR_HEIGHT-10);
    display.print(statusMessage);

    const int barLength = ((SCREEN_WIDTH-2)*step)/numSteps;
    display.drawRoundRect(0, SCREEN_HEIGHT-SCREEN_PROGRESS_BAR_HEIGHT, SCREEN_WIDTH, 12, 6, WHITE);
    display.fillRoundRect(1, SCREEN_HEIGHT-SCREEN_PROGRESS_BAR_HEIGHT+1, barLength, 10, 6, WHITE);
    display.display();
}

void oledShowWeight(uint16_t weight) {
    // Display Gewicht
    oledcleardata();
    display.setTextSize(3);
    display.setCursor(oled_center_h(String(weight)+" g"), OLED_DATA_START+10);
    display.print(weight);
    display.print(" g");
    display.display();
}
