#include <functional>
#include <cereal/cereal.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/string.hpp>
#include <sstream>
#include <atomic>
#include <iostream>
#include "debugger.h"
#include "log.h"
#include "processor.h"
#include "debugmsg.h"
#include "debugnet.h"
#include "debugcontrol.h"

static const bool FORCE_DEBUGGER_ON = false;

Debugger
gDebugger;

Debugger::Debugger()
   : mEnabled(false)
{
}

void
Debugger::debugThread()
{
   printf("Debugger Thread Started");

   while (true) {
      std::unique_lock<std::mutex> lock{ mMsgLock };
      if (mMsgQueue.empty()) {
         mMsgCond.wait(lock);
         continue;
      }

      DebugMessage *msg = mMsgQueue.front();
      mMsgQueue.pop();
      lock.unlock();

      handleMessage(msg);
      delete msg;
   }
}

void
Debugger::handleMessage(DebugMessage *msg)
{
   gLog->debug("Handling message {}", (int)msg->type());

   switch (msg->type()) {
   case DebugMessageType::DebuggerDc: {
      gDebugControl.pauseAll();
      gLog->debug("Debugger disconnected, game has been paused.");
      break;
   }

   case DebugMessageType::PreLaunch: {
      gDebugControl.waitForAllPaused();
      
      gLog->debug("Prelaunch Occured");
      gDebugNet.writePrelaunch();
      break;
   }
   case DebugMessageType::BpHit: {
      auto bpMsg = reinterpret_cast<DebugMessageBpHit*>(msg);

      gDebugControl.waitForAllPaused();

      gLog->debug("Breakpoint Hit on Core #{}", bpMsg->coreId);
      gDebugNet.writeBreakpointHit(bpMsg->coreId, bpMsg->userData);

      break;
   }
   }
}

void
Debugger::initialise()
{
   if (gDebugNet.connect("127.0.0.1", 11234)) {
      // Debugging is connected!
      mEnabled = true;
   } else if (FORCE_DEBUGGER_ON) {
      mEnabled = true;
   } else {
      mEnabled = false;
   }

   if (mEnabled) {
      mDebuggerThread = std::thread(&Debugger::debugThread, this);
   }
}

bool
Debugger::isEnabled() const
{
   return mEnabled;
}

void
Debugger::notify(DebugMessage *msg)
{
   assert(mEnabled);

   std::unique_lock<std::mutex> lock{ mMsgLock };
   mMsgQueue.push(msg);
   mMsgCond.notify_all();
}

void
Debugger::pause()
{
   gDebugControl.pauseAll();
   gDebugControl.waitForAllPaused();
}

void
Debugger::resume()
{
   gDebugControl.resumeAll();
}

void
Debugger::addBreakpoint(uint32_t addr, uint32_t userData)
{
   assert(mEnabled);

   while (true) {
      BreakpointList oldList = mBreakpoints;
      BreakpointList newList(new BreakpointListType(*oldList));
      newList->emplace(addr, userData);

      if (!std::atomic_compare_exchange_weak(&mBreakpoints, &oldList, newList)) {
         // Keep trying until success!
         continue;
      }
   }
}

void
Debugger::removeBreakpoint(uint32_t addr)
{
   assert(mEnabled);

   while (true) {
      BreakpointList oldList = mBreakpoints;
      BreakpointList newList(new BreakpointListType(*oldList));
      auto bpitr = newList->find(addr);
      if (bpitr != newList->end()) {
         newList->erase(bpitr);
      }

      if (!std::atomic_compare_exchange_weak(&mBreakpoints, &oldList, newList)) {
         // Keep trying until success!
         continue;
      }
   }
}


