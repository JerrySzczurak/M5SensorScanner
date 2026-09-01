#include "m5dial_text_input.h"
#include "M5Dial.h"

// ============================================================
// Zestawy znakow na pierscieniu. Kazdy element to krotki string
// (1-3 znaki), bo M5Dial ma tylko okragly ekran 1.28" - miejsce jest
// bardzo ograniczone.
//
// Tryb ALNUM: a-z, 0-9, potem 4 funkcyjne: SHIFT, DEL, "#" (przelacz na
// znaki specjalne), OK (zatwierdz cala wpisana wartosc).
// Tryb SYMBOLS: typowe znaki specjalne + ABC (powrot do ALNUM) + DEL + OK.
// ============================================================

static const char* ALNUM_ITEMS[] = {
  "a","b","c","d","e","f","g","h","i","j","k","l","m",
  "n","o","p","q","r","s","t","u","v","w","x","y","z",
  "0","1","2","3","4","5","6","7","8","9",
  "^", "DEL", "#", "OK"   // SHIFT, BACKSPACE, przelacznik symboli, potwierdz
};
static const int ALNUM_COUNT = sizeof(ALNUM_ITEMS) / sizeof(ALNUM_ITEMS[0]);
#define ALNUM_SHIFT_IDX  (ALNUM_COUNT - 4)
#define ALNUM_DEL_IDX    (ALNUM_COUNT - 3)
#define ALNUM_SYM_IDX    (ALNUM_COUNT - 2)
#define ALNUM_OK_IDX     (ALNUM_COUNT - 1)

static const char* SYMBOL_ITEMS[] = {
  "!","@","#","$","%","^","&","*","(",")",
  "-","_","+","=",".",",","?","/",":",";","'","\"",
  "ABC", "DEL", "OK"      // powrot do liter, backspace, potwierdz
};
static const int SYMBOL_COUNT = sizeof(SYMBOL_ITEMS) / sizeof(SYMBOL_ITEMS[0]);
#define SYMBOL_ABC_IDX  (SYMBOL_COUNT - 3)
#define SYMBOL_DEL_IDX  (SYMBOL_COUNT - 2)
#define SYMBOL_OK_IDX   (SYMBOL_COUNT - 1)

enum RingMode { MODE_ALNUM, MODE_SYMBOLS };

// ============================================================
// Stan modulu
// ============================================================
static RingMode ringMode = MODE_ALNUM;
static int   selectedIndex   = 0;
static long  encoderOldPos   = 0;
static bool  capsOn          = false;
static bool  active          = false;

static char textBuffer[TEXT_INPUT_MAX_LEN + 1] = "";
static int  textLen = 0;

static char promptText[32] = "";

// ============================================================
// Kolory
// ============================================================
static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
static const uint16_t COL_BG        = rgb565(245, 247, 250);
static const uint16_t COL_DIM       = rgb565(150, 150, 150);
static const uint16_t COL_HILITE_BG = rgb565(40, 110, 200);
static const uint16_t COL_HILITE_FG = rgb565(255, 255, 255);
static const uint16_t COL_PREVIEW   = rgb565(30, 90, 160);
static const uint16_t COL_TEXT      = rgb565(20, 20, 20);
static const uint16_t COL_PROMPT    = rgb565(120, 120, 120);

// ============================================================
// Pomocnicze - liczba elementow / wskaznik na tablice biezacego trybu
// ============================================================
static int currentCount() {
  return (ringMode == MODE_ALNUM) ? ALNUM_COUNT : SYMBOL_COUNT;
}
static const char* currentItem(int idx) {
  return (ringMode == MODE_ALNUM) ? ALNUM_ITEMS[idx] : SYMBOL_ITEMS[idx];
}

// Zwraca znak do dopisania do bufora, z uwzglednieniem Caps - tylko dla
// pojedynczych liter w trybie ALNUM. Dla cyfr/symboli zwraca znak wprost.
static char resolveChar(const char* item) {
  char c = item[0];
  if (ringMode == MODE_ALNUM && c >= 'a' && c <= 'z' && capsOn) {
    return c - 'a' + 'A';
  }
  return c;
}

// ============================================================
// Rysowanie calego ekranu (pierscien + podglad + wpisany tekst)
// ============================================================
static void drawScreen() {
  int w  = M5Dial.Display.width();
  int h  = M5Dial.Display.height();
  int cx = w / 2;
  int cy = h / 2;
  int radius = (min(w, h) / 2) - 20;

  M5Dial.Display.startWrite();
  M5Dial.Display.fillScreen(COL_BG);

  int count = currentCount();
  float angleStep = 2.0f * PI / count;

  // --- Pierscien znakow ---
  for (int i = 0; i < count; i++) {
    float angle = -PI / 2.0f + i * angleStep;
    int x = cx + (int)(radius * cosf(angle));
    int y = cy + (int)(radius * sinf(angle));

    bool isSelected = (i == selectedIndex);
    const char* item = currentItem(i);

    M5Dial.Display.setTextDatum(middle_center);
    if (isSelected) {
      M5Dial.Display.fillCircle(x, y, 15, COL_HILITE_BG);
      M5Dial.Display.setTextColor(COL_HILITE_FG);
      M5Dial.Display.setTextSize(2);
    } else {
      M5Dial.Display.setTextColor(COL_DIM);
      M5Dial.Display.setTextSize(1);
    }

    if (ringMode == MODE_ALNUM && i < 26) {
      char shown[2] = { resolveChar(item), '\0' };
      M5Dial.Display.drawString(shown, x, y);
    } else {
      M5Dial.Display.drawString(item, x, y);
    }
  }

  // --- Podglad aktualnie wskazywanego znaku (duzy, srodek gora) ---
  const char* selItem = currentItem(selectedIndex);
  char previewBuf[4];
  if (ringMode == MODE_ALNUM && selectedIndex < 26) {
    previewBuf[0] = resolveChar(selItem);
    previewBuf[1] = '\0';
  } else {
    strncpy(previewBuf, selItem, sizeof(previewBuf) - 1);
    previewBuf[sizeof(previewBuf) - 1] = '\0';
  }
  M5Dial.Display.setTextDatum(middle_center);
  M5Dial.Display.setTextColor(COL_PREVIEW);
  M5Dial.Display.setTextSize(4);
  M5Dial.Display.drawString(previewBuf, cx, cy - 25);

  // --- Prompt ---
  M5Dial.Display.setTextColor(COL_PROMPT);
  M5Dial.Display.setTextSize(1);
  M5Dial.Display.drawString(promptText, cx, cy + 5);

  // --- Wpisany dotychczas tekst (ostatnie znaki jesli za dlugi) ---
  const char* visible = textBuffer;
  int maxVisibleChars = 12;
  if (textLen > maxVisibleChars) {
    visible = textBuffer + (textLen - maxVisibleChars);
  }
  M5Dial.Display.setTextColor(COL_TEXT);
  M5Dial.Display.setTextSize(2);
  M5Dial.Display.drawString(visible, cx, cy + 25);
  if (textLen == 0) {
    M5Dial.Display.setTextColor(COL_DIM);
    M5Dial.Display.drawString("(pusty)", cx, cy + 25);
  }

  M5Dial.Display.endWrite();
}

// ============================================================
// Obsluga akcji po potwierdzeniu wybranego elementu (klik pokretla)
// ============================================================
static void handleConfirmSelected() {
  const char* item = currentItem(selectedIndex);

  if (ringMode == MODE_ALNUM) {
    if (selectedIndex == ALNUM_SHIFT_IDX) {
      capsOn = !capsOn;
      return;
    }
    if (selectedIndex == ALNUM_DEL_IDX) {
      if (textLen > 0) { textLen--; textBuffer[textLen] = '\0'; }
      return;
    }
    if (selectedIndex == ALNUM_SYM_IDX) {
      ringMode = MODE_SYMBOLS;
      selectedIndex = 0;
      return;
    }
    if (selectedIndex == ALNUM_OK_IDX) {
      active = false;
      return;
    }
    if (textLen < TEXT_INPUT_MAX_LEN) {
      textBuffer[textLen++] = resolveChar(item);
      textBuffer[textLen] = '\0';
    }
  } else {
    if (selectedIndex == SYMBOL_ABC_IDX) {
      ringMode = MODE_ALNUM;
      selectedIndex = 0;
      return;
    }
    if (selectedIndex == SYMBOL_DEL_IDX) {
      if (textLen > 0) { textLen--; textBuffer[textLen] = '\0'; }
      return;
    }
    if (selectedIndex == SYMBOL_OK_IDX) {
      active = false;
      return;
    }
    if (textLen < TEXT_INPUT_MAX_LEN) {
      textBuffer[textLen++] = item[0];
      textBuffer[textLen] = '\0';
    }
  }
}

// ============================================================
// Publiczne API
// ============================================================
void textInputBegin(const char* prompt) {
  strncpy(promptText, prompt, sizeof(promptText) - 1);
  promptText[sizeof(promptText) - 1] = '\0';

  textBuffer[0] = '\0';
  textLen = 0;
  ringMode = MODE_ALNUM;
  selectedIndex = 0;
  capsOn = false;
  active = true;

  encoderOldPos = M5Dial.Encoder.read();
  drawScreen();
}

bool textInputUpdate() {
  if (!active) {
    return false;
  }

  M5Dial.update();

  bool needRedraw = false;

  long newPos = M5Dial.Encoder.read();
  if (newPos != encoderOldPos) {
    int step = (newPos > encoderOldPos) ? 1 : -1;
    encoderOldPos = newPos;

    int count = currentCount();
    selectedIndex = (selectedIndex + step + count) % count;

    M5Dial.Speaker.tone(3500, 10);
    needRedraw = true;
  }

  if (M5Dial.BtnA.wasPressed()) {
    handleConfirmSelected();
    M5Dial.Speaker.tone(2000, 25);
    needRedraw = true;
  }

  if (!active) {
    return true;
  }

  if (needRedraw) {
    drawScreen();
  }

  return false;
}

const char* textInputGetResult() {
  return textBuffer;
}

bool textInputIsActive() {
  return active;
}
