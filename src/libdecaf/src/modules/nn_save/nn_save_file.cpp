#include "nn_save.h"
#include "nn_save_dir.h"
#include "nn_save_file.h"
#include "modules/coreinit/coreinit_fs_dir.h"
#include "modules/coreinit/coreinit_fs_file.h"
#include "modules/coreinit/coreinit_fs_stat.h"

namespace nn
{

namespace save
{

FSStatus
SAVEOpenFile(FSClient *client,
             FSCmdBlock *block,
             uint8_t account,
             const char *path,
             const char *mode,
             be_val<FSFileHandle> *handle,
             uint32_t flags)
{
   auto fsPath = internal::getSavePath(account, path);
   return FSOpenFile(client, block, fsPath.path().c_str(), mode, handle, flags);
}

FSStatus
SAVEOpenFileAsync(FSClient *client,
                  FSCmdBlock *block,
                  uint8_t account,
                  const char *path,
                  const char *mode,
                  be_val<FSFileHandle> *handle,
                  uint32_t flags,
                  FSAsyncData *asyncData)
{
   auto fsPath = internal::getSavePath(account, path);
   return FSOpenFileAsync(client, block, fsPath.path().c_str(), mode, handle, flags, asyncData);
}

FSStatus
SAVERemoveAsync(FSClient *client,
                FSCmdBlock *block,
                uint8_t account,
                const char *path,
                uint32_t flags,
                FSAsyncData *asyncData)
{
   auto fsPath = internal::getSavePath(account, path);
   return FSRemoveAsync(client, block, fsPath.path().c_str(), flags, asyncData);
}

FSStatus
SAVERemove(FSClient *client,
           FSCmdBlock *block,
           uint8_t account,
           const char *path,
           uint32_t flags)
{
   auto fsPath = internal::getSavePath(account, path);
   return FSRemove(client, block, fsPath.path().c_str(), flags);
}

FSStatus
SAVEGetStatAsync(FSClient *client,
                 FSCmdBlock *block,
                 uint8_t account,
                 const char *path,
                 FSStat *stat,
                 uint32_t flags,
                 FSAsyncData *asyncData)
{
   auto fsPath = internal::getSavePath(account, path);
   return FSGetStatAsync(client, block, fsPath.path().c_str(), stat, flags, asyncData);
}

FSStatus
SAVEGetStat(FSClient *client,
            FSCmdBlock *block,
            uint8_t account,
            const char *path,
            FSStat *stat,
            uint32_t flags)
{
   auto fsPath = internal::getSavePath(account, path);
   return FSGetStat(client, block, fsPath.path().c_str(), stat, flags);
}

SaveStatus
SAVEChangeGroupAndOthersMode(FSClient *client,
                             FSCmdBlock *block,
                             uint8_t account,
                             const char *path,
                             uint32_t mode,
                             uint32_t flags)
{
   // TODO: SAVEChangeGroupAndOthersMode
   return SaveStatus::OK;
}

void
Module::registerFileFunctions()
{
   RegisterKernelFunction(SAVEOpenFile);
   RegisterKernelFunction(SAVEOpenFileAsync);
   RegisterKernelFunction(SAVERemoveAsync);
   RegisterKernelFunction(SAVERemove);
   RegisterKernelFunction(SAVEGetStatAsync);
   RegisterKernelFunction(SAVEGetStat);
   RegisterKernelFunction(SAVEChangeGroupAndOthersMode);
}

} // namespace save

} // namespace nn
