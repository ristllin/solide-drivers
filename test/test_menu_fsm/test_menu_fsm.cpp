// Host unit tests for the solide::menu FSM (pure logic).
#include <unity.h>
#include "solide/menu.h"

using namespace solide::menu;

void setUp() {}
void tearDown() {}

static void test_menu_open_close() {
  Menu m;
  TEST_ASSERT_FALSE(m.isOpen());
  TEST_ASSERT_FALSE(m.view().visible);
  m.onClick();                                    // closed -> open (Main)
  TEST_ASSERT_TRUE(m.isOpen());
  MenuView v = m.view();
  TEST_ASSERT_TRUE(v.visible);
  TEST_ASSERT_EQUAL_STRING("Menu", v.title.c_str());
  TEST_ASSERT_EQUAL_INT(0, v.selected);
  m.onLongPress();                                // close from anywhere
  TEST_ASSERT_FALSE(m.isOpen());
}

static void test_menu_rotate_wraps() {
  Menu m; m.onClick();                            // Main has 8 items
  m.onRotate(-1);                                 // wrap up from 0 -> 7
  TEST_ASSERT_EQUAL_INT(7, m.view().selected);
  m.onRotate(+1);                                 // -> 0
  TEST_ASSERT_EQUAL_INT(0, m.view().selected);
}

static void test_menu_record_action() {
  Menu m; m.onClick();                            // Main, sel=0 (Record audio)
  m.onClick();                                    // select -> action + close
  TEST_ASSERT_FALSE(m.isOpen());
  TEST_ASSERT_TRUE(m.takeAction() == MenuAction::Record);
  TEST_ASSERT_TRUE(m.takeAction() == MenuAction::None);   // pending cleared
}

static void test_menu_factory_reset_confirm() {
  Menu m; m.onClick();                            // Main
  for (int i = 0; i < 5; i++) m.onRotate(+1);     // sel=5 (Factory reset)
  TEST_ASSERT_EQUAL_INT(5, m.view().selected);
  m.onClick();                                    // -> ConfirmReset (No/Yes, sel=0)
  MenuView v = m.view();
  TEST_ASSERT_EQUAL_STRING("Factory reset?", v.title.c_str());
  TEST_ASSERT_EQUAL_INT(2, (int)v.items.size());
  m.onClick();                                    // sel=0 (No) -> back to Main, no action
  TEST_ASSERT_TRUE(m.takeAction() == MenuAction::None);
  TEST_ASSERT_TRUE(m.isOpen());
  m.onClick();                                    // Main sel still 5 -> ConfirmReset
  m.onRotate(+1);                                 // sel=1 (Yes)
  m.onClick();                                    // confirm -> FactoryReset + close
  TEST_ASSERT_FALSE(m.isOpen());
  TEST_ASSERT_TRUE(m.takeAction() == MenuAction::FactoryReset);
}

static void test_menu_wifi_prompt_shortcut() {
  Menu m;
  m.promptWifiReset();                            // BOOT-button shortcut -> ConfirmWifi
  TEST_ASSERT_TRUE(m.isOpen());
  TEST_ASSERT_EQUAL_STRING("Reset Wi-Fi?", m.view().title.c_str());
  m.onRotate(+1);                                 // Yes
  m.onClick();
  TEST_ASSERT_TRUE(m.takeAction() == MenuAction::SetWifi);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_menu_open_close);
  RUN_TEST(test_menu_rotate_wraps);
  RUN_TEST(test_menu_record_action);
  RUN_TEST(test_menu_factory_reset_confirm);
  RUN_TEST(test_menu_wifi_prompt_shortcut);
  return UNITY_END();
}
