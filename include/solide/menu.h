#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Portable, host-testable knob-driven menu state machine. No Arduino/display
// deps. The display module reads MenuView and renders it (fast B/W); the glue
// feeds encoder events and performs the returned MenuAction.
namespace solide::menu {

enum class MenuAction : uint8_t {
  None, Record, SetWifi, ShowVersion, ShowLogs, SetIdle, FactoryReset, Restart
};

// What the screen should draw. When !visible, the normal status/chat UI shows.
struct MenuView {
  bool visible = false;
  std::string title;
  std::vector<std::string> items;
  int selected = 0;
};

class Menu {
 public:
  Menu() = default;
  bool isOpen() const { return state_ != State::Closed; }
  void open();
  void close();
  void onRotate(int dir);   // +1 / -1 (already debounced to one unit per detent)
  void onClick();           // closed -> open; in-menu -> select / confirm
  void onLongPress();       // closes the menu from anywhere
  void promptWifiReset();   // jump straight to the Wi-Fi reset confirm (BOOT button)
  MenuView view() const;
  MenuAction takeAction();  // returns + clears a pending terminal action

 private:
  enum class State : uint8_t { Closed, Main, ConfirmReset, ConfirmRestart, ConfirmWifi };
  State state_ = State::Closed;
  int mainSel_ = 0;
  int confirmSel_ = 0;       // 0 = No, 1 = Yes
  MenuAction pending_ = MenuAction::None;
};

}  // namespace solide::menu
