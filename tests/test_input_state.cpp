#include "../Config.h"
#include "../EventDispatcher.h"
#include "../Globals.h"
#include "../KeybindManager.h"

#include <cassert>

namespace {
void ResetInputState() {
  KeybindManager::Initialize();
  Globals::g_bindingTarget.store(nullptr);
  Globals::g_isSpamActive.store(false);
  Globals::g_lurchState.store(0);
  for (auto &state : Globals::g_KeyInfo) {
    state.physicalKeyDown.store(false);
    state.spamming.store(false);
  }
  Config::EnableSpam.store(false);
  Config::EnableSnapTap.store(false);
  Config::EnableTurboLoot.store(false);
  Config::EnableTurboJump.store(false);
  Config::EnableSuperglide.store(false);
  Config::KeySpamTriggerToggleActive.store(false);
  Config::TurboLootToggleActive.store(false);
  Config::TurboJumpToggleActive.store(false);
}

void TestOppositeEdgesRemainAuthoritative() {
  ResetInputState();
  assert(!HandleFeatureKeyEvent('Q', true));
  assert(Globals::g_KeyInfo['Q'].physicalKeyDown.load());
  assert(!HandleFeatureKeyEvent('Q', false));
  assert(!Globals::g_KeyInfo['Q'].physicalKeyDown.load());
  assert(!HandleFeatureKeyEvent('Q', true));
  assert(Globals::g_KeyInfo['Q'].physicalKeyDown.load());
}

void TestReboundKeyWorksOnFirstPress() {
  ResetInputState();
  Globals::g_bindingTarget.store(&Config::SuperglideBind);
  assert(KeybindManager::HandleBind('F', true));
  assert(KeybindManager::GetLastBindResult() ==
         KeybindManager::BindResult::Success);
  assert(KeybindManager::HandleBind('F', false));
  assert(!Globals::g_KeyInfo['F'].physicalKeyDown.load());

  assert(KeybindManager::ProcessSuperglide('F', true));
  assert(!KeybindManager::ProcessSuperglide('F', false));
}

void TestDuplicateBindingReportsReason() {
  ResetInputState();
  Config::KeySpamTrigger.store('C');
  Config::TurboLootKey.store('E');
  Globals::g_bindingTarget.store(&Config::TurboLootKey);
  assert(KeybindManager::HandleBind('C', true));
  assert(KeybindManager::GetLastBindResult() ==
         KeybindManager::BindResult::Duplicate);
  assert(Config::TurboLootKey.load() == 'E');
}
} // namespace

int run_input_state_tests() {
  TestOppositeEdgesRemainAuthoritative();
  TestReboundKeyWorksOnFirstPress();
  TestDuplicateBindingReportsReason();
  return 0;
}