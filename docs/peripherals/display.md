# solide::display — e-paper

WeAct 2.9" 3-colour (SSD1680), on a dedicated HSPI bus. All rendering runs on a
render task (callers enqueue and never block). Two paths on one panel: fast B/W
(~2.2 s, custom WS_20_30 LUT) for interactive UI, and 3-colour (~18.5 s, OTP) for
stable art.

## API
```cpp
bool begin();
bool taskAlive();
void requestText(const String& title, const String& body,
                 int scrollOffset = 0, bool scrollMode = false);   // bold title + wrapped body
int  maxTextScroll();                                              // clamp the encoder
void requestMenu(const solide::menu::MenuView& view, bool full = false);
void requestBitmap(const uint8_t* black, const uint8_t* red, int16_t w, int16_t h, bool fast);
void showArt(int state, bool fast);                                // solide::art::State mascot
void clear();
```

## Example
`examples/02_display_hello` (text + mascot), `examples/04_display` (text/menu/art cycle).

## Limitations
- The GFX font is **ASCII-only** — non-ASCII bytes are dropped (so LLM/agent output
  can't index past the font table and crash).
- 3-colour full refresh is **~18.5 s**; use it only for idle/sleep/art. Interactive UI
  should stay on `requestText`/`requestMenu` (fast B/W).
- Partial-refresh menus accumulate ghosting; a full refresh runs every 10th frame.
- Hardware-only: no branding/status semantics here — compose a status screen from
  `requestText`/`requestBitmap` in your app.
