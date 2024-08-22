#ifndef SAVESTATE_H
#define SAVESTATE_H

#include <cstdint>
#include <string>

#include "../core.h"

class SaveState
{
  public:
    SaveState(Core* core) : core(core) {}
    bool check(const void* data, size_t& size);
    bool save(void* data, size_t& size);
    bool load(const void* data, size_t& size);

  private:
    Core* core;
    static const char *stateTag;
    static const uint32_t stateVersion;
};

#endif // SAVESTATE_H
