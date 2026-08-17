# solide::input - EC11 encoder + button (+ solide::menu)

A 1 kHz poll task feeds the host-tested quadrature decoder + button debounce and
queues events; drain them from your loop. The portable `solide::menu` FSM turns
events into a knob-driven menu.

## API
```cpp
namespace solide::input {
  enum class Event { None, RotateCW, RotateCCW, Click, LongPress };
  bool begin();
  bool pop(Event& e);   // non-blocking
  bool pressed();       // live debounced switch state (for hold-to-talk)
  bool taskAlive();
  // portable (also host-tested): class QuadDecoder, class Button
}
namespace solide::menu {
  enum class MenuAction { None, Record, SetWifi, ShowVersion, ShowLogs, SetIdle, FactoryReset, Restart };
  struct MenuView { bool visible; std::string title; std::vector<std::string> items; int selected; };
  class Menu { void onRotate(int); void onClick(); void onLongPress(); void promptWifiReset();
               MenuView view() const; MenuAction takeAction(); bool isOpen() const; };
}
```

## Example
`examples/99_combined_demo` (knob drives LED brightness + scheme). Feed
`input::Event`s into a `menu::Menu` and render `menu.view()` with `display::requestMenu`.

## Limitations
- 4 quarter-steps per detent (EC11 standard). Long-press default 400 ms.
- SW is on GPIO48 (the board's on-board RGB pin, repurposed). Event queue holds 16 -
  drain it each loop or events drop.
