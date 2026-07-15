#include "../Config.h"

#include <cassert>
#include <limits>

int run_config_validation_tests() {
  Config::SpamDelayMs.store(-1);
  Config::TurboJumpDurationMs.store(50000);
  Config::TargetFPS.store(std::numeric_limits<double>::infinity());
  Config::KeySpamTrigger.store(999);
  Config::EnableSpam.store(true);
  Config::TurboLootKey.store('C');
  Config::EnableTurboLoot.store(true);
  Config::SuperglideBind.store(0);
  Config::EnableSuperglide.store(false);

  Config::ValidateConfig();

  assert(Config::SpamDelayMs.load() == 0);
  assert(Config::TurboJumpDurationMs.load() == 1000);
  assert(Config::TargetFPS.load() == 60.0);
  assert(Config::KeySpamTrigger.load() == 'C');
  assert(!Config::EnableTurboLoot.load());
  assert(Config::SuperglideBind.load() == 0);
  return 0;
}