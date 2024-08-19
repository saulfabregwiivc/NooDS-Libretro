#ifndef GAMEDB_H
#define GAMEDB_H

#include <cstdint>
#include <string>

#include "../cartridge.h"

struct GameInfo {
  uint32_t id = 0;
  uint32_t saveType = 0;
  uint32_t saveSize = 0;
};

namespace GameDB
{
  GameInfo analyze(const uint32_t& romCode);
}

#ifdef __LIBRETRO__
void CartridgeNds::detectSaveSize()
{
  GameInfo info = GameDB::analyze(romCode);
  if (info.saveSize) resizeSave(info.saveSize, false);
}
#endif

#endif // GAMEDB_H
