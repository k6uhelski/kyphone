// Inkplate_Prop_Screenshot.ino
//
// Standalone prop for the YouTube shoot — cycles between the KyPhone Home
// screen (TEXT selected) and a huge-letters "KyPhone" title card every
// CYCLE_MS. Not wired to the Radxa, no SPI slave logic, nothing else
// running. Home screen rendering is copied verbatim from render_home2()
// in Inkplate_SPI_Peripheral.ino so it's a visual match for the real UI.
//
// To change what's shown, edit the constants below and reflash.

#include <Inkplate.h>

Inkplate display(INKPLATE_1BIT);

const char* TIME_STR   = "9:41 AM";
const int   HOME_INDEX = 0;  // 0=TEXT 1=CALL 2=READ 3=LISTEN, -1=header
const int   UNREAD     = 0;
const unsigned long CYCLE_MS = 10000UL;

void draw_home_screen() {
    display.clearDisplay();
    display.setTextColor(BLACK);

    const int header_h = 60;
    bool header_sel = (HOME_INDEX == -1);

    if (header_sel) {
        display.fillRect(0, 0, 600, header_h, BLACK);
        display.setTextColor(WHITE);
    } else {
        display.setTextColor(BLACK);
    }

    // "KYPHONE" — left, textSize 2 (bold = double-print)
    display.setTextSize(2);
    display.setCursor(24, 18);  display.print("KYPHONE");
    display.setCursor(25, 18);  display.print("KYPHONE");

    // Clock — right, textSize 3 (18px/char)
    display.setTextSize(3);
    int tw = strlen(TIME_STR) * 18;
    display.setCursor(600 - 24 - tw, 14);
    display.print(TIME_STR);

    // Header rule (2px)
    display.drawLine(0, header_h,     600, header_h,     BLACK);
    display.drawLine(0, header_h + 1, 600, header_h + 1, BLACK);

    // 4 menu rows
    const char* labels[]  = {"TEXT", "CALL", "READ", "LISTEN"};
    const char* numbers[] = {"01",   "02",   "03",   "04"};
    const int row_h    = (600 - header_h) / 4;
    const int pad_left = 160; // left-justified, but shifted right to center the block
    const int num_w    = 36;
    const int gap      = 20;
    const int label_x  = pad_left + num_w + gap;

    for (int i = 0; i < 4; i++) {
        int y   = header_h + i * row_h;
        bool sel = (i == HOME_INDEX);

        if (sel) {
            display.fillRect(0, y, 600, row_h, BLACK);
            display.setTextColor(WHITE);
        } else {
            display.setTextColor(BLACK);
        }

        display.setTextSize(3);
        display.setCursor(pad_left, y + (row_h - 24) / 2 + 12);
        display.print(numbers[i]);

        display.setTextSize(6);
        display.setCursor(label_x, y + (row_h - 48) / 2);
        display.print(labels[i]);
        display.setCursor(label_x + 1, y + (row_h - 48) / 2);
        display.print(labels[i]);

        if (i == 0 && UNREAD > 0) {
            uint16_t bg = sel ? WHITE : BLACK;
            uint16_t fg = sel ? BLACK : WHITE;
            int bx = 600 - 50, by = y + (row_h - 24) / 2;
            display.fillRect(bx, by, 28, 24, bg);
            char badge[2] = {'0' + (char)(UNREAD > 9 ? 9 : UNREAD), '\0'};
            display.setTextSize(2);
            display.setTextColor(fg);
            display.setCursor(bx + 6, by + 4);
            display.print(badge);
        }

        display.setTextColor(BLACK);
        display.drawLine(0, y + row_h, 600, y + row_h, BLACK);
    }

    display.display();
}

void draw_kyphone_screen() {
    display.clearDisplay();
    display.setTextColor(BLACK);

    const char* text = "KyPhone";
    display.setTextSize(12);  // 72px wide, 96px tall per char
    int tw = strlen(text) * 6 * 12;
    int th = 8 * 12;
    int tx = (600 - tw) / 2;
    int ty = (600 - th) / 2;

    display.setCursor(tx,     ty); display.print(text);
    display.setCursor(tx + 1, ty); display.print(text);  // bold

    display.display();
}

void setup() {
    display.begin();
    draw_home_screen();
}

void loop() {
    delay(CYCLE_MS);
    draw_kyphone_screen();
    delay(CYCLE_MS);
    draw_home_screen();
}
