#include "snd_core.h"
#include "snd_core_core.h"
#include "snd_core_voice.h"
#include "decaf_sound.h"
#include "modules/coreinit/coreinit_alarm.h"
#include "modules/coreinit/coreinit_interrupts.h"
#include "modules/coreinit/coreinit_memheap.h"
#include "modules/coreinit/coreinit_scheduler.h"
#include "modules/coreinit/coreinit_systeminfo.h"
#include "modules/coreinit/coreinit_thread.h"
#include "modules/coreinit/coreinit_time.h"
#include "ppcutils/wfunc_ptr.h"
#include "ppcutils/wfunc_call.h"

namespace snd_core
{

static const size_t MaxFrameCallbacks = 64;

static BOOL
gAXInit = FALSE;

static int
sOutputRate;

static int
sOutputChannels;

static coreinit::OSThreadEntryPointFn
sFrameCallbackThreadEntryPoint;

static coreinit::AlarmCallback
sFrameAlarmHandler = nullptr;

static virtual_ptr<coreinit::OSAlarm>
sFrameAlarm;

static coreinit::OSThread *
sFrameCallbackThread;

static coreinit::OSThreadQueue *
sFrameCallbackThreadQueue;

static AXFrameCallback
sFrameCallbacks[MaxFrameCallbacks];

static std::atomic<int32_t>
sProtectLock = { 0 };

static std::array<int32_t, 6*144>  // Enough for 6 channels of 3ms at 48kHz
sMixBuffer;

static std::array<int16_t, 6*144>
sOutputBuffer;

void
AXInit()
{
   AXInitParams params;
   params.renderer = AXInitRenderer::Out32khz;
   params.pipeline = AXInitPipeline::Single;
   AXInitWithParams(&params);
}

void
AXInitWithParams(AXInitParams *params)
{
   if (AXIsInit()) {
      return;
   }

   switch (params->renderer) {
   case AXInitRenderer::Out32khz:
      sOutputRate = 32000;
      break;
   case AXInitRenderer::Out48khz:
      sOutputRate = 48000;
      break;
   default:
      decaf_abort(fmt::format("Unimplemented AXInitRenderer {}", params->renderer));
   }

   sOutputChannels = 2;  // TODO: surround support
   internal::initVoices();
   internal::initEvents();

   if (auto driver = decaf::getSoundDriver()) {
      if (!driver->start(sOutputRate, sOutputChannels)) {
         gLog->error("Sound driver failed to start, disabling sound output");
         decaf::setSoundDriver(nullptr);
      }
   }

   gAXInit = TRUE;
}

BOOL
AXIsInit()
{
   return gAXInit;
}

void
AXInitProfile(AXProfile *profile, uint32_t count)
{
   // TODO: AXInitProfile
}

uint32_t
AXGetSwapProfile(AXProfile *profile, uint32_t count)
{
   return 0;
}

AXResult
AXSetDefaultMixerSelect(uint32_t)
{
   // TODO: AXSetDefaultMixerSelect
   return AXResult::Success;
}

AXResult
AXSetAuxReturnVolume(uint32_t, uint32_t, uint32_t, uint16_t volume)
{
   // TODO: AXSetAuxReturnVolume
   return AXResult::Success;
}

AXResult
AXRegisterAppFrameCallback(AXFrameCallback callback)
{
   for (auto i = 0; i < MaxFrameCallbacks; ++i) {
      if (!sFrameCallbacks[i]) {
         sFrameCallbacks[i] = callback;
         return AXResult::Success;
      }
   }
   // TODO: Return the appropriate error here
   return AXResult::Success;
}

AXResult
AXRegisterFrameCallback(AXFrameCallback callback)
{
   // TODO: Maybe this is meant to be separate?
   return AXRegisterAppFrameCallback(callback);
}

int32_t
AXUserBegin()
{
   // TODO: Implement this properly
   return sProtectLock.fetch_add(1);
}

int32_t
AXUserEnd()
{
   // TODO: Implement this properly
   return sProtectLock.fetch_sub(1);
}

int32_t
AXVoiceBegin(AXVoice *voice)
{
   // TODO: Implement this properly
   return AXUserBegin();
}

int32_t
AXVoiceEnd(AXVoice *voice)
{
   // TODO: Implement this properly
   return AXUserEnd();
}

BOOL AXUserIsProtected()
{
   // TODO: Implement this properly
   return sProtectLock.load() > 0;
}

uint32_t
AXGetInputSamplesPerFrame()
{
   if (sOutputRate == 32000) {
      return 96;
   } else if (sOutputRate == 48000) {
      return 144;
   } else {
      decaf_abort(fmt::format("Unexpected output rate {}", sOutputRate));
   }
}

uint32_t
AXGetInputSamplesPerSec()
{
   return (AXGetInputSamplesPerFrame() / 3) * 1000;
}

int32_t
AXRmtGetSamplesLeft()
{
   // TODO: AXRmtGetSamplesLeft
   return 0;
}

int32_t
AXRmtGetSamples(int32_t, be_val<uint8_t> *buffer, int32_t samples)
{
   // TODO: AXRmtGetSamples
   return 0;
}

int32_t
AXRmtAdvancePtr(int32_t)
{
   // TODO: AXRmtAdvancePtr
   return 0;
}

uint32_t
FrameCallbackThreadEntry(uint32_t core_id, void *arg2)
{
   int numSamples = sOutputRate * 3 / 1000;

   while (true) {
      coreinit::internal::lockScheduler();
      coreinit::internal::sleepThreadNoLock(sFrameCallbackThreadQueue);
      coreinit::internal::rescheduleSelfNoLock();
      coreinit::internal::unlockScheduler();

      for (auto i = 0; i < MaxFrameCallbacks; ++i) {
         if (sFrameCallbacks[i]) {
            sFrameCallbacks[i]();
         }
      }

      decaf_check(static_cast<size_t>(numSamples * sOutputChannels) <= sMixBuffer.size());
      internal::mixOutput(&sMixBuffer[0], numSamples, sOutputChannels);

      // TODO: FinalMixCallback

      auto driver = decaf::getSoundDriver();

      if (driver) {
         for (int i = 0; i < numSamples * sOutputChannels; ++i) {
            sOutputBuffer[i] = static_cast<int16_t>(std::min(std::max(sMixBuffer[i], -32768), 32767));
         }

         driver->output(&sOutputBuffer[0], numSamples);
      }
   }

   return 0;
}

namespace internal
{

void
startFrameAlarmThread()
{
   using namespace coreinit;

   auto stackSize = 16 * 1024;
   auto stack = reinterpret_cast<uint8_t*>(coreinit::internal::sysAlloc(stackSize, 8));
   auto name = coreinit::internal::sysStrDup("AX Callback Thread");

   OSCreateThread(sFrameCallbackThread, sFrameCallbackThreadEntryPoint, 0, nullptr,
      reinterpret_cast<be_val<uint32_t>*>(stack + stackSize), stackSize, -1,
      static_cast<OSThreadAttributes>(1 << cpu::this_core::id()));
   OSSetThreadName(sFrameCallbackThread, name);
   OSResumeThread(sFrameCallbackThread);
}

void
frameAlarmHandler(coreinit::OSAlarm *alarm, coreinit::OSContext *context)
{
   coreinit::internal::lockScheduler();
   coreinit::internal::wakeupThreadNoLock(sFrameCallbackThreadQueue);
   coreinit::internal::unlockScheduler();
}

void
initEvents()
{
   using namespace coreinit;

   for (auto i = 0; i < MaxFrameCallbacks; ++i) {
      sFrameCallbacks[i] = nullptr;
   }

   startFrameAlarmThread();

   sFrameAlarm = coreinit::internal::sysAlloc<OSAlarm>();
   auto ticks = static_cast<OSTime>(OSGetSystemInfo()->busSpeed / 4) * 3 / 1000;
   OSCreateAlarm(sFrameAlarm);
   OSSetPeriodicAlarm(sFrameAlarm, OSGetTime(), ticks, sFrameAlarmHandler);
}


int
getOutputRate()
{
   return sOutputRate;
}

} // namespace internal

void
Module::registerCoreFunctions()
{
   RegisterKernelFunction(AXInit);
   RegisterKernelFunction(AXInitWithParams);
   RegisterKernelFunction(AXIsInit);
   RegisterKernelFunction(AXInitProfile);
   RegisterKernelFunction(AXGetSwapProfile);
   RegisterKernelFunction(AXSetDefaultMixerSelect);
   RegisterKernelFunction(AXSetAuxReturnVolume);
   RegisterKernelFunction(AXRegisterAppFrameCallback);
   RegisterKernelFunction(AXRegisterFrameCallback);
   RegisterKernelFunction(AXUserBegin);
   RegisterKernelFunction(AXUserEnd);
   RegisterKernelFunction(AXVoiceBegin);
   RegisterKernelFunction(AXVoiceEnd);
   RegisterKernelFunction(AXUserIsProtected);
   RegisterKernelFunction(AXGetInputSamplesPerFrame);
   RegisterKernelFunction(AXGetInputSamplesPerSec);
   RegisterKernelFunction(AXRmtGetSamples);
   RegisterKernelFunction(AXRmtGetSamplesLeft);
   RegisterKernelFunction(AXRmtAdvancePtr);
   RegisterKernelFunctionName("internal_FrameAlarmHandler", snd_core::internal::frameAlarmHandler);
   RegisterInternalFunction(FrameCallbackThreadEntry, sFrameCallbackThreadEntryPoint);
   RegisterInternalData(sFrameCallbackThreadQueue);
   RegisterInternalData(sFrameCallbackThread);
}

void
Module::initialiseCore()
{
   sFrameAlarmHandler = findExportAddress("internal_FrameAlarmHandler");
}

} // namespace snd_core
