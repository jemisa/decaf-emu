#include "snd_core.h"
#include "snd_core_mix.h"

namespace snd_core
{

void
MIXUpdateSettings()
{
}

void
Module::registerMixFunctions()
{
   RegisterKernelFunction(MIXUpdateSettings);
}

} // namespace snd_core
