# VT100 Terminal Emulator for Arduino STM32
Arduino STM32 用の VT100 エミュレータです。

![image](https://user-images.githubusercontent.com/14885863/48667436-be584b00-eb18-11e8-9793-de8a9e4e1c98.png)

## 必要なハードウェア
以下のハードウェアが必要です。

 - Blue Pill (STM32F103 minimum development board)
 - ILI9341 TFT LCD Display (SPI)
 - PS/2 Keyboard Connector
 - Passive Buzzer (Option)
 - LED (Option)

**See also:**
 - [Blue Pill (wiki.stm32duino.com)](https://wiki.stm32duino.com/index.php?title=Blue_Pill)

### 接続
接続は以下のようになります。

![image](https://user-images.githubusercontent.com/14885863/48667439-e0ea6400-eb18-11e8-8a12-b14a269898ec.png)

#### ILI9341 TFT LCD Display
SPI 接続です。

![image](https://user-images.githubusercontent.com/14885863/48667493-ff049400-eb19-11e8-814d-52d6b3746fc3.png)

| TFT LCD | Blue Pill (STM32) |
|:--------|:------------------|
| 1 VCC| 3.3V |
| 2 GND| GND |
| 3 CS| PA4|
| 4 RESET| PA3|
| 5 DC| PA2|
| 6 MOSI| PA7|
| 7 SCK| PA5|
| 8 LED| 3.3V |
| 9 MISO| PA6|


#### PS/2 Keyboard Connector
ピンを移動させる場合には 5V トレラントのピンを選んでください。CLK と DAT ピンは 10kΩ でプルアップする必要があります。

![image](https://user-images.githubusercontent.com/14885863/48667483-db414e00-eb19-11e8-81c2-ea4701579dba.png)

| PS/2 | Blue Pill (STM32) |
|:--------|:------------------|
| 1 CLK| PB6 |
| 2 DAT| PB7 |
| 3 VCC| 5V|
| 4 GND| GND|


#### Passive Buzzer
PA8 に接続します。


#### LED
PB15 ～ PB12 に接続します、電流制限抵抗をお忘れなく。

## 必要なソフトウェア
コンパイルするには以下のソフトウェアが必要です。

 - [Arduino IDE (arduino.cc)](https://www.arduino.cc/en/Main/Software)
 - [Arduino STM32 (rogerclarkmelbourne@GitHub)](https://github.com/rogerclarkmelbourne/Arduino_STM32)
 - [Arduino_PS2Keyboard (Tamakichi@GitHub)](https://github.com/Tamakichi/Arduino_PS2Keyboard)
 - [[Option] TTVoutfonts (Tamakichi@GitHub)](https://github.com/Tamakichi/ttbasic_arduino_stm32/tree/master/libraries/TTVoutfonts) - 視認性のいい 6x8 フォントです。
 
 転送はブートローダーでもシリアルでも ST-Link でも構いません。

**See also:**
 - [STM32F103 (ht-deko.com)](https://ht-deko.com/arduino/stm32f103c8t6.html)

### シリアルバッファのサイズ調整
Arduino STM32 のデフォルトである 64 だと 9600bps で描画が追い付かない事があるためシリアルバッファを増やします (usart.h)。

```cpp:usart.h
/*
 * Devices
 */

#ifndef USART_RX_BUF_SIZE
#define USART_RX_BUF_SIZE               512
#endif

#ifndef USART_TX_BUF_SIZE
#define USART_TX_BUF_SIZE               128
#endif
```

 - [Serial buffer (STM32duino.com)](https://www.stm32duino.com/viewtopic.php?f=3&t=1070&sid=ffd27e6413dc3c4009afedd3df84f569&start=10#p24957)

### Ctrl キー対応
Ctrl キーに対応させるため、Arduino_PS2Keyboard の PS2Keyboard.cpp を編集します。

```cpp
#define BREAK     0x01
#define MODIFIER  0x02
#define SHIFT_L   0x04
#define SHIFT_R   0x08
#define ALTGR     0x10
/* 追加 BEGIN */
#define ALT       0x20
#define CTRL      0x40
/* 追加 END */

static char get_iso8859_code(void)
{
  static uint8_t state = 0;
  static uint8_t sState = 0;　// 追加
  uint8_t s;
  char c;

  while (1) {
    s = get_scan_code();
    if (!s) return 0;
    if (s == 0xF0) {
      state |= BREAK;
    } else if (s == 0xE0) {
      state |= MODIFIER;
    } else {
      if (state & BREAK) {
        if (s == 0x12) {
          state &= ~SHIFT_L;
        } else if (s == 0x59) {
          state &= ~SHIFT_R;
        } else if (s == 0x11 && (state & MODIFIER)) {
          state &= ~ALTGR;
        }
        // CTRL, ALT & WIN keys could be added
        // but is that really worth the overhead?
        state &= ~(BREAK | MODIFIER);
        continue;
      }
      if (s == 0x12) {
        state |= SHIFT_L;
        sState |= SHIFT_L; // 追加
        continue;
      /* 追加 BEGIN */
      } else if (s == 0x11) {
        sState |= ALT;
        continue;
      } else if (s == 0x14) {
        sState |= CTRL;
        continue;
      /* 追加 END */
      } else if (s == 0x59) {
        state |= SHIFT_R;
        sState |= SHIFT_R; // 追加
        continue;
      } else if (s == 0x11 && (state & MODIFIER)) {
        state |= ALTGR;
        sState |= ALTGR; // 追加
      }
      c = 0;
      if (state & MODIFIER) {
        switch (s) {
//        case 0x70: c = PS2_INSERT;      break;
//        case 0x6C: c = PS2_HOME;        break;
//        case 0x7D: c = PS2_PAGEUP;      break;
          case 0x71: c = PS2_DELETE;      break;
//        case 0x69: c = PS2_END;         break;
//        case 0x7A: c = PS2_PAGEDOWN;    break;
          case 0x75: c = PS2_UPARROW;     break;
          case 0x6B: c = PS2_LEFTARROW;   break;
          case 0x72: c = PS2_DOWNARROW;   break;
          case 0x74: c = PS2_RIGHTARROW;  break;
          case 0x4A: c = '/';             break;
          case 0x5A: c = PS2_ENTER;       break;
          default: break;
        }
      } else if (state & ALTGR) {
        // [右ALT]+[カナ/かな]キーが押された
        if (s == 0x13) {
          kana_f = ~kana_f ;
          if (kana_f == 0) {
            keymap = &PS2Keymap_JP ;      // 日本語キーマップに戻す
          } else {
            keymap = &PS2Keymap_KANA ;    // カタカナキーマップにする
          }
          c = PS2_KANA ; // この行をコメントにすればキーコードは返りません
        }
      } else if (state & (SHIFT_L | SHIFT_R)) {
        if (s < PS2_KEYMAP_SIZE)
          c = pgm_read_byte(keymap->shift + s);
      } else {
        if (s < PS2_KEYMAP_SIZE)
          c = pgm_read_byte(keymap->noshift + s);
      }
      state &= ~(BREAK | MODIFIER);
      if (c) {
        /* 追加 BEGIN */
        if (sState & 0x40) {
          c &= 0x1F;
          sState = 0;  
        }
        /* 追加 END */
        return c;
      }
    }
  }
}
```

 - [キャレット記法 (Wikipedia)](https://ja.wikipedia.org/wiki/%E3%82%AD%E3%83%A3%E3%83%AC%E3%83%83%E3%83%88%E8%A8%98%E6%B3%95)
 - [制御コードの入力 (通信用語の基礎知識)](https://www.wdic.org/w/TECH/Ctrl%E3%82%AD%E3%83%BC#xE5x88xB6xE5xBExA1xE3x82xB3xE3x83xBCxE3x83x89xE3x81xAExE5x85xA5xE5x8Ax9B)
 - [Delphi で ^ は "べき乗" の演算子ではないけれど？ (DEKO のざつだん)](https://ht-deko.com/ft1210.html#121019)
 
 ## 使い方
 キーボードを接続し、Blue Pill (STM32) と通信相手をつなぎます。

| COM | Blue Pill (STM32) |
|:--------|:------------------|
| TXD| PB11 (Serial3: RXD)|
| RXD| PB10 (Serial3: TXD) |
| GND| GND|

[RunCPM](https://github.com/MockbaTheBorg/RunCPM) や [CP/M 8266](https://github.com/SmallRoomLabs/cpm8266) と接続してみました。

 ![image](https://user-images.githubusercontent.com/14885863/48667172-fc06a500-eb13-11e8-8e5b-0273c8d8047d.png)
 
※ この VT100 エミュレータの通信速度は 9600 bps、画面サイズは 53 x 30 です。 

## VT100 の参考資料

![image](https://user-images.githubusercontent.com/14885863/48684164-c9cc7480-ebf3-11e8-92b3-a85c321d1771.png)

VT100 のエスケープシーケンスは以下のサイトを参考にしました。

 - [VT100のエスケープシーケンス (BK class)](http://bkclass.web.fc2.com/doc_vt100.html)
 - [対応制御シーケンス - Tera Term ヘルプ 目次 (Tera Term Home Page)](https://ttssh2.osdn.jp/manual/ja/about/ctrlseq.html)
 - [Chapter 3 Programmer Information - VT100 User Guide (VT100.net)](https://vt100.net/docs/vt100-ug/chapter3.html)
 - [ANSI/VT100 Terminal Control Escape Sequences (termsys.demon.co.uk)](http://www.termsys.demon.co.uk/vtansi.htm)
 - [VT100 escape codes](https://www.csie.ntu.edu.tw/~r92094/c++/VT100.html)
 - [ANSI Escape sequences - VT100 / VT52](http://ascii-table.com/ansi-escape-sequences-vt-100.php)

## コーディング上の参考資料
~~パク~~...インスパイア元です。
 - [Minimalistic STM32 and ILI9341 based terminal (GitHub)](https://github.com/cbm80amiga/STM32_TFT22_terminal_RRE)
 - [次はSTM32ボードを積極的に使ていきたい（26） グラフィック液晶（7) (猫にコ・ン・バ・ン・ワ)](http://nuneno.cocolog-nifty.com/blog/2018/07/stm3226-7-3b99.html)
 
