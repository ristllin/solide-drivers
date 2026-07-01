#include "solide/menu.h"

namespace solide::menu {

static const char* kMain[] = {
    "Record audio", "Set Wi-Fi", "Versions", "Logs", "Idle timeout",
    "Factory reset", "Restart", "Close"};
static const int kMainN = 8;

static int wrap(int v, int n) { return ((v % n) + n) % n; }

void Menu::open() {
  if (state_ == State::Closed) {
    state_ = State::Main;
    mainSel_ = 0;
  }
}

void Menu::close() {
  state_ = State::Closed;
  confirmSel_ = 0;
}

void Menu::onLongPress() { close(); }

void Menu::promptWifiReset() {
  state_ = State::ConfirmWifi;
  confirmSel_ = 0;             // default "No"
  pending_ = MenuAction::None;
}

void Menu::onRotate(int dir) {
  if (dir == 0) return;
  int step = dir > 0 ? 1 : -1;
  if (state_ == State::Main) {
    mainSel_ = wrap(mainSel_ + step, kMainN);
  } else if (state_ == State::ConfirmReset || state_ == State::ConfirmRestart ||
             state_ == State::ConfirmWifi) {
    confirmSel_ = wrap(confirmSel_ + step, 2);
  }
}

void Menu::onClick() {
  switch (state_) {
    case State::Closed:
      open();
      break;
    case State::Main:
      switch (mainSel_) {
        case 0: pending_ = MenuAction::Record;      close(); break;
        case 1: state_ = State::ConfirmWifi;    confirmSel_ = 0; break;
        case 2: pending_ = MenuAction::ShowVersion; close(); break;
        case 3: pending_ = MenuAction::ShowLogs;    close(); break;
        // Idle timeout advances to the next preset (no confirm); the glue shows
        // a brief toast confirming the new value.
        case 4: pending_ = MenuAction::SetIdle;     close(); break;
        case 5: state_ = State::ConfirmReset;   confirmSel_ = 0; break;
        case 6: state_ = State::ConfirmRestart; confirmSel_ = 0; break;
        case 7: close(); break;
      }
      break;
    case State::ConfirmReset:
      if (confirmSel_ == 1) { pending_ = MenuAction::FactoryReset; close(); }
      else { state_ = State::Main; }
      break;
    case State::ConfirmRestart:
      if (confirmSel_ == 1) { pending_ = MenuAction::Restart; close(); }
      else { state_ = State::Main; }
      break;
    case State::ConfirmWifi:
      if (confirmSel_ == 1) { pending_ = MenuAction::SetWifi; close(); }
      else { state_ = State::Main; }
      break;
  }
}

MenuView Menu::view() const {
  MenuView v;
  if (state_ == State::Closed) return v;  // visible=false
  v.visible = true;
  if (state_ == State::Main) {
    v.title = "Menu";
    for (int i = 0; i < kMainN; i++) v.items.emplace_back(kMain[i]);
    v.selected = mainSel_;
  } else {
    if (state_ == State::ConfirmReset)        v.title = "Factory reset?";
    else if (state_ == State::ConfirmRestart) v.title = "Restart?";
    else                                      v.title = "Reset Wi-Fi?";
    v.items = {"No", "Yes"};
    v.selected = confirmSel_;
  }
  return v;
}

MenuAction Menu::takeAction() {
  MenuAction a = pending_;
  pending_ = MenuAction::None;
  return a;
}

}  // namespace solide::menu
