/*
  vt100_stm32.ino - VT100 Terminal Emulator for Arduino STM32
  Copyright (c) 2018 Hideaki Tominaga. All rights reserved.
*/

#include <SPI.h>
#include <tone.h>
#include <libmaple/nvic.h>

#include <Adafruit_GFX_AS.h>      // Core graphics library
#include <Adafruit_ILI9341_STM.h> // Hardware-specific library
#include <font6x8tt.h>            // 6x8ドットフォント
#include <PS2Keyboard_stm32.h>    // PS/2 Keyboard Library for STM32

// TFT 制御用ピン
#define TFT_CS   PA4
#define TFT_RST  PA3
#define TFT_DC   PA2

// キーボード制御用ピン (5V トレラント)
#define KBD_DAT  PB7
#define KBD_CLK  PB6

// スピーカー制御用ピン
#define SPK_PIN  PA8

// LED 制御用ピン
#define LED_01  PB15
#define LED_02  PB14
#define LED_03  PB13
#define LED_04  PB12

// デフォルト通信速度
#define DEFAULT_BAUDRATE 9600

// TFT 制御用
Adafruit_ILI9341_STM tft = Adafruit_ILI9341_STM(TFT_CS, TFT_DC, TFT_RST);

// キーボード制御用
PS2Keyboard keyboard;

// フォント管理用
uint8_t* fontTop;
#define CH_W    6                       // フォント横サイズ
#define CH_H    8                       // フォント縦サイズ

// スクリーン管理用
#define SC_W    53                      // キャラクタスクリーン横サイズ
#define SC_H    30                      // キャラクタスクリーン縦サイズ
uint16_t M_TOP = 0;                     // 上マージン行
uint16_t M_BOTTOM = SC_H - 1;           // 下マージン行

// 座標やサイズのプレ計算
const uint16_t SCSIZE   = SC_W * SC_H;  // キャラクタスクリーンサイズ
const uint16_t SP_W     = SC_W * CH_W;  // ピクセルスクリーン横サイズ
const uint16_t SP_H     = SC_H * CH_H;  // ピクセルスクリーン縦サイズ
const uint16_t MAX_CH_X = CH_W - 1;     // フォント最大横位置
const uint16_t MAX_CH_Y = CH_H - 1;     // フォント最大縦位置
const uint16_t MAX_SC_X = SC_W - 1;     // キャラクタスクリーン最大横位置
const uint16_t MAX_SC_Y = SC_H - 1;     // キャラクタスクリーン最大縦位置
const uint16_t MAX_SP_X = SP_W - 1;     // ピクセルスクリーン最大横位置
const uint16_t MAX_SP_Y = SP_H - 1;     // ピクセルスクリーン最大縦位置

// 文字アトリビュート用
struct TATTR {
  uint8_t Bold  : 1;     // 1
  uint8_t Lowint  : 1;   // 2
  uint8_t Underline : 1; // 4
  uint8_t Blink : 1;     // 5
  uint8_t Reverse : 1;   // 7
  uint8_t Hide : 1;      // 8
  uint8_t Reserved : 2;
};

union ATTR {
  uint8_t value;
  struct TATTR Bits;
};

// カラーアトリビュート用 (RGB565)
static const uint16_t aColors[] = {
  // Normal
  0x0000, // 0-black
  0xf800, // 1-red
  0x07e0, // 2-green
  0xffe0, // 3-yellow
  0x001f, // 4-blue
  0xf81f, // 5-magenta
  0x07ff, // 6-cyan
  0xffff, // 7-white
  // Blink (暗色表現)
  0x0000, // 8-black (Dark)
  0x8000, // 9-red (Dark)
  0x0400, // 10-green (Dark)
  0x8400, // 11-yellow (Dark)
  0x0010, // 12-blue (Dark)
  0x8010, // 13-magenta (Dark)
  0x0410, // 14-cyan (Dark)
  0x8410  // 15-white (Dark)
};

const uint8_t clBlack = 0;
const uint8_t clRed = 1;
const uint8_t clGreen = 2;
const uint8_t clYellow = 3;
const uint8_t clBlue = 4;
const uint8_t clMagenta = 5;
const uint8_t clCyan = 6;
const uint8_t clWhite = 7;

struct TCOLOR {
  uint8_t Foreground : 3;
  uint8_t Background : 3;
  uint8_t Reserved : 2;
};
union COLOR {
  uint8_t value;
  struct TCOLOR Color;
};

// バッファ
uint8_t screen[SCSIZE];      // スクリーンバッファ
uint8_t attrib[SCSIZE];      // 文字アトリビュートバッファ
uint8_t colors[SCSIZE];      // カラーアトリビュートバッファ
uint8_t tabs[SC_W];          // タブ位置バッファ

// 状態
bool wrap = true;            // 折り返しモード
bool isShowCursor = false;   // カーソル表示中か？
bool canShowCursor = false;  // カーソル表示可能か？
bool lastShowCursor = false; // 前回のカーソル表示状態
bool hasParam = false;       // <ESC> [ がパラメータを持っているか？
uint8_t escMode = 0;         // エスケープシーケンスモード

// オリジナル情報
int16_t o_XP = 0;
int16_t o_YP = 0;
union COLOR oColor;

// カレント情報
int16_t XP = 0;
int16_t YP = 0;
union ATTR cAttr;
union COLOR cColor;

// バックアップ情報
int16_t b_XP = 0;
int16_t b_YP = 0;
union ATTR bAttr;
union COLOR bColor;

// CSI パラメータ
int16_t nVals = 0;
int16_t vals[10] = {0};

// 関数
// -----------------------------------------------------------------------------

// 指定位置の文字の更新表示
void sc_updateChar(uint16_t x, uint16_t y) {
  uint16_t idx = SC_W * y + x;
  uint8_t c    = screen[idx];        // キャラクタの取得
  uint8_t* ptr = &fontTop[c * CH_H]; // フォントデータ先頭アドレス
  union ATTR a;
  union COLOR l;
  a.value = attrib[idx];             // 文字アトリビュートの取得
  l.value = colors[idx];             // カラーアトリビュートの取得
  uint16_t fore = aColors[l.Color.Foreground | (a.Bits.Blink * 8)];
  uint16_t back = aColors[l.Color.Background | (a.Bits.Blink * 8)];
  if (a.Bits.Reverse) swap(fore, back);
  uint16_t xx = x * CH_W;
  uint16_t yy = y * CH_H;
  tft.setAddrWindow(xx, yy, xx + MAX_CH_X, yy + MAX_CH_Y);
  for (uint8_t i = 0; i < CH_H; i++) {
    bool prev = (a.Bits.Underline && (i == MAX_CH_Y));
    for (uint8_t j = 0; j < CH_W; j++) {
      bool pset = ((*ptr) & (0x80 >> j));
      if (pset || prev) {
        tft.pushColor(fore);
      } else {
        tft.pushColor(back);
      }
      if (a.Bits.Bold)
        prev = pset;
    }
    ptr++;
  }
}

// カーソルの描画
void drawCursor(uint16_t x, uint16_t y) {
  uint16_t xx = x * CH_W;
  uint16_t yy = y * CH_H;
  tft.setAddrWindow(xx, yy, xx + MAX_CH_X, yy + MAX_CH_Y);
  for (uint8_t i = 0; i < CH_H; i++) {
    for (uint8_t j = 0; j < CH_W; j++)
      tft.pushColor(aColors[oColor.Color.Foreground]);
  }
}

// カーソルの表示
void dispCursor(bool forceupdate) {
  if (escMode) 
    return;
  if (!forceupdate)
    isShowCursor = !isShowCursor;
  if (lastShowCursor || (forceupdate && isShowCursor))
    sc_updateChar(o_XP, o_YP);
  if (isShowCursor) {
    drawCursor(XP, YP);
    o_XP = XP;
    o_YP = YP;
  }
  if (!forceupdate)
    canShowCursor = false;
  lastShowCursor = isShowCursor;
}

// 指定行をTFT画面に反映
// 引数
//  ln:行番号（0～29)
void sc_updateLine(uint16_t ln) {
  uint8_t c;
  uint8_t dt;
  uint16_t buf[2][SP_W];
  uint16_t cnt, idx;
  union ATTR a;
  union COLOR l;

  for (uint16_t i = 0; i < CH_H; i++) {            // 1文字高さ分ループ
    cnt = 0;
    for (uint16_t clm = 0; clm < SC_W; clm++) {    // 横文字数分ループ
      idx = ln * SC_W + clm;
      c  = screen[idx];                            // キャラクタの取得
      a.value = attrib[idx];                       // 文字アトリビュートの取得
      l.value = colors[idx];                       // カラーアトリビュートの取得
      uint16_t fore = aColors[l.Color.Foreground | (a.Bits.Blink * 8)];
      uint16_t back = aColors[l.Color.Background | (a.Bits.Blink * 8)];
      if (a.Bits.Reverse) swap(fore, back);
      dt = fontTop[c * CH_H + i];                  // 文字内i行データの取得
      bool prev = (a.Bits.Underline && (i == MAX_CH_Y));
      for (uint16_t j = 0; j < CH_W; j++) {
        if ((dt & (0x80 >> j)) || (prev)) {
          buf[i & 1][cnt] = fore;
        } else {
          buf[i & 1][cnt] = back;
        }
        if (a.Bits.Bold)
          prev = dt & (0x80 >> j);
        cnt++;
      }
    }
    tft.pushColors(buf[i & 1], SP_W, true);
  }

  // SPI1 の DMA 転送待ち (SPI1 DMA1 CH3)
  while ((dma_get_isr_bits(DMA1, DMA_CH3) & DMA_ISR_TCIF1) == 0);
}

// カーソルをホーム位置へ移動
void setCursorToHome() {
  XP = 0;
  YP = 0;
}

// カーソル位置と属性の初期化
void initCursorAndAttribute() {
  cAttr.value = 0;
  cColor.value = oColor.value;
  memset(tabs, 0x00, SC_W);
  for (uint8_t i = 0; i < SC_W; i += 8)
    tabs[i] = 1;
  setTopAndBottomMargins(1, SC_H);
}

// 一行スクロール
// (DECSTBM の影響を受ける)
void scroll() {
  XP = 0;
  YP++;
  if (YP > M_BOTTOM) {
    uint16_t n = SCSIZE - SC_W - ((M_TOP + MAX_SC_Y - M_BOTTOM) * SC_W);
    uint16_t idx = SC_W * M_BOTTOM;
    uint16_t idx2;
    uint16_t idx3 = M_TOP * SC_W;
    memmove(&screen[idx3], &screen[idx3 + SC_W], n);
    memmove(&attrib[idx3], &attrib[idx3 + SC_W], n);
    memmove(&colors[idx3], &colors[idx3 + SC_W], n);
    for (uint8_t x = 0; x < SC_W; x++) {
      idx2 = idx + x;
      screen[idx2] = 0;
      attrib[idx2] = 0;
      colors[idx2] = oColor.value;
    }
    tft.setAddrWindow(0, M_TOP * CH_H, MAX_SP_X, (M_BOTTOM + 1) * CH_H - 1);
    for (uint8_t y = M_TOP; y <= M_BOTTOM; y++)
      sc_updateLine(y);
    YP = M_BOTTOM;
  }
}

// パラメータのクリア
void clearParams(uint8_t m) {
  escMode = m;
  nVals = 0;
  vals[0] = vals[1] = vals[2] = vals[3] = 0;
  hasParam = false;
}

// 文字描画
void printChar(char c) {
  // [ESC] キー
  if (c == 0x1b) {
    escMode = 1;   // エスケープシーケンス開始
    return;
  }
  // エスケープシーケンス
  if (escMode == 1) {
    switch (c) {
      case '[':
        // Control Sequence Introducer (CSI) シーケンス へ
        clearParams(2);
        break;
      case '#':
        // Line Size Command  シーケンス へ
        clearParams(4);
        break;
      case '(':
        // G0 セット シーケンス へ
        clearParams(5);
        break;
      case ')':
        // G1 セット シーケンス へ
        clearParams(6);
        break;
      default:
        // <ESC> xxx: エスケープシーケンス
        switch (c) {
          case '7':
            // DECSC (Save Cursor): カーソル位置と属性を保存
            saveCursor();
            break;
          case '8':
            // DECRC (Restore Cursor): 保存したカーソル位置と属性を復帰
            restoreCursor();
            break;
          case '=':
            // DECKPAM (Keypad Application Mode): アプリケーションキーパッドモードにセット
            keypadApplicationMode();
            break;
          case '>':
            // DECKPNM (Keypad Numeric Mode): 数値キーパッドモードにセット
            keypadNumericMode();
            break;
          case 'D':
            // IND (Index): カーソルを一行下へ移動
            index(1);
            break;
          case 'E':
            // NEL (Next Line): 改行、カーソルを次行の最初へ移動
            nextLine();
            break;
          case 'H':
            // HTS (Horizontal Tabulation Set): 現在の桁位置にタブストップを設定
            horizontalTabulationSet();
            break;
          case 'M':
            // RI (Reverse Index): カーソルを一行上へ移動
            reverceIndex(1);
            break;
          case 'Z':
            // DECID (Identify): 端末IDシーケンスを送信
            identify();
            break;
          case 'c':
            // RIS (Reset To Initial State): リセット
            resetToInitialState();
            break;
          default:
            // 未確認のシーケンス
            unknownSequence(escMode, c);
            break;
        }
        clearParams(0);
        break;
    }
    return;
  }

  // "[" Control Sequence Introducer (CSI) シーケンス
  if (escMode == 2) {
    switch (c) {
      case '?':
        escMode = 21;
        break;
      default:
        escMode = 20;
        break;
    }
  }

  int16_t v1 = 0;
  int16_t v2 = 0;

  if ((escMode == 20) || (escMode == 21)) {
    if (isdigit(c)) {
      // [パラメータ]
      vals[nVals] = vals[nVals] * 10 + (c - '0');
      hasParam = true;
    } else if (c == ';') {
      // [セパレータ]
      nVals++;
      hasParam = false;
    } else {
      if (hasParam) nVals++;
      switch (c) {
        case 'A':
          // CUU (Cursor Up): カーソルをPl行上へ移動
          v1 = (nVals == 0) ? 1 : vals[0];
          reverceIndex(v1);
          break;
        case 'B':
          // CUD (Cursor Down): カーソルをPl行下へ移動
          v1 = (nVals == 0) ? 1 : vals[0];
          cursorDown(v1);
          break;
        case 'C':
          // CUF (Cursor Forward): カーソルをPc桁右へ移動
          v1 = (nVals == 0) ? 1 : vals[0];
          cursorForward(v1);
          break;
        case 'D':
          // CUB (Cursor Backward): カーソルをPc桁左へ移動
          v1 = (nVals == 0) ? 1 : vals[0];
          cursorBackward(v1);
          break;
        case 'H':
        // CUP (Cursor Position): カーソルをPl行Pc桁へ移動
        case 'f':
          // HVP (Horizontal and Vertical Position): カーソルをPl行Pc桁へ移動
          v1 = (nVals == 0) ? 1 : vals[0];
          v2 = (nVals <= 1) ? 1 : vals[1];
          cursorPosition(v1, v2);
          break;
        case 'J':
          // ED (Erase In Display): 画面を消去
          v1 = (nVals == 0) ? 0 : vals[0];
          eraseInDisplay(v1);
          break;
        case 'K':
          // EL (Erase In Line) 行を消去
          v1 = (nVals == 0) ? 0 : vals[0];
          eraseInLine(v1);
          break;
        case 'L':
          // IL (Insert Line): カーソルのある行の前に Ps 行空行を挿入
          v1 = (nVals == 0) ? 1 : vals[0];
          insertLine(v1);
          break;
        case 'M':
          // DL (Delete Line): カーソルのある行から Ps 行を削除
          v1 = (nVals == 0) ? 1 : vals[0];
          deleteLine(v1);
          break;
        case 'c':
          // DA (Device Attributes): 装置オプションのレポート
          v1 = (nVals == 0) ? 0 : vals[0];
          deviceAttributes(v1);
          break;
        case 'g':
          // TBC (Tabulation Clear): タブストップをクリア
          v1 = (nVals == 0) ? 0 : vals[0];
          tabulationClear(v1);
          break;
        case 'h':
          // SM (Set Mode): モードのセット
          setMode(vals, nVals);
          break;
        case 'l':
          // RM (Reset Mode): モードのリセット
          resetMode(vals, nVals);
          break;
        case 'm':
          // SGR (Select Graphic Rendition): 文字修飾の設定
          if (nVals == 0)
            nVals = 1; // vals[0] = 0
          selectGraphicRendition(vals, nVals);
          break;
        case 'n':
          // DSR (Device Status Report): 端末状態のリポート
          v1 = (nVals == 0) ? 0 : vals[0];
          deviceStatusReport(v1);
          break;
        case 'q':
          // DECLL (Load LEDS): LED の設定
          v1 = (nVals == 0) ? 0 : vals[0];
          loadLEDs(v1);
          break;
        case 'r':
          // DECSTBM (Set Top and Bottom Margins): スクロール範囲をPt行からPb行に設定
          v1 = (nVals == 0) ? 1 : vals[0];
          v2 = (nVals <= 1) ? SC_H : vals[1];
          setTopAndBottomMargins(v1, v2);
          break;
        case 'y':
          // DECTST (Invoke Confidence Test): テスト診断を行う
          if ((nVals > 1) && (vals[0] = 2))
            invokeConfidenceTests(vals[1]);
          break;
        default:
          // 未確認のシーケンス
          unknownSequence(escMode, c);
          break;
      }
      clearParams(0);
    }
    return;
  }

  // "#" Line Size Command  シーケンス
  if (escMode == 4) {
    switch (c) {
      case '3':
        // DECDHL (Double Height Line): カーソル行を倍高、倍幅、トップハーフへ変更
        doubleHeightLine_TopHalf();
        break;
      case '4':
        // DECDHL (Double Height Line): カーソル行を倍高、倍幅、ボトムハーフへ変更
        doubleHeightLine_BotomHalf();
        break;
      case '5':
        // DECSWL (Single-width Line): カーソル行を単高、単幅へ変更
        singleWidthLine();
        break;
      case '6':
        // DECDWL (Double-Width Line): カーソル行を単高、倍幅へ変更
        doubleWidthLine();
        break;
      case '8':
        // DECALN (Screen Alignment Display): 画面を文字‘E’で埋める
        screenAlignmentDisplay();
        break;
      default:
        // 未確認のシーケンス
        unknownSequence(escMode, c);
        break;
    }
    clearParams(0);
    return;
  }

  // "(" G0 セットシーケンス
  if (escMode == 5) {
    // SCS (Select Character Set): G0 文字コードの設定
    setG0charset(c);
    clearParams(0);
    return;
  }

  // ")" G1 セットシーケンス
  if (escMode == 6) {
    // SCS (Select Character Set): G1 文字コードの設定
    setG1charset(c);
    clearParams(0);
    return;
  }

  // 改行 (LF) / (FF)
  if ((c == 0x0a) || (c == 0x0c)) {
    scroll();
    return;
  }

  // 復帰 (CR)
  if (c == 0x0d) {
    XP = 0;
    return;
  }

  // バックスペース (BS)
  if ((c == 0x08) || (c == 0x7f)) {
    cursorBackward(1);
    uint16_t idx = YP * SC_W + XP;
    screen[idx] = 0;
    attrib[idx] = 0;
    colors[idx] = cColor.value;
    sc_updateChar(XP, YP);
    return;
  }

  // タブ (TAB)
  if (c == 0x09) {
    int16_t idx = -1;
    for (int16_t i = XP + 1; i < SC_W; i++) {
      if (tabs[i]) {
        idx = i;
        break;
      }
    }
    XP = (idx == -1) ? MAX_SC_X : idx;
    return;
  }

  // 通常文字
  if (XP < SC_W) {
    uint16_t idx = YP * SC_W + XP;
    screen[idx] = c;
    attrib[idx] = cAttr.value;
    colors[idx] = cColor.value;
    sc_updateChar(XP, YP);
  }

  // X 位置 + 1
  XP ++;

  // ワードラップ
  if (XP >= SC_W) {
    if (wrap)
      scroll();
    else
      XP = MAX_SC_X;
  }
}

// 文字列描画
void printString(const char *str) {
  while (*str) printChar(*str++);
}

// エスケープシーケンス
// -----------------------------------------------------------------------------

// DECSC (Save Cursor): カーソル位置と属性を保存
void saveCursor() {
  b_XP = XP;
  b_YP = YP;
  bAttr.value = cAttr.value;
  bColor.value = cColor.value;
}

// DECRC (Restore Cursor): 保存したカーソル位置と属性を復帰
void restoreCursor() {
  XP = b_XP;
  YP = b_YP;
  cAttr.value = bAttr.value;
  cColor.value = bColor.value;
}

// DECKPAM (Keypad Application Mode): アプリケーションキーパッドモードにセット
void keypadApplicationMode() {
  Serial.println("Unimplement: keypadApplicationMode");
}

// DECKPNM (Keypad Numeric Mode): 数値キーパッドモードにセット
void keypadNumericMode() {
  Serial.println("Unimplement: keypadNumericMode");
}

// IND (Index): カーソルを一行下へ移動
// (DECSTBM の影響を受ける)
void index(int16_t v) {
  cursorDown(v);
}

// NEL (Next Line): 改行、カーソルを次行の最初へ移動
// (DECSTBM の影響を受ける)
void nextLine() {
  scroll();
}

// HTS (Horizontal Tabulation Set): 現在の桁位置にタブストップを設定
void horizontalTabulationSet() {
  tabs[XP] = 1;
}

// RI (Reverse Index): カーソルを一行上へ移動
// (DECSTBM の影響を受ける)
void reverceIndex(int16_t v) {
  cursorUp(v);
}

// DECID (Identify): 端末IDシーケンスを送信
void identify() {
  deviceAttributes(0); // same as DA (Device Attributes)
}

// RIS (Reset To Initial State) リセット
void resetToInitialState() {
  initCursorAndAttribute();
  eraseInDisplay(2);
}

// "[" Control Sequence Introducer (CSI) シーケンス
// -----------------------------------------------------------------------------

// CUU (Cursor Up): カーソルをPl行上へ移動
// (DECSTBM の影響を受ける)
void cursorUp(int16_t v) {
  YP -= v;
  if (YP <= M_TOP) YP = M_TOP;
}

// CUD (Cursor Down): カーソルをPl行下へ移動
// (DECSTBM の影響を受ける)
void cursorDown(int16_t v) {
  YP += v;
  if (YP >= M_BOTTOM) YP = M_BOTTOM;
}

// CUF (Cursor Forward): カーソルをPc桁右へ移動
void cursorForward(int16_t v) {
  XP += v;
  if (XP >= SC_W) XP = MAX_SC_X;
}

// CUB (Cursor Backward): カーソルをPc桁左へ移動
void cursorBackward(int16_t v) {
  XP -= v;
  if (XP <= 0) XP = 0;
}

// CUP (Cursor Position): カーソルをPl行Pc桁へ移動
// HVP (Horizontal and Vertical Position): カーソルをPl行Pc桁へ移動
void cursorPosition(uint8_t y, uint8_t x) {
  XP = x - 1;
  if (XP >= SC_W) XP = MAX_SC_X;
  YP = y - 1;
  if (YP >= SC_H) YP = MAX_SC_Y;
}

// ED (Erase In Display): 画面を消去
void eraseInDisplay(uint8_t m) {
  uint8_t sl = 0, el = 0;
  uint16_t idx, n;

  switch (m) {
    case 0:
      // カーソルから画面の終わりまでを消去
      sl = YP;
      el = MAX_SC_Y;
      idx = YP * SC_W + XP;
      n   = SCSIZE - (YP * SC_W + XP);
      break;
    case 1:
      // 画面の始めからカーソルまでを消去
      sl = 0;
      el = YP;
      idx = 0;
      n = YP * SC_W + XP + 1;
      break;
    case 2:
      // 画面全体を消去
      sl = 0;
      el = MAX_SC_Y;
      idx = 0;
      n = SCSIZE;
      break;
  }

  if (m <= 2) {
    memset(&screen[idx], 0x00, n);
    memset(&attrib[idx], 0x00, n);
    memset(&colors[idx], 0x00, n);
    tft.setAddrWindow(0, sl * CH_H, MAX_SP_X, (el + 1) * CH_H - 1);
    for (uint8_t i = sl; i <= el; i++)
      sc_updateLine(i);
  }
}

// EL (Erase In Line): 行を消去
void eraseInLine(uint8_t m) {
  uint16_t slp = 0, elp = 0;

  switch (m) {
    case 0:
      // カーソルから行の終わりまでを消去
      slp = YP * SC_W + XP;
      elp = YP * SC_W + MAX_SC_X;
      break;
    case 1:
      // 行の始めからカーソルまでを消去
      slp = YP * SC_W;
      elp = YP * SC_W + XP;
      break;
    case 2:
      // 行全体を消去
      slp = YP * SC_W;
      elp = YP * SC_W + MAX_SC_X;
      break;
  }

  if (m <= 2) {
    uint16_t n = elp - slp + 1;
    memset(&screen[slp], 0x00, n);
    memset(&attrib[slp], 0x00, n);
    memset(&colors[slp], 0x00, n);
    tft.setAddrWindow(0, YP * CH_H, MAX_SP_X, (YP + 1) * CH_H - 1);
    sc_updateLine(YP);
  }
}

// IL (Insert Line): カーソルのある行の前に Ps 行空行を挿入
// (DECSTBM の影響を受ける)
void insertLine(uint8_t v) {
  int16_t rows = v;
  if (rows == 0) return;
  if (rows > ((M_BOTTOM + 1) - YP)) rows = (M_BOTTOM + 1) - YP;
  int16_t idx = SC_W * YP;
  int16_t n = SC_W * rows;
  int16_t idx2 = idx + n;
  int16_t move_rows = (M_BOTTOM + 1) - YP - rows;
  int16_t n2 = SC_W * move_rows;

  if (move_rows > 0) {
    memmove(&screen[idx2], &screen[idx], n2);
    memmove(&attrib[idx2], &attrib[idx], n2);
    memmove(&colors[idx2], &colors[idx], n2);
  }
  memset(&screen[idx], 0x00, n);
  memset(&attrib[idx], 0x00, n);
  memset(&colors[idx], 0x00, n);
  tft.setAddrWindow(0, YP * CH_H, MAX_SP_X, (M_BOTTOM + 1) * CH_H - 1);
  for (uint8_t y = YP; y <= M_BOTTOM; y++)
    sc_updateLine(y);
}

// DL (Delete Line): カーソルのある行から Ps 行を削除
// (DECSTBM の影響を受ける)
void deleteLine(uint8_t v) {
  int16_t rows = v;
  if (rows == 0) return;
  if (rows > ((M_BOTTOM + 1) - YP)) rows = (M_BOTTOM + 1) - YP;
  int16_t idx = SC_W * YP;
  int16_t n = SC_W * rows;
  int16_t idx2 = idx + n;
  int16_t move_rows = (M_BOTTOM + 1) - YP - rows;
  int16_t n2 = SC_W * move_rows;
  int16_t idx3 = (M_BOTTOM + 1) * SC_W - n;

  if (move_rows > 0) {
    memmove(&screen[idx], &screen[idx2], n2);
    memmove(&attrib[idx], &attrib[idx2], n2);
    memmove(&colors[idx], &colors[idx2], n2);
  }
  memset(&screen[idx3], 0x00, n);
  memset(&attrib[idx3], 0x00, n);
  memset(&colors[idx3], 0x00, n);
  tft.setAddrWindow(0, YP * CH_H, MAX_SP_X, (M_BOTTOM + 1) * CH_H - 1);
  for (uint8_t y = YP; y <= M_BOTTOM; y++)
    sc_updateLine(y);
}

// CPR (Cursor Position Report): カーソル位置のレポート
void cursorPositionReport(uint16_t x, uint16_t y) {
  Serial3.print("\e[");
  Serial3.print(String(x, DEC));
  Serial3.print(";");
  Serial3.print(String(y, DEC));
  Serial3.print("R"); // CPR (Cursor Position Report)
}

// DA (Device Attributes): 装置オプションのレポート
// オプションのレポート
void deviceAttributes(uint8_t m) {
  Serial3.print("\e[?1;2c"); // 2 Advanced video option (AVO)
}

// TBC (Tabulation Clear): タブストップをクリア
void tabulationClear(uint8_t m) {
  switch (m) {
    case 0:
      // 現在位置のタブストップをクリア
      tabs[XP] = 0;
      break;
    case 3:
      // すべてのタブストップをクリア
      memset(tabs, 0x00, SC_W);
      break;
  }
}

// SM (Set Mode): モードのセット
void setMode(int16_t *vals, int16_t nVals) {
  for (int16_t i = 0; i < nVals; i++) {
    switch (vals[i]) {
      case 7:
        // 文字の折り返しをオンにセット
        wrap = true;
        break;
      default:
        Serial.print("Unimplement: setMode ");
        Serial.println(String(vals[i], DEC));
        break;
    }
  }
}

// RM (Reset Mode): モードのリセット
void resetMode(int16_t *vals, int16_t nVals) {
  for (int16_t i = 0; i < nVals; i++) {
    switch (vals[i]) {
      case 7:
        // 文字の折り返しをオフにセット
        wrap = false;
        break;
      default:
        Serial.print("Unimplement: resetMode ");
        Serial.println(String(vals[i], DEC));
        break;
    }
  }
}

// SGR (Select Graphic Rendition): 文字修飾の設定
void selectGraphicRendition(int16_t *vals, int16_t nVals) {
  for (int16_t i = 0; i < nVals; i++) {
    int16_t v = vals[i];
    switch (v) {
      case 0:
        // 属性クリア
        cAttr.value = 0;
        cColor.value = oColor.value;
        break;
      case 1:
        // 太字
        cAttr.Bits.Bold = 1;
        break;
      case 2:
        // イタリック
        cAttr.Bits.Lowint = 1;
        break;
      case 4:
        // アンダーライン
        cAttr.Bits.Underline = 1;
        break;
      case 5:
        // 点滅 (暗色表現)
        cAttr.Bits.Blink = 1;
        break;
      case 7:
        // 反転
        cAttr.Bits.Reverse = 1;
        break;
      case 8:
        // 不可視
        cAttr.Bits.Hide = 1;
        break;
      case 21:
        // 太字オフ
        cAttr.Bits.Bold = 0;
        break;
      case 22:
        // イタリックオフ
        cAttr.Bits.Lowint = 0;
        break;
      case 24:
        // アンダーラインオフ
        cAttr.Bits.Underline = 0;
        break;
      case 25:
        // 点滅 (暗色表現) オフ
        cAttr.Bits.Blink = 0;
        break;
      case 27:
        // 反転オフ
        cAttr.Bits.Reverse = 0;
        break;
      case 28:
        // 不可視オフ
        cAttr.Bits.Hide = 0;
        break;
      case 39:
        // 前景色をデフォルトに戻す
        cColor.Color.Foreground = oColor.Color.Foreground;
        break;
      case 49:
        // 背景色をデフォルトに戻す
        cColor.Color.Background = oColor.Color.Background;
        break;
      default:
        if (v >= 30 && v < 38) {
          // 前景色
          cColor.Color.Foreground = v - 30;
        } else if (v >= 40 && v < 48) {
          // 背景色
          cColor.Color.Background = v - 40;
        }
        break;
    }
  }
}

// DSR (Device Status Report): 端末状態のリポート
void deviceStatusReport(uint8_t m) {
  switch (m) {
    case 5:
      Serial3.print("\e[0n");      // 0 Ready, No malfunctions detected (default) (DSR)
      break;
    case 6:
      cursorPositionReport(XP, YP); // CPR (Cursor Position Report)
      break;
  }
}

// DECLL (Load LEDS): LED の設定
void loadLEDs(uint8_t m) {
  switch (m) {
    case 0:
      // すべての LED をオフ
      digitalWrite(LED_01, LOW);
      digitalWrite(LED_02, LOW);
      digitalWrite(LED_03, LOW);
      digitalWrite(LED_04, LOW);
      break;
    case 1:
      // LED1 をオン
      digitalWrite(LED_01, HIGH);
      break;
    case 2:
      // LED2 をオン
      digitalWrite(LED_02, HIGH);
      break;
    case 3:
      // LED3 をオン
      digitalWrite(LED_03, HIGH);
      break;
    case 4:
      // LED4 をオン
      digitalWrite(LED_04, HIGH);
      break;
  }
}

// DECSTBM (Set Top and Bottom Margins): スクロール範囲をPt行からPb行に設定
void setTopAndBottomMargins(int16_t s, int16_t e) {
  if (e <= s) return;
  M_TOP    = s - 1;
  if (M_TOP > MAX_SC_Y) M_TOP = MAX_SC_Y;
  M_BOTTOM = e - 1;
  if (M_BOTTOM > MAX_SC_Y) M_BOTTOM = MAX_SC_Y;
  setCursorToHome();
}

// DECTST (Invoke Confidence Test): テスト診断を行う
void invokeConfidenceTests(uint8_t m) {
  nvic_sys_reset();
}

// "]" Operating System Command (OSC) シーケンス
// -----------------------------------------------------------------------------

// "#" Line Size Command  シーケンス
// -----------------------------------------------------------------------------

// DECDHL (Double Height Line): カーソル行を倍高、倍幅、トップハーフへ変更
void doubleHeightLine_TopHalf() {
  Serial.println("Unimplement: doubleHeightLine_TopHalf");
}

// DECDHL (Double Height Line): カーソル行を倍高、倍幅、ボトムハーフへ変更
void doubleHeightLine_BotomHalf() {
  Serial.println("Unimplement: doubleHeightLine_BotomHalf");
}

// DECSWL (Single-width Line): カーソル行を単高、単幅へ変更
void singleWidthLine() {
  Serial.println("Unimplement: singleWidthLine");
}

// DECDWL (Double-Width Line): カーソル行を単高、倍幅へ変更
void doubleWidthLine() {
  Serial.println("Unimplement: doubleWidthLine");
}

// DECALN (Screen Alignment Display): 画面を文字‘E’で埋める
void screenAlignmentDisplay() {
  tft.setAddrWindow(0, 0, MAX_SP_X, MAX_SP_Y);
  memset(screen, 0x45, SCSIZE);
  memset(attrib, 0x00, SCSIZE);
  memset(colors, oColor.value, SCSIZE);
  for (uint8_t y = 0; y < SC_H; y++)
    sc_updateLine(y);
}

// "(" G0 Sets Sequence
// -----------------------------------------------------------------------------

// G0 文字コードの設定
void setG0charset(char c) {
  Serial.println("Unimplement: setG0charset");
}

// "(" G1 Sets Sequence
// -----------------------------------------------------------------------------

// G1 文字コードの設定
void setG1charset(char c) {
  Serial.println("Unimplement: setG1charset");
}

// Unknown Sequence
// -----------------------------------------------------------------------------

// 不明なシーケンス
void unknownSequence(uint8_t m, char c) {
  String s = (m > 0) ? "[ESC]" : "";
  switch (m) {
    case 2:
      s = s + " [";
      break;
    case 3:
      s = s + " ]";
      break;
    case 4:
      s = s + " #";
      break;
    case 5:
      s = s + " (";
      break;
    case 6:
      s = s + " )";
      break;
    case 20:
      break;
    case 21:
      s = s + " [ ?";
      break;
  }
  Serial.print("Unknown: ");
  Serial.print(s);
  Serial.print(" ");
  Serial.print(c);
}

// -----------------------------------------------------------------------------

// タイマーハンドラ
void handle_timer() {
  canShowCursor = true;
}

void setup() {
  keyboard.begin(KBD_DAT, KBD_CLK);
  Serial.begin(115200);
  Serial3.begin(DEFAULT_BAUDRATE);

  // LED の初期化
  pinMode(LED_01, OUTPUT);
  pinMode(LED_02, OUTPUT);
  pinMode(LED_03, OUTPUT);
  pinMode(LED_04, OUTPUT);
  digitalWrite(LED_01, LOW);
  digitalWrite(LED_02, LOW);
  digitalWrite(LED_03, LOW);
  digitalWrite(LED_04, LOW);

  // TFT の初期化
  oColor.Color.Foreground = clWhite;
  oColor.Color.Background = clBlack;
  cColor.value = oColor.value;
  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(aColors[oColor.Color.Background]);
  fontTop = (uint8_t*)font6x8tt + 3;
  resetToInitialState();
  printString("\e[0;44m *** Terminal Init *** \e[0m\n");
  setCursorToHome();

  // カーソル用タイマーの設定
  Timer3.pause();
  Timer3.setPrescaleFactor(7200);
  Timer3.setOverflow(2500); // 250ms
  Timer3.attachInterrupt(TIMER_UPDATE_INTERRUPT, handle_timer);
  Timer3.setCount(0);
  Timer3.refresh();
  Timer3.resume();
}

void loop() {
  bool needCursorUpdate = false;

  // キーボード入力処理 (通信相手への出力)
  if (keyboard.available()) {
    char c = keyboard.read();
    switch (c) {
      case PS2_UPARROW:
        Serial3.print("\e[A");
        break;
      case PS2_DOWNARROW:
        Serial3.print("\e[B");
        break;
      case PS2_RIGHTARROW:
        Serial3.print("\e[C");
        break;
      case PS2_LEFTARROW:
        Serial3.print("\e[D");
        break;
      case PS2_F1:
        Serial3.print("\e[P");
        break;
      case PS2_F2:
        Serial3.print("\e[Q");
        break;
      case PS2_F3:
        Serial3.print("\e[R");
        break;
      case PS2_F4:
        Serial3.print("\e[S");
        break;
      default:
        Serial3.print(c);
        break;
    }
    needCursorUpdate = c;
  }

  // カーソル表示処理
  if (canShowCursor || needCursorUpdate)
    dispCursor(needCursorUpdate);

  // シリアル入力処理 (通信相手からの入力)
  while (Serial3.available()) {
    char c = Serial3.read();
    switch (c) {
      case 0x07:
        tone(SPK_PIN, 4000, 583);
        break;
      default:
        printChar(c);
        break;
    }
  }
}
