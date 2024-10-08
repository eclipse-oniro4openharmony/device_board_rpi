/*
 * Copyright (c) 2021-2024 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "main_thread.h"

#include <malloc.h>
#include <new>
#include <regex>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>

#include "ability_manager_client.h"
#include "constants.h"
#include "ability_delegator.h"
#include "ability_delegator_registry.h"
#include "ability_loader.h"
#include "ability_thread.h"
#include "ability_util.h"
#include "app_loader.h"
#include "app_recovery.h"
#include "appfreeze_inner.h"
#include "appfreeze_state.h"
#include "application_data_manager.h"
#include "application_env_impl.h"
#include "bundle_mgr_proxy.h"
#include "hitrace_meter.h"
#include "child_main_thread.h"
#include "child_process_manager.h"
#include "configuration_convertor.h"
#include "common_event_manager.h"
#include "global_constant.h"
#include "context_deal.h"
#include "context_impl.h"
#include "dump_ffrt_helper.h"
#include "dump_ipc_helper.h"
#include "dump_runtime_helper.h"
#include "exit_reason.h"
#include "extension_ability_info.h"
#include "extension_module_loader.h"
#include "extension_plugin_info.h"
#include "extract_resource_manager.h"
#include "ffrt.h"
#include "file_path_utils.h"
#include "freeze_util.h"
#include "hilog_tag_wrapper.h"
#include "resource_config_helper.h"
#ifdef SUPPORT_GRAPHICS
#include "locale_config.h"
#include "ace_forward_compatibility.h"
#include "form_constants.h"
#include "include/private/EGL/cache.h"
#ifdef SUPPORT_APP_PREFERRED_LANGUAGE
#include "preferred_language.h"
#endif
#endif
#include "app_mgr_client.h"
#include "if_system_ability_manager.h"
#include "iservice_registry.h"
#include "js_runtime.h"
#ifdef CJ_FRONTEND
#include "cj_runtime.h"
#endif
#include "native_lib_util.h"
#include "nlohmann/json.hpp"
#include "ohos_application.h"
#include "overlay_module_info.h"
#include "parameters.h"
#include "resource_manager.h"
#include "runtime.h"
#include "sys_mgr_client.h"
#include "system_ability_definition.h"
#include "task_handler_client.h"
#include "time_util.h"
#include "uncaught_exception_callback.h"
#include "hisysevent.h"
#include "js_runtime_utils.h"
#include "context/application_context.h"
#include "os_account_manager_wrapper.h"

#if defined(NWEB)
#include <thread>
#include "app_mgr_client.h"
#include "nweb_helper.h"
#endif

#ifdef IMAGE_PURGEABLE_PIXELMAP
#include "purgeable_resource_manager.h"
#endif

#if defined(ABILITY_LIBRARY_LOADER) || defined(APPLICATION_LIBRARY_LOADER)
#include <dirent.h>
#include <dlfcn.h>
#endif
namespace OHOS {
using AbilityRuntime::FreezeUtil;
namespace AppExecFwk {
using namespace OHOS::AbilityBase::Constants;
std::weak_ptr<OHOSApplication> MainThread::applicationForDump_;
std::shared_ptr<MainThread::MainHandler> MainThread::mainHandler_ = nullptr;
const std::string PERFCMD_PROFILE = "profile";
const std::string PERFCMD_DUMPHEAP = "dumpheap";
namespace {
#ifdef APP_USE_ARM
constexpr char FORM_RENDER_LIB_PATH[] = "/system/lib/libformrender.z.so";
#elif defined(APP_USE_X86_64)
constexpr char FORM_RENDER_LIB_PATH[] = "/system/lib64/libformrender.z.so";
#else
constexpr char FORM_RENDER_LIB_PATH[] = "/system/lib64/libformrender.z.so";
#endif

constexpr int32_t DELIVERY_TIME = 200;
constexpr int32_t DISTRIBUTE_TIME = 100;
constexpr int32_t START_HIGH_SENSITIVE = 1;
constexpr int32_t EXIT_HIGH_SENSITIVE = 2;
constexpr int32_t UNSPECIFIED_USERID = -2;
constexpr int32_t JS_ERROR_EXIT = -2;
constexpr int32_t TIME_OUT = 120;
constexpr int32_t DEFAULT_SLEEP_TIME = 100000;

enum class SignalType {
    SIGNAL_JSHEAP_OLD,
    SIGNAL_JSHEAP,
    SIGNAL_JSHEAP_PRIV,
    SIGNAL_NO_TRIGGERID,
    SIGNAL_NO_TRIGGERID_PRIV,
    SIGNAL_FORCE_FULLGC,
};

constexpr char EVENT_KEY_PACKAGE_NAME[] = "PACKAGE_NAME";
constexpr char EVENT_KEY_VERSION[] = "VERSION";
constexpr char EVENT_KEY_TYPE[] = "TYPE";
constexpr char EVENT_KEY_HAPPEN_TIME[] = "HAPPEN_TIME";
constexpr char EVENT_KEY_REASON[] = "REASON";
constexpr char EVENT_KEY_JSVM[] = "JSVM";
constexpr char EVENT_KEY_SUMMARY[] = "SUMMARY";
constexpr char EVENT_KEY_APP_RUNING_UNIQUE_ID[] = "APP_RUNNING_UNIQUE_ID";
constexpr char DEVELOPER_MODE_STATE[] = "const.security.developermode.state";
constexpr char PRODUCT_ASSERT_FAULT_DIALOG_ENABLED[] = "persisit.sys.abilityms.support_assert_fault_dialog";
constexpr char KILL_REASON[] = "Kill Reason:Js Error";

const int32_t JSCRASH_TYPE = 3;
const std::string JSVM_TYPE = "ARK";
const std::string SIGNAL_HANDLER = "OS_SignalHandler";

constexpr uint32_t CHECK_MAIN_THREAD_IS_ALIVE = 1;

const std::string OVERLAY_STATE_CHANGED = "usual.event.OVERLAY_STATE_CHANGED";
const std::string JSON_KEY_APP_FONT_SIZE_SCALE = "fontSizeScale";
const std::string JSON_KEY_APP_FONT_MAX_SCALE = "fontSizeMaxScale";
const std::string JSON_KEY_APP_CONFIGURATION = "configuration";
const std::string DEFAULT_APP_FONT_SIZE_SCALE = "nonFollowSystem";
const std::string SYSTEM_DEFAULT_FONTSIZE_SCALE = "1.0";
const int32_t TYPE_RESERVE = 1;
const int32_t TYPE_OTHERS = 2;

extern "C" int DFX_SetAppRunningUniqueId(const char* appRunningId, size_t len) __attribute__((weak));

static bool initStatus;
static std::string cacheDir;
static std::string ddkcacheDir;

void SetCacheDir(const std::string dir)
{
    struct stat dirStat;
    if (stat(dir.c_str(), &dirStat) != 0) {
        TAG_LOGI(AAFwkTag::APPKIT, "inputdir not Create");
        initStatus = false;
        return;
    }

    struct stat cachedirstat;
    struct stat ddkdirstat;
    cacheDir = dir + std::string("/blobShader");
    ddkcacheDir = dir + std::string("/ddkShader");

    if (stat(cacheDir.c_str(), &cachedirstat) != 0) {
        initStatus = false;
        int ret = mkdir(cacheDir.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH);
        if (ret == -1) {
            initStatus = false;
            TAG_LOGI(AAFwkTag::APPKIT, "cacheDir not Create");
        } else {
            initStatus = true;
        }
    } else {
        initStatus = true;
    }

    if (stat(ddkcacheDir.c_str(), &ddkdirstat) != 0) {
        int ret = mkdir(ddkcacheDir.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH);
        if (ret == -1 && errno != EEXIST) {
           TAG_LOGI(AAFwkTag::APPKIT, "ddkcacheDir not Create");
        }
    }
}

} // namespace

void MainThread::GetNativeLibPath(const BundleInfo &bundleInfo, const HspList &hspList, AppLibPathMap &appLibPaths)
{
    std::string patchNativeLibraryPath = bundleInfo.applicationInfo.appQuickFix.deployedAppqfInfo.nativeLibraryPath;
    if (!patchNativeLibraryPath.empty()) {
        // libraries in patch lib path has a higher priority when loading.
        std::string patchLibPath = LOCAL_CODE_PATH;
        patchLibPath += (patchLibPath.back() == '/') ? patchNativeLibraryPath : "/" + patchNativeLibraryPath;
        TAG_LOGD(AAFwkTag::APPKIT, "lib path = %{private}s", patchLibPath.c_str());
        appLibPaths["default"].emplace_back(patchLibPath);
    }

    std::string nativeLibraryPath = bundleInfo.applicationInfo.nativeLibraryPath;
    if (!nativeLibraryPath.empty()) {
        if (nativeLibraryPath.back() == '/') {
            nativeLibraryPath.pop_back();
        }
        std::string libPath = LOCAL_CODE_PATH;
        libPath += (libPath.back() == '/') ? nativeLibraryPath : "/" + nativeLibraryPath;
        TAG_LOGD(AAFwkTag::APPKIT, "lib path = %{private}s", libPath.c_str());
        appLibPaths["default"].emplace_back(libPath);
    }

    for (auto &hapInfo : bundleInfo.hapModuleInfos) {
        TAG_LOGD(AAFwkTag::APPKIT,
            "moduleName: %{public}s, isLibIsolated: %{public}d, compressNativeLibs: %{public}d.",
            hapInfo.moduleName.c_str(), hapInfo.isLibIsolated, hapInfo.compressNativeLibs);
        GetPatchNativeLibPath(hapInfo, patchNativeLibraryPath, appLibPaths);
        GetHapSoPath(hapInfo, appLibPaths, hapInfo.hapPath.find(ABS_CODE_PATH));
    }

    for (auto &hspInfo : hspList) {
        TAG_LOGD(AAFwkTag::APPKIT, "bundle:%s, module:%s, nativeLibraryPath:%s", hspInfo.bundleName.c_str(),
            hspInfo.moduleName.c_str(), hspInfo.nativeLibraryPath.c_str());
        GetHspNativeLibPath(hspInfo, appLibPaths, hspInfo.hapPath.find(ABS_CODE_PATH) != 0u);
    }
}

/**
 *
 * @brief Notify the AppMgrDeathRecipient that the remote is dead.
 *
 * @param remote The remote which is dead.
 */
void AppMgrDeathRecipient::OnRemoteDied(const wptr<IRemoteObject> &remote)
{
    TAG_LOGE(AAFwkTag::APPKIT, "remote died receive");
}

MainThread::MainThread()
{
#ifdef ABILITY_LIBRARY_LOADER
    fileEntries_.clear();
    nativeFileEntries_.clear();
    handleAbilityLib_.clear();
#endif  // ABILITY_LIBRARY_LOADER
}

MainThread::~MainThread()
{
    TAG_LOGD(AAFwkTag::APPKIT, "called");
    if (watchdog_ != nullptr && !watchdog_->IsStopWatchdog()) {
        watchdog_->Stop();
        watchdog_ = nullptr;
    }
}

/**
 *
 * @brief Get the current MainThreadState.
 *
 * @return Returns the current MainThreadState.
 */
MainThreadState MainThread::GetMainThreadState() const
{
    return mainThreadState_;
}

/**
 *
 * @brief Set the runner state of mainthread.
 *
 * @param runnerStart whether the runner is started.
 */
void MainThread::SetRunnerStarted(bool runnerStart)
{
    isRunnerStarted_ = runnerStart;
}

/**
 *
 * @brief Get the runner state of mainthread.
 *
 * @return Returns the runner state of mainthread.
 */
bool MainThread::GetRunnerStarted() const
{
    return isRunnerStarted_;
}

/**
 *
 * @brief Get the newThreadId.
 *
 * @return Returns the newThreadId.
 */
int MainThread::GetNewThreadId()
{
    return newThreadId_++;
}

/**
 *
 * @brief Get the application.
 *
 * @return Returns the application.
 */
std::shared_ptr<OHOSApplication> MainThread::GetApplication() const
{
    return application_;
}

/**
 *
 * @brief Get the applicationInfo.
 *
 * @return Returns the applicationInfo.
 */
std::shared_ptr<ApplicationInfo> MainThread::GetApplicationInfo() const
{
    return applicationInfo_;
}

/**
 *
 * @brief Get the applicationImpl.
 *
 * @return Returns the applicationImpl.
 */
std::shared_ptr<ApplicationImpl> MainThread::GetApplicationImpl()
{
    return applicationImpl_;
}

/**
 *
 * @brief Connect the mainthread to the AppMgr.
 *
 */
bool MainThread::ConnectToAppMgr()
{
    HITRACE_METER_NAME(HITRACE_TAG_APP, __PRETTY_FUNCTION__);
    TAG_LOGD(AAFwkTag::APPKIT, "%{public}s start.", __func__);
    auto object = OHOS::DelayedSingleton<SysMrgClient>::GetInstance()->GetSystemAbility(APP_MGR_SERVICE_ID);
    if (object == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "failed to get app manager service");
        return false;
    }
    deathRecipient_ = new (std::nothrow) AppMgrDeathRecipient();
    if (deathRecipient_ == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "failed to new AppMgrDeathRecipient");
        return false;
    }

    if (!object->AddDeathRecipient(deathRecipient_)) {
        TAG_LOGE(AAFwkTag::APPKIT, "failed to AddDeathRecipient");
        return false;
    }

    appMgr_ = iface_cast<IAppMgr>(object);
    if (appMgr_ == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "failed to iface_cast object to appMgr_");
        return false;
    }
    TAG_LOGI(AAFwkTag::APPKIT, "attach to appMGR.");
    appMgr_->AttachApplication(this);
    TAG_LOGD(AAFwkTag::APPKIT, "end");
    return true;
}

/**
 *
 * @brief Attach the mainthread to the AppMgr.
 *
 */
void MainThread::Attach()
{
    HITRACE_METER_NAME(HITRACE_TAG_APP, __PRETTY_FUNCTION__);
    TAG_LOGD(AAFwkTag::APPKIT, "Attach");
    if (!ConnectToAppMgr()) {
        TAG_LOGE(AAFwkTag::APPKIT, "attachApplication failed");
        return;
    }
    mainThreadState_ = MainThreadState::ATTACH;
}

/**
 *
 * @brief remove the deathRecipient from appMgr.
 *
 */
void MainThread::RemoveAppMgrDeathRecipient()
{
    TAG_LOGD(AAFwkTag::APPKIT, "called");
    if (appMgr_ == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "failed");
        return;
    }

    sptr<IRemoteObject> object = appMgr_->AsObject();
    if (object != nullptr) {
        object->RemoveDeathRecipient(deathRecipient_);
    } else {
        TAG_LOGE(AAFwkTag::APPKIT, "appMgr_->AsObject() failed");
    }
}

/**
 *
 * @brief Get the eventHandler of mainthread.
 *
 * @return Returns the eventHandler of mainthread.
 */
std::shared_ptr<EventHandler> MainThread::GetMainHandler() const
{
    return mainHandler_;
}

/**
 *
 * @brief Schedule the foreground lifecycle of application.
 *
 */
void MainThread::ScheduleForegroundApplication()
{
    HITRACE_METER_NAME(HITRACE_TAG_APP, __PRETTY_FUNCTION__);
    TAG_LOGD(AAFwkTag::APPKIT, "called");
    wptr<MainThread> weak = this;
    auto task = [weak]() {
        auto appThread = weak.promote();
        if (appThread == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "appThread is nullptr.");
            return;
        }
        appThread->HandleForegroundApplication();
    };
    if (!mainHandler_->PostTask(task, "MainThread:ForegroundApplication")) {
        TAG_LOGE(AAFwkTag::APPKIT, "PostTask task failed");
    }
    auto tmpWatchdog = watchdog_;
    if (tmpWatchdog == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "Watch dog is nullptr.");
        return;
    }
    tmpWatchdog->SetBackgroundStatus(false);
    tmpWatchdog = nullptr;
}

/**
 *
 * @brief Schedule the background lifecycle of application.
 *
 */
void MainThread::ScheduleBackgroundApplication()
{
    TAG_LOGD(AAFwkTag::APPKIT, "called");
    wptr<MainThread> weak = this;
    auto task = [weak]() {
        auto appThread = weak.promote();
        if (appThread == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "appThread is nullptr");
            return;
        }
        appThread->HandleBackgroundApplication();
    };
    if (!mainHandler_->PostTask(task, "MainThread:BackgroundApplication")) {
        TAG_LOGE(AAFwkTag::APPKIT, "PostTask task failed");
    }

    auto tmpWatchdog = watchdog_;
    if (tmpWatchdog == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "Watch dog is nullptr.");
        return;
    }
    tmpWatchdog->SetBackgroundStatus(true);
    tmpWatchdog = nullptr;
}

/**
 *
 * @brief Schedule the terminate lifecycle of application.
 *
 * @param isLastProcess When it is the last application process, pass in true.
 */
void MainThread::ScheduleTerminateApplication(bool isLastProcess)
{
    TAG_LOGD(AAFwkTag::APPKIT, "called");
    wptr<MainThread> weak = this;
    auto task = [weak, isLastProcess]() {
        auto appThread = weak.promote();
        if (appThread == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "appThread is nullptr");
            return;
        }
        appThread->HandleTerminateApplication(isLastProcess);
    };
    if (!mainHandler_->PostTask(task, "MainThread:TerminateApplication")) {
        TAG_LOGE(AAFwkTag::APPKIT, "PostTask task failed");
    }
}

/**
 *
 * @brief Shrink the memory which used by application.
 *
 * @param level Indicates the memory trim level, which shows the current memory usage status.
 */
void MainThread::ScheduleShrinkMemory(const int level)
{
    TAG_LOGD(AAFwkTag::APPKIT, "level: %{public}d", level);
    wptr<MainThread> weak = this;
    auto task = [weak, level]() {
        auto appThread = weak.promote();
        if (appThread == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "appThread is nullptr");
            return;
        }
        appThread->HandleShrinkMemory(level);
    };
    if (!mainHandler_->PostTask(task, "MainThread:ShrinkMemory")) {
        TAG_LOGE(AAFwkTag::APPKIT, "PostTask task failed");
    }
}

/**
 *
 * @brief Notify the memory level.
 *
 * @param level Indicates the memory trim level, which shows the current memory usage status.
 */
void MainThread::ScheduleMemoryLevel(const int level)
{
    TAG_LOGD(AAFwkTag::APPKIT, "level: %{public}d", level);
    wptr<MainThread> weak = this;
    auto task = [weak, level]() {
        auto appThread = weak.promote();
        if (appThread == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "appThread is nullptr");
            return;
        }
        appThread->HandleMemoryLevel(level);
    };
    if (!mainHandler_->PostTask(task, "MainThread:MemoryLevel")) {
        TAG_LOGE(AAFwkTag::APPKIT, "PostTask task failed");
    }
}

/**
 *
 * @brief Get the application's memory allocation info.
 *
 * @param pid, pid input.
 * @param mallocInfo, dynamic storage information output.
 */
void MainThread::ScheduleHeapMemory(const int32_t pid, OHOS::AppExecFwk::MallocInfo &mallocInfo)
{
    struct mallinfo mi = mallinfo();
    int usmblks = mi.usmblks; // 当前从分配器中分配的总的堆内存大小
    int uordblks = mi.uordblks; // 当前已释放给分配器，分配缓存了未释放给系统的内存大小
    int fordblks = mi.fordblks; // 当前未释放的大小
    int hblkhd = mi.hblkhd; // 堆内存的总共占用大小
    TAG_LOGD(AAFwkTag::APPKIT, "The pid of the app we want to dump memory allocation information is: %{public}i", pid);
    TAG_LOGD(AAFwkTag::APPKIT, "usmblks: %{public}i, uordblks: %{public}i, fordblks: %{public}i, hblkhd: %{public}i",
        usmblks, uordblks, fordblks, hblkhd);
    mallocInfo.usmblks = usmblks;
    mallocInfo.uordblks = uordblks;
    mallocInfo.fordblks = fordblks;
    mallocInfo.hblkhd = hblkhd;
}

/**
 *
 * @brief the application triggerGC and dump jsheap memory.
 *
 * @param info, pid, tid, needGC, needSnapshot.
 */
void MainThread::ScheduleJsHeapMemory(OHOS::AppExecFwk::JsHeapDumpInfo &info)
{
    TAG_LOGI(AAFwkTag::APPKIT, "pid: %{public}d, tid: %{public}d, needGc: %{public}d, needSnapshot: %{public}d",
        info.pid, info.tid, info.needGc, info.needSnapshot);
    auto app = applicationForDump_.lock();
    if (app == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "app nullptr");
        return;
    }
    auto &runtime = app->GetRuntime();
    if (runtime == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "runtime nullptr");
        return;
    }
    if (info.needSnapshot == true) {
        runtime->DumpHeapSnapshot(info.tid, info.needGc);
    } else {
        if (info.needGc == true) {
            runtime->ForceFullGC(info.tid);
        }
    }
}

/**
 *
 * @brief Schedule the application process exit safely.
 *
 */
void MainThread::ScheduleProcessSecurityExit()
{
    wptr<MainThread> weak = this;
    auto task = [weak]() {
        auto appThread = weak.promote();
        if (appThread == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "appThread is nullptr");
            return;
        }
        appThread->HandleProcessSecurityExit();
    };
    bool result = mainHandler_->PostTask(task, "MainThread:ProcessSecurityExit");
    if (!result) {
        TAG_LOGE(AAFwkTag::APPKIT, "post task failed");
    }
}

/**
 *
 * @brief Low the memory which used by application.
 *
 */
void MainThread::ScheduleLowMemory()
{}

/**
 *
 * @brief Launch the application.
 *
 * @param data The launchdata of the application witch launced.
 *
 */
void MainThread::ScheduleLaunchApplication(const AppLaunchData &data, const Configuration &config)
{
    HITRACE_METER_NAME(HITRACE_TAG_APP, __PRETTY_FUNCTION__);
    TAG_LOGD(AAFwkTag::APPKIT, "called");
    wptr<MainThread> weak = this;
    auto task = [weak, data, config]() {
        auto appThread = weak.promote();
        if (appThread == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "appThread is nullptr");
            return;
        }
        appThread->HandleInitAssertFaultTask(data.GetDebugApp(), data.GetApplicationInfo().debug);
        appThread->HandleLaunchApplication(data, config);
    };
    if (!mainHandler_->PostTask(task, "MainThread:LaunchApplication")) {
        TAG_LOGE(AAFwkTag::APPKIT, "PostTask task failed");
    }
}

/**
 *
 * @brief update the application info after new module installed.
 *
 * @param appInfo The latest application info obtained from bms for update abilityRuntimeContext.
 *
 */
void MainThread::ScheduleUpdateApplicationInfoInstalled(const ApplicationInfo &appInfo)
{
    TAG_LOGD(AAFwkTag::APPKIT, "ScheduleUpdateApplicationInfoInstalled");
    wptr<MainThread> weak = this;
    auto task = [weak, appInfo]() {
        auto appThread = weak.promote();
        if (appThread == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "appThread is nullptr");
            return;
        }
        appThread->HandleUpdateApplicationInfoInstalled(appInfo);
    };
    if (!mainHandler_->PostTask(task, "MainThread:UpdateApplicationInfoInstalled")) {
        TAG_LOGE(AAFwkTag::APPKIT, "PostTask task failed");
    }
}

void MainThread::ScheduleAbilityStage(const HapModuleInfo &abilityStage)
{
    TAG_LOGD(AAFwkTag::APPKIT, "called");
    wptr<MainThread> weak = this;
    auto task = [weak, abilityStage]() {
        auto appThread = weak.promote();
        if (appThread == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "appThread is nullptr");
            return;
        }
        appThread->HandleAbilityStage(abilityStage);
    };
    if (!mainHandler_->PostTask(task, "MainThread:AbilityStage")) {
        TAG_LOGE(AAFwkTag::APPKIT, "PostTask task failed");
    }
}

bool MainThread::IsBgWorkingThread(const AbilityInfo &info)
{
    return info.extensionAbilityType == ExtensionAbilityType::BACKUP;
}

void MainThread::ScheduleLaunchAbility(const AbilityInfo &info, const sptr<IRemoteObject> &token,
    const std::shared_ptr<AAFwk::Want> &want, int32_t abilityRecordId)
{
    HITRACE_METER_NAME(HITRACE_TAG_ABILITY_MANAGER, __PRETTY_FUNCTION__);
    TAG_LOGI(AAFwkTag::APPKIT, "%{public}s called, ability %{public}s, type is %{public}d.",
        __func__, info.name.c_str(), info.type);

    if (want != nullptr) {
        AAFwk::Want newWant(*want);
        newWant.CloseAllFd();
    }
    std::shared_ptr<AbilityInfo> abilityInfo = std::make_shared<AbilityInfo>(info);
    auto abilityRecord = std::make_shared<AbilityLocalRecord>(abilityInfo, token);
    abilityRecord->SetWant(want);
    abilityRecord->SetAbilityRecordId(abilityRecordId);
    auto tmpWatchdog = watchdog_;
    if (tmpWatchdog != nullptr) {
        tmpWatchdog->SetBgWorkingThreadStatus(IsBgWorkingThread(info));
        tmpWatchdog = nullptr;
    }
    FreezeUtil::LifecycleFlow flow = { token, FreezeUtil::TimeoutState::LOAD };
    std::string entry = std::to_string(AbilityRuntime::TimeUtil::SystemTimeMillisecond()) +
        "; MainThread::ScheduleLaunchAbility; the load lifecycle.";
    FreezeUtil::GetInstance().AddLifecycleEvent(flow, entry);

    wptr<MainThread> weak = this;
    auto task = [weak, abilityRecord]() {
        auto appThread = weak.promote();
        if (appThread == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "appThread is nullptr");
            return;
        }
        appThread->HandleLaunchAbility(abilityRecord);
    };
    if (!mainHandler_->PostTask(task, "MainThread:LaunchAbility")) {
        TAG_LOGE(AAFwkTag::APPKIT, "PostTask task failed");
    }
}

/**
 *
 * @brief clean the ability by token.
 *
 * @param token The token belong to the ability which want to be cleaned.
 *
 */
void MainThread::ScheduleCleanAbility(const sptr<IRemoteObject> &token, bool isCacheProcess)
{
    TAG_LOGD(AAFwkTag::APPKIT, "called, with isCacheProcess =%{public}d.", isCacheProcess);
    wptr<MainThread> weak = this;
    auto task = [weak, token, isCacheProcess]() {
        auto appThread = weak.promote();
        if (appThread == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "appThread is nullptr");
            return;
        }
        appThread->HandleCleanAbility(token, isCacheProcess);
    };
    if (!mainHandler_->PostTask(task, "MainThread:CleanAbility")) {
        TAG_LOGE(AAFwkTag::APPKIT, "PostTask task failed");
    }
}

/**
 *
 * @brief send the new profile.
 *
 * @param profile The updated profile.
 *
 */
void MainThread::ScheduleProfileChanged(const Profile &profile)
{
    TAG_LOGD(AAFwkTag::APPKIT, "profile name: %{public}s", profile.GetName().c_str());
}

/**
 *
 * @brief send the new config to the application.
 *
 * @param config The updated config.
 *
 */
void MainThread::ScheduleConfigurationUpdated(const Configuration &config)
{
    HITRACE_METER_NAME(HITRACE_TAG_ABILITY_MANAGER, __PRETTY_FUNCTION__);
    TAG_LOGD(AAFwkTag::APPKIT, "called");
    wptr<MainThread> weak = this;
    auto task = [weak, config]() {
        auto appThread = weak.promote();
        if (appThread == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "appThread is nullptr");
            return;
        }
        appThread->HandleConfigurationUpdated(config);
    };
    if (!mainHandler_->PostTask(task, "MainThread:ConfigurationUpdated")) {
        TAG_LOGE(AAFwkTag::APPKIT, "PostTask task failed");
    }
}

bool MainThread::CheckLaunchApplicationParam(const AppLaunchData &appLaunchData) const
{
    ApplicationInfo appInfo = appLaunchData.GetApplicationInfo();
    ProcessInfo processInfo = appLaunchData.GetProcessInfo();

    if (appInfo.name.empty()) {
        TAG_LOGE(AAFwkTag::APPKIT, "applicationName is empty");
        return false;
    }

    if (processInfo.GetProcessName().empty()) {
        TAG_LOGE(AAFwkTag::APPKIT, "processName is empty");
        return false;
    }
    return true;
}

/**
 *
 * @brief Check whether the record is legal.
 *
 * @param record The record should be checked.
 *
 * @return if the record is legal, return true. else return false.
 */
bool MainThread::CheckAbilityItem(const std::shared_ptr<AbilityLocalRecord> &record) const
{
    if (record == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "record is null");
        return false;
    }

    std::shared_ptr<AbilityInfo> abilityInfo = record->GetAbilityInfo();
    sptr<IRemoteObject> token = record->GetToken();

    if (abilityInfo == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "abilityInfo is null");
        return false;
    }

    if (token == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "token is null");
        return false;
    }
    return true;
}

/**
 *
 * @brief Terminate the application but don't notify ams.
 *
 */
void MainThread::HandleTerminateApplicationLocal()
{
    TAG_LOGD(AAFwkTag::APPKIT, "called");
    if (application_ == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "error!");
        return;
    }
    applicationImpl_->PerformTerminateStrong();

    std::shared_ptr<EventRunner> runner = mainHandler_->GetEventRunner();
    if (runner == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "get manHandler error");
        return;
    }

    if (watchdog_ != nullptr && !watchdog_->IsStopWatchdog()) {
        watchdog_->Stop();
        watchdog_ = nullptr;
    }

    int ret = runner->Stop();
    if (ret != ERR_OK) {
        TAG_LOGE(AAFwkTag::APPKIT, "runner->Run failed ret = %{public}d", ret);
    }

    TAG_LOGD(AAFwkTag::APPKIT, "runner is stopped");
    SetRunnerStarted(false);
    HandleCancelAssertFaultTask();
}

/**
 *
 * @brief Schedule the application process exit safely.
 *
 */
void MainThread::HandleProcessSecurityExit()
{
    HITRACE_METER_NAME(HITRACE_TAG_APP, __PRETTY_FUNCTION__);
    TAG_LOGD(AAFwkTag::APPKIT, "HandleProcessSecurityExit");
    if (abilityRecordMgr_ == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "abilityRecordMgr_ is null");
        return;
    }

    std::vector<sptr<IRemoteObject>> tokens = (abilityRecordMgr_->GetAllTokens());

    for (auto iter = tokens.begin(); iter != tokens.end(); ++iter) {
        HandleCleanAbilityLocal(*iter);
    }

    // in process cache state, there can be abilityStage with no abilities
    application_->CleanEmptyAbilityStage();

    HandleTerminateApplicationLocal();
}

bool MainThread::InitCreate(
    std::shared_ptr<ContextDeal> &contextDeal, ApplicationInfo &appInfo, ProcessInfo &processInfo)
{
    HITRACE_METER_NAME(HITRACE_TAG_APP, __PRETTY_FUNCTION__);
    // get application shared point
    application_ = std::shared_ptr<OHOSApplication>(ApplicationLoader::GetInstance().GetApplicationByName());
    if (application_ == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "create failed");
        return false;
    }

    applicationInfo_ = std::make_shared<ApplicationInfo>(appInfo);
    if (applicationInfo_ == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "create applicationInfo_ failed");
        return false;
    }

    processInfo_ = std::make_shared<ProcessInfo>(processInfo);
    if (processInfo_ == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "create processInfo_ failed");
        return false;
    }

    applicationImpl_ = std::make_shared<ApplicationImpl>();
    if (applicationImpl_ == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "create applicationImpl_ failed");
        return false;
    }

    abilityRecordMgr_ = std::make_shared<AbilityRecordMgr>();
    if (abilityRecordMgr_ == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "create AbilityRecordMgr failed");
        return false;
    }

    contextDeal = std::make_shared<ContextDeal>();
    if (contextDeal == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "create contextDeal failed");
        return false;
    }
    AppExecFwk::AppfreezeInner::GetInstance()->SetApplicationInfo(applicationInfo_);

    application_->SetProcessInfo(processInfo_);
    contextDeal->SetApplicationInfo(applicationInfo_);
    contextDeal->SetBundleCodePath(applicationInfo_->codePath);  // BMS need to add cpath

    return true;
}

bool MainThread::CheckForHandleLaunchApplication(const AppLaunchData &appLaunchData)
{
    if (application_ != nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "already create application");
        return false;
    }

    if (!CheckLaunchApplicationParam(appLaunchData)) {
        TAG_LOGE(AAFwkTag::APPKIT, "appLaunchData invalid");
        return false;
    }
    return true;
}

bool MainThread::InitResourceManager(std::shared_ptr<Global::Resource::ResourceManager> &resourceManager,
    const AppExecFwk::HapModuleInfo &entryHapModuleInfo, const std::string &bundleName,
    bool multiProjects, const Configuration &config)
{
    HITRACE_METER_NAME(HITRACE_TAG_APP, __PRETTY_FUNCTION__);
    bool isStageBased = entryHapModuleInfo.isStageBasedModel;
    if (isStageBased && multiProjects) {
        TAG_LOGI(AAFwkTag::APPKIT, "multiProjects");
    } else {
        OnStartAbility(bundleName, resourceManager, entryHapModuleInfo);
    }

    std::unique_ptr<Global::Resource::ResConfig> resConfig(Global::Resource::CreateResConfig());
#if defined(SUPPORT_GRAPHICS) && defined(SUPPORT_APP_PREFERRED_LANGUAGE)
    UErrorCode status = U_ZERO_ERROR;
    icu::Locale systemLocale = icu::Locale::forLanguageTag(Global::I18n::LocaleConfig::GetSystemLocale(), status);
    resConfig->SetLocaleInfo(systemLocale);

    if (Global::I18n::PreferredLanguage::IsSetAppPreferredLanguage()) {
        icu::Locale preferredLocale =
            icu::Locale::forLanguageTag(Global::I18n::PreferredLanguage::GetAppPreferredLanguage(), status);
        resConfig->SetPreferredLocaleInfo(preferredLocale);
    }
#endif
    std::string colormode = config.GetItem(AAFwk::GlobalConfigurationKey::SYSTEM_COLORMODE);
    TAG_LOGD(AAFwkTag::APPKIT, "Colormode is %{public}s", colormode.c_str());
    resConfig->SetColorMode(ConvertColorMode(colormode));

    std::string hasPointerDevice = config.GetItem(AAFwk::GlobalConfigurationKey::INPUT_POINTER_DEVICE);
    TAG_LOGD(AAFwkTag::APPKIT, "HasPointerDevice is %{public}s", hasPointerDevice.c_str());
    resConfig->SetInputDevice(ConvertHasPointerDevice(hasPointerDevice));

    std::string deviceType = config.GetItem(AAFwk::GlobalConfigurationKey::DEVICE_TYPE);
    TAG_LOGD(AAFwkTag::APPKIT, "deviceType is %{public}s <---->  %{public}d", deviceType.c_str(),
        ConvertDeviceType(deviceType));
    resConfig->SetDeviceType(ConvertDeviceType(deviceType));

    std::string mcc = config.GetItem(AAFwk::GlobalConfigurationKey::SYSTEM_MCC);
    TAG_LOGD(AAFwkTag::APPKIT, "mcc is %{public}s", mcc.c_str());
    uint32_t mccNum = 0;
    if (AbilityRuntime::ResourceConfigHelper::ConvertStringToUint32(mcc, mccNum)) {
        resConfig->SetMcc(mccNum);
    }

    std::string mnc = config.GetItem(AAFwk::GlobalConfigurationKey::SYSTEM_MNC);
    TAG_LOGD(AAFwkTag::APPKIT, "mnc is %{public}s", mnc.c_str());
    uint32_t mncNum = 0;
    if (AbilityRuntime::ResourceConfigHelper::ConvertStringToUint32(mnc, mncNum)) {
        resConfig->SetMnc(mncNum);
    }

    resourceManager->UpdateResConfig(*resConfig);
    return true;
}

void MainThread::OnStartAbility(const std::string &bundleName,
    std::shared_ptr<Global::Resource::ResourceManager> &resourceManager,
    const AppExecFwk::HapModuleInfo &entryHapModuleInfo)
{
    std::regex pattern(std::string(ABS_CODE_PATH) + std::string(FILE_SEPARATOR) + bundleName);
    std::string loadPath =
        (!entryHapModuleInfo.hapPath.empty()) ? entryHapModuleInfo.hapPath : entryHapModuleInfo.resourcePath;
    if (!loadPath.empty()) {
        loadPath = std::regex_replace(loadPath, pattern, std::string(LOCAL_CODE_PATH));
        TAG_LOGD(AAFwkTag::APPKIT, "ModuleResPath: %{public}s", loadPath.c_str());
        // getOverlayPath
        auto res = GetOverlayModuleInfos(bundleName, entryHapModuleInfo.moduleName, overlayModuleInfos_);
        if (res != ERR_OK) {
            TAG_LOGW(AAFwkTag::APPKIT, "getOverlayPath failed");
        }
        if (overlayModuleInfos_.size() == 0) {
            if (!resourceManager->AddResource(loadPath.c_str())) {
                TAG_LOGE(AAFwkTag::APPKIT, "AddResource failed");
            }
        } else {
            std::vector<std::string> overlayPaths = GetOverlayPaths(bundleName, overlayModuleInfos_);
            TAG_LOGD(AAFwkTag::APPKIT, "OverlayPaths size:%{public}zu", overlayPaths.size());
            if (!resourceManager->AddResource(loadPath, overlayPaths)) {
                TAG_LOGE(AAFwkTag::APPKIT, "AddResource failed");
            }
            SubscribeOverlayChange(bundleName, loadPath, resourceManager, entryHapModuleInfo);
        }
    }
}

std::vector<std::string> MainThread::GetOverlayPaths(const std::string &bundleName,
    const std::vector<OverlayModuleInfo> &overlayModuleInfos)
{
    std::vector<std::string> overlayPaths;
    for (auto it : overlayModuleInfos_) {
        if (std::regex_search(it.hapPath, std::regex(bundleName))) {
            it.hapPath = std::regex_replace(it.hapPath, std::regex(std::string(ABS_CODE_PATH) +
                std::string(FILE_SEPARATOR) + bundleName), std::string(LOCAL_CODE_PATH));
        } else {
            it.hapPath = std::regex_replace(it.hapPath, std::regex(ABS_CODE_PATH), LOCAL_BUNDLES);
        }
        if (it.state == OverlayState::OVERLAY_ENABLE) {
            TAG_LOGD(AAFwkTag::APPKIT, "hapPath: %{public}s", it.hapPath.c_str());
            overlayPaths.emplace_back(it.hapPath);
        }
    }
    return overlayPaths;
}

void MainThread::SubscribeOverlayChange(const std::string &bundleName, const std::string &loadPath,
    std::shared_ptr<Global::Resource::ResourceManager> &resourceManager,
    const AppExecFwk::HapModuleInfo &entryHapModuleInfo)
{
    // add listen overlay change
    EventFwk::MatchingSkills matchingSkills;
    matchingSkills.AddEvent(OVERLAY_STATE_CHANGED);
    EventFwk::CommonEventSubscribeInfo subscribeInfo(matchingSkills);
    subscribeInfo.SetThreadMode(EventFwk::CommonEventSubscribeInfo::COMMON);
    wptr<MainThread> weak = this;
    auto callback = [weak, resourceManager, bundleName, moduleName = entryHapModuleInfo.moduleName,
        loadPath](const EventFwk::CommonEventData &data) {
        TAG_LOGD(AAFwkTag::APPKIT, "On overlay changed");
        auto appThread = weak.promote();
        if (appThread == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "abilityThread is nullptr, SetRunnerStarted failed");
            return;
        }
        appThread->OnOverlayChanged(data, resourceManager, bundleName, moduleName, loadPath);
    };
    auto subscriber = std::make_shared<OverlayEventSubscriber>(subscribeInfo, callback);
    bool subResult = EventFwk::CommonEventManager::SubscribeCommonEvent(subscriber);
    TAG_LOGD(AAFwkTag::APPKIT, "Overlay event subscriber register result is %{public}d", subResult);
}

void MainThread::OnOverlayChanged(const EventFwk::CommonEventData &data,
    const std::shared_ptr<Global::Resource::ResourceManager> &resourceManager, const std::string &bundleName,
    const std::string &moduleName, const std::string &loadPath)
{
    if (mainHandler_ == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "mainHandler is nullptr");
        return;
    }
    wptr<MainThread> weak = this;
    auto task = [weak, data, resourceManager, bundleName, moduleName, loadPath]() {
        auto appThread = weak.promote();
        if (appThread == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "abilityThread is nullptr");
            return;
        }
        appThread->HandleOnOverlayChanged(data, resourceManager, bundleName, moduleName, loadPath);
    };
    if (!mainHandler_->PostTask(task, "MainThread:OnOverlayChanged")) {
        TAG_LOGE(AAFwkTag::APPKIT, "PostTask task failed");
    }
}

void MainThread::HandleOnOverlayChanged(const EventFwk::CommonEventData &data,
    const std::shared_ptr<Global::Resource::ResourceManager> &resourceManager, const std::string &bundleName,
    const std::string &moduleName, const std::string &loadPath)
{
    TAG_LOGD(AAFwkTag::APPKIT, "begin");
    auto want = data.GetWant();
    std::string action = want.GetAction();
    if (action != OVERLAY_STATE_CHANGED) {
        TAG_LOGD(AAFwkTag::APPKIT, "Not this subscribe, action: %{public}s", action.c_str());
        return;
    }
    bool isEnable = data.GetWant().GetBoolParam(Constants::OVERLAY_STATE, false);
    // 1.get overlay hapPath
    if (resourceManager == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "resourceManager is nullptr");
        return;
    }
    std::vector<OverlayModuleInfo> overlayModuleInfos;
    auto res = GetOverlayModuleInfos(bundleName, moduleName, overlayModuleInfos);
    if (res != ERR_OK) {
        return;
    }

    // 2.add/remove overlay hapPath
    if (loadPath.empty() || overlayModuleInfos.size() == 0) {
        TAG_LOGW(AAFwkTag::APPKIT, "There is not any hapPath in overlayModuleInfo");
    } else {
        if (isEnable) {
            std::vector<std::string> overlayPaths = GetAddOverlayPaths(overlayModuleInfos);
            if (!resourceManager->AddResource(loadPath, overlayPaths)) {
                TAG_LOGE(AAFwkTag::APPKIT, "AddResource failed");
            }
        } else {
            std::vector<std::string> overlayPaths = GetRemoveOverlayPaths(overlayModuleInfos);
            if (!resourceManager->RemoveResource(loadPath, overlayPaths)) {
                TAG_LOGE(AAFwkTag::APPKIT, "RemoveResource failed");
            }
        }
    }
}

bool IsNeedLoadLibrary(const std::string &bundleName)
{
    std::vector<std::string> needLoadLibraryBundleNames{
        "com.ohos.contactsdataability",
        "com.ohos.medialibrary.medialibrarydata",
        "com.ohos.ringtonelibrary.ringtonelibrarydata",
        "com.ohos.telephonydataability",
        "com.ohos.FusionSearch",
        "com.ohos.formrenderservice"
    };

    for (const auto &item : needLoadLibraryBundleNames) {
        if (item == bundleName) {
            return true;
        }
    }
    return false;
}

bool GetBundleForLaunchApplication(std::shared_ptr<BundleMgrHelper> bundleMgrHelper, const std::string &bundleName,
    int32_t appIndex, BundleInfo &bundleInfo)
{
    bool queryResult;
    if (appIndex > AbilityRuntime::GlobalConstant::MAX_APP_CLONE_INDEX) {
        TAG_LOGD(AAFwkTag::APPKIT, "The bundleName = %{public}s", bundleName.c_str());
        queryResult = (bundleMgrHelper->GetSandboxBundleInfo(bundleName,
            appIndex, UNSPECIFIED_USERID, bundleInfo) == 0);
    } else {
        TAG_LOGD(AAFwkTag::APPKIT, "The bundleName = %{public}s", bundleName.c_str());
        queryResult = (bundleMgrHelper->GetBundleInfoForSelf(
            (static_cast<int32_t>(GetBundleInfoFlag::GET_BUNDLE_INFO_WITH_EXTENSION_ABILITY) +
            static_cast<int32_t>(GetBundleInfoFlag::GET_BUNDLE_INFO_WITH_HAP_MODULE) +
            static_cast<int32_t>(GetBundleInfoFlag::GET_BUNDLE_INFO_WITH_DISABLE) +
            static_cast<int32_t>(GetBundleInfoFlag::GET_BUNDLE_INFO_WITH_APPLICATION) +
            static_cast<int32_t>(GetBundleInfoFlag::GET_BUNDLE_INFO_WITH_SIGNATURE_INFO) +
            static_cast<int32_t>(GetBundleInfoFlag::GET_BUNDLE_INFO_WITH_ABILITY) +
#ifdef SUPPORT_GRAPHICS
            static_cast<int32_t>(GetBundleInfoFlag::GET_BUNDLE_INFO_WITH_REQUESTED_PERMISSION) +
#endif
            static_cast<int32_t>(GetBundleInfoFlag::GET_BUNDLE_INFO_WITH_METADATA)), bundleInfo) == ERR_OK);
    }
    return queryResult;
}
#ifdef CJ_FRONTEND
CJUncaughtExceptionInfo MainThread::CreateCjExceptionInfo(const std::string &bundleName,
    uint32_t versionCode, const std::string &hapPath)
{
    CJUncaughtExceptionInfo uncaughtExceptionInfo;
    wptr<MainThread> weak_this = this;
    uncaughtExceptionInfo.hapPath = hapPath.c_str();
    uncaughtExceptionInfo.uncaughtTask = [weak_this, bundleName, versionCode]
        (std::string summary, const CJErrorObject errorObj) {
            auto appThread = weak_this.promote();
            if (appThread == nullptr) {
                TAG_LOGE(AAFwkTag::APPKIT, "appThread is nullptr");
                return;
            }
            time_t timet;
            time(&timet);
            std::string errName = errorObj.name ? errorObj.name : "[none]";
            std::string errMsg = errorObj.message ? errorObj.message : "[none]";
            std::string errStack = errorObj.stack ? errorObj.stack : "[none]";
            HiSysEventWrite(OHOS::HiviewDFX::HiSysEvent::Domain::AAFWK, "CJ_ERROR",
                OHOS::HiviewDFX::HiSysEvent::EventType::FAULT,
                EVENT_KEY_PACKAGE_NAME, bundleName,
                EVENT_KEY_VERSION, std::to_string(versionCode),
                EVENT_KEY_TYPE, JSCRASH_TYPE,
                EVENT_KEY_HAPPEN_TIME, timet,
                EVENT_KEY_REASON, errName,
                EVENT_KEY_JSVM, JSVM_TYPE,
                EVENT_KEY_SUMMARY, summary);
            ErrorObject appExecErrorObj = {
                .name = errName,
                .message = errMsg,
                .stack = errStack
            };
            FaultData faultData;
            faultData.faultType = FaultDataType::CJ_ERROR;
            faultData.errorObject = appExecErrorObj;
            DelayedSingleton<AppExecFwk::AppMgrClient>::GetInstance()->NotifyAppFault(faultData);
            if (ApplicationDataManager::GetInstance().NotifyCJUnhandledException(summary) &&
                ApplicationDataManager::GetInstance().NotifyCJExceptionObject(appExecErrorObj)) {
                return;
            }
            // if app's callback has been registered, let app decide whether exit or not.
            TAG_LOGE(AAFwkTag::APPKIT,
                "\n%{public}s is about to exit due to RuntimeError\nError type:%{public}s\n%{public}s\n"
                "message: %{public}s\n"
                "stack: %{public}s",
                bundleName.c_str(), errName.c_str(), summary.c_str(), errMsg.c_str(), errStack.c_str());
            AAFwk::ExitReason exitReason = { REASON_CJ_ERROR, errName };
            AbilityManagerClient::GetInstance()->RecordAppExitReason(exitReason);
            appThread->ScheduleProcessSecurityExit();
        };
    return uncaughtExceptionInfo;
}
#endif
/**
 *
 * @brief Launch the application.
 *
 * @param appLaunchData The launchdata of the application witch launced.
 *
 */
void MainThread::HandleLaunchApplication(const AppLaunchData &appLaunchData, const Configuration &config)
{
    HITRACE_METER_NAME(HITRACE_TAG_APP, __PRETTY_FUNCTION__);
    if (!CheckForHandleLaunchApplication(appLaunchData)) {
        TAG_LOGE(AAFwkTag::APPKIT, "CheckForHandleLaunchApplication failed");
        return;
    }

    if (appLaunchData.GetDebugApp() && watchdog_ != nullptr && !watchdog_->IsStopWatchdog()) {
        SetAppDebug(AbilityRuntime::AppFreezeState::AppFreezeFlag::DEBUG_LAUNCH_MODE, true);
        watchdog_->Stop();
        watchdog_.reset();
    }

    auto appInfo = appLaunchData.GetApplicationInfo();
    ProcessInfo processInfo = appLaunchData.GetProcessInfo();
    TAG_LOGD(AAFwkTag::APPKIT, "InitCreate Start");
    std::shared_ptr<ContextDeal> contextDeal;
    if (!InitCreate(contextDeal, appInfo, processInfo)) {
        TAG_LOGE(AAFwkTag::APPKIT, "InitCreate failed");
        return;
    }
    auto bundleMgrHelper = contextDeal->GetBundleManager();
    if (bundleMgrHelper == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "The bundleMgrHelper is nullptr");
        return;
    }

    auto bundleName = appInfo.bundleName;
    auto tmpWatchdog = watchdog_;
    if (tmpWatchdog != nullptr) {
        tmpWatchdog->SetBundleInfo(bundleName, appInfo.versionName);
        tmpWatchdog = nullptr;
    }
    BundleInfo bundleInfo;
    if (!GetBundleForLaunchApplication(bundleMgrHelper, bundleName, appLaunchData.GetAppIndex(), bundleInfo)) {
        TAG_LOGE(AAFwkTag::APPKIT, "Failed to get bundle info");
        return;
    }

    bool moduelJson = false;
    bool isStageBased = false;
    bool findEntryHapModuleInfo = false;
#ifdef CJ_FRONTEND
    bool isCJApp = false;
#endif
    AppExecFwk::HapModuleInfo entryHapModuleInfo;
    if (!bundleInfo.hapModuleInfos.empty()) {
        for (auto hapModuleInfo : bundleInfo.hapModuleInfos) {
            if (hapModuleInfo.moduleType == AppExecFwk::ModuleType::ENTRY) {
                findEntryHapModuleInfo = true;
                entryHapModuleInfo = hapModuleInfo;
                break;
            }
        }
        if (!findEntryHapModuleInfo) {
            TAG_LOGW(AAFwkTag::APPKIT, "HandleLaunchApplication find entry hap module info failed");
            entryHapModuleInfo = bundleInfo.hapModuleInfos.back();
        }
#ifdef CJ_FRONTEND
        if (!entryHapModuleInfo.abilityInfos.empty()) {
            isCJApp = AbilityRuntime::CJRuntime::IsCJAbility(entryHapModuleInfo.abilityInfos.front().srcEntrance);
        }
#endif
        moduelJson = entryHapModuleInfo.isModuleJson;
        isStageBased = entryHapModuleInfo.isStageBasedModel;
    }

#ifdef SUPPORT_GRAPHICS
    std::vector<OHOS::AppExecFwk::Metadata> metaData = entryHapModuleInfo.metadata;
    bool isFullUpdate = std::any_of(metaData.begin(), metaData.end(), [](const auto &metaDataItem) {
        return metaDataItem.name == "ArkTSPartialUpdate" && metaDataItem.value == "false";
    });
    bool isReqForm = std::any_of(bundleInfo.reqPermissions.begin(), bundleInfo.reqPermissions.end(),
        [] (const auto &reqPermission) {
        return reqPermission == OHOS::AppExecFwk::Constants::PERMISSION_REQUIRE_FORM;
    });
    Ace::AceForwardCompatibility::Init(bundleName, appInfo.apiCompatibleVersion, (isFullUpdate || isReqForm));
#endif

    if (IsNeedLoadLibrary(bundleName)) {
        std::vector<std::string> localPaths;
        ChangeToLocalPath(bundleName, appInfo.moduleSourceDirs, localPaths);
        LoadAbilityLibrary(localPaths);
        LoadNativeLiabrary(bundleInfo, appInfo.nativeLibraryPath);
#ifdef SUPPORT_GRAPHICS
    } else if (Ace::AceForwardCompatibility::PipelineChanged()) {
        std::vector<std::string> localPaths;
        ChangeToLocalPath(bundleName, appInfo.moduleSourceDirs, localPaths);
        LoadAbilityLibrary(localPaths);
#endif
    }
    if (appInfo.needAppDetail) {
        TAG_LOGD(AAFwkTag::APPKIT,
            "MainThread::handleLaunchApplication %{public}s need add app detail ability library path",
            bundleName.c_str());
        LoadAppDetailAbilityLibrary(appInfo.appDetailAbilityLibraryPath);
    }
    LoadAppLibrary();

    applicationForDump_ = application_;

    if (isStageBased) {
        AppRecovery::GetInstance().InitApplicationInfo(GetMainHandler(), GetApplicationInfo());
    }
    TAG_LOGD(AAFwkTag::APPKIT, "stageBased:%{public}d moduleJson:%{public}d size:%{public}zu",
        isStageBased, moduelJson, bundleInfo.hapModuleInfos.size());

    // create contextImpl
    std::shared_ptr<AbilityRuntime::ContextImpl> contextImpl = std::make_shared<AbilityRuntime::ContextImpl>();
    contextImpl->SetApplicationInfo(std::make_shared<ApplicationInfo>(appInfo));
    std::shared_ptr<AbilityRuntime::ApplicationContext> applicationContext =
        AbilityRuntime::ApplicationContext::GetInstance();
    int32_t appIndex = appLaunchData.GetAppIndex();
    applicationContext->SetCurrentAppCloneIndex(appIndex);
    applicationContext->SetCurrentAppMode(static_cast<int32_t>(appInfo.multiAppMode.multiAppModeType));
    applicationContext->AttachContextImpl(contextImpl);
    auto appRunningId = appLaunchData.GetAppRunningUniqueId();
    applicationContext->SetAppRunningUniqueId(appRunningId);
    if (DFX_SetAppRunningUniqueId != nullptr) {
        DFX_SetAppRunningUniqueId(appRunningId.c_str(), appRunningId.length());
    }
    application_->SetApplicationContext(applicationContext);

#ifdef SUPPORT_GRAPHICS
    TAG_LOGD(
        AAFwkTag::APPKIT, "HandleLaunchApplication cacheDir: %{public}s", applicationContext->GetCacheDir().c_str());
        SetCacheDir(applicationContext->GetCacheDir());
    // OHOS::EglSetCacheDir(applicationContext->GetCacheDir());
#endif

    HspList hspList;
    ErrCode ret = bundleMgrHelper->GetBaseSharedBundleInfos(appInfo.bundleName, hspList,
        AppExecFwk::GetDependentBundleInfoFlag::GET_ALL_DEPENDENT_BUNDLE_INFO);
    if (ret != ERR_OK) {
        TAG_LOGE(AAFwkTag::APPKIT, "Get base shared bundle infos failed: %{public}d", ret);
    }
    
    std::map<std::string, std::string> pkgContextInfoJsonStringMap;
    for (auto hapModuleInfo : bundleInfo.hapModuleInfos) {
        pkgContextInfoJsonStringMap[hapModuleInfo.moduleName] = hapModuleInfo.hapPath;
    }
    
    AppLibPathMap appLibPaths {};
    GetNativeLibPath(bundleInfo, hspList, appLibPaths);
    bool isSystemApp = bundleInfo.applicationInfo.isSystemApp;
    TAG_LOGD(AAFwkTag::APPKIT, "the application isSystemApp: %{public}d", isSystemApp);
#ifdef CJ_FRONTEND
    if (isCJApp) {
        AbilityRuntime::CJRuntime::SetAppLibPath(appLibPaths);
        if (appInfo.asanEnabled) {
            AbilityRuntime::CJRuntime::SetAsanVersion();
        } else if (appInfo.tsanEnabled) {
            AbilityRuntime::CJRuntime::SetTsanVersion();
        }
    } else {
#endif
        AbilityRuntime::JsRuntime::SetAppLibPath(appLibPaths, isSystemApp);
#ifdef CJ_FRONTEND
    }
#endif

    if (isStageBased) {
        // Create runtime
        auto hapPath = entryHapModuleInfo.hapPath;
        auto moduleName = entryHapModuleInfo.moduleName;
        AbilityRuntime::Runtime::Options options;
        options.bundleName = appInfo.bundleName;
        options.codePath = LOCAL_CODE_PATH;
        options.hapPath = hapPath;
        options.moduleName = moduleName;
        options.eventRunner = mainHandler_->GetEventRunner();
        options.loadAce = true;
        options.isBundle = (entryHapModuleInfo.compileMode != AppExecFwk::CompileMode::ES_MODULE);
        options.isDebugVersion = bundleInfo.applicationInfo.debug;
        options.arkNativeFilePath = bundleInfo.applicationInfo.arkNativeFilePath;
        options.uid = bundleInfo.applicationInfo.uid;
        options.apiTargetVersion = appInfo.apiTargetVersion;
        options.pkgContextInfoJsonStringMap = pkgContextInfoJsonStringMap;
#ifdef CJ_FRONTEND
        options.lang = isCJApp ? AbilityRuntime::Runtime::Language::CJ : AbilityRuntime::Runtime::Language::JS;
#endif
        if (applicationInfo_->appProvisionType == Constants::APP_PROVISION_TYPE_DEBUG) {
            TAG_LOGD(AAFwkTag::APPKIT, "multi-thread mode: %{public}d", appLaunchData.GetMultiThread());
            options.isMultiThread = appLaunchData.GetMultiThread();
            TAG_LOGD(AAFwkTag::JSRUNTIME, "Start Error-Info-Enhance Mode: %{public}d.",
                appLaunchData.GetErrorInfoEnhance());
            options.isErrorInfoEnhance = appLaunchData.GetErrorInfoEnhance();
        }
        options.jitEnabled = appLaunchData.IsJITEnabled();
        AbilityRuntime::ChildProcessManager::GetInstance().SetForkProcessJITEnabled(appLaunchData.IsJITEnabled());
        TAG_LOGD(AAFwkTag::APPKIT, "isStartWithDebug:%{public}d, debug:%{public}d, isNativeStart:%{public}d",
            appLaunchData.GetDebugApp(), appInfo.debug, appLaunchData.isNativeStart());
        AbilityRuntime::ChildProcessManager::GetInstance().SetForkProcessDebugOption(appInfo.bundleName,
            appLaunchData.GetDebugApp(), appInfo.debug, appLaunchData.isNativeStart());
        if (!bundleInfo.hapModuleInfos.empty()) {
            for (auto hapModuleInfo : bundleInfo.hapModuleInfos) {
                options.hapModulePath[hapModuleInfo.moduleName] = hapModuleInfo.hapPath;
                options.packageNameList[hapModuleInfo.moduleName] = hapModuleInfo.packageName;
            }
        }
        auto runtime = AbilityRuntime::Runtime::Create(options);
        if (!runtime) {
            TAG_LOGE(AAFwkTag::APPKIT, "Failed to create runtime");
            return;
        }

        if (appInfo.debug && appLaunchData.GetDebugApp()) {
            wptr<MainThread> weak = this;
            auto cb = [weak]() {
                auto appThread = weak.promote();
                if (appThread == nullptr) {
                    TAG_LOGE(AAFwkTag::APPKIT, "appThread is nullptr");
                    return false;
                }
                return appThread->NotifyDeviceDisConnect();
            };
            runtime->SetDeviceDisconnectCallback(cb);
        }
        auto perfCmd = appLaunchData.GetPerfCmd();
        int32_t pid = -1;
        std::string processName = "";
        if (processInfo_ != nullptr) {
            pid = processInfo_->GetPid();
            processName = processInfo_->GetProcessName();
            TAG_LOGD(AAFwkTag::APPKIT, "pid is %{public}d, processName is %{public}s", pid, processName.c_str());
        }
        AbilityRuntime::Runtime::DebugOption debugOption;
        debugOption.isStartWithDebug = appLaunchData.GetDebugApp();
        debugOption.processName = processName;
        debugOption.isDebugApp = appInfo.debug;
        debugOption.isStartWithNative = appLaunchData.isNativeStart();
        if (perfCmd.find(PERFCMD_PROFILE) != std::string::npos ||
            perfCmd.find(PERFCMD_DUMPHEAP) != std::string::npos) {
            TAG_LOGD(AAFwkTag::APPKIT, "perfCmd is %{public}s", perfCmd.c_str());
            debugOption.perfCmd = perfCmd;
            runtime->StartProfiler(debugOption);
        } else {
            runtime->StartDebugMode(debugOption);
        }

        std::vector<HqfInfo> hqfInfos = appInfo.appQuickFix.deployedAppqfInfo.hqfInfos;
        std::map<std::string, std::string> modulePaths;
        if (!hqfInfos.empty()) {
            for (auto it = hqfInfos.begin(); it != hqfInfos.end(); it++) {
                TAG_LOGI(AAFwkTag::APPKIT, "moudelName: %{private}s, hqfFilePath: %{private}s",
                    it->moduleName.c_str(), it->hqfFilePath.c_str());
                modulePaths.insert(std::make_pair(it->moduleName, it->hqfFilePath));
            }
            runtime->RegisterQuickFixQueryFunc(modulePaths);
        }

        auto bundleName = appInfo.bundleName;
        auto versionCode = appInfo.versionCode;
#ifdef CJ_FRONTEND
        if (!isCJApp) {
#endif
            JsEnv::UncaughtExceptionInfo uncaughtExceptionInfo;
            uncaughtExceptionInfo.hapPath = hapPath;
            wptr<MainThread> weak = this;
            uncaughtExceptionInfo.uncaughtTask = [weak, bundleName, versionCode, appRunningId = std::move(appRunningId),
                pid, processName] (std::string summary, const JsEnv::ErrorObject errorObj) {
                auto appThread = weak.promote();
                if (appThread == nullptr) {
                    TAG_LOGE(AAFwkTag::APPKIT, "appThread is nullptr");
                    return;
                }
                time_t timet;
                time(&timet);
                HiSysEventWrite(OHOS::HiviewDFX::HiSysEvent::Domain::AAFWK, "JS_ERROR",
                    OHOS::HiviewDFX::HiSysEvent::EventType::FAULT,
                    EVENT_KEY_PACKAGE_NAME, bundleName,
                    EVENT_KEY_VERSION, std::to_string(versionCode),
                    EVENT_KEY_TYPE, JSCRASH_TYPE,
                    EVENT_KEY_HAPPEN_TIME, timet,
                    EVENT_KEY_REASON, errorObj.name,
                    EVENT_KEY_JSVM, JSVM_TYPE,
                    EVENT_KEY_SUMMARY, summary,
                    EVENT_KEY_APP_RUNING_UNIQUE_ID, appRunningId);
                ErrorObject appExecErrorObj = {
                    .name = errorObj.name,
                    .message = errorObj.message,
                    .stack = errorObj.stack
                };
                FaultData faultData;
                faultData.faultType = FaultDataType::JS_ERROR;
                faultData.errorObject = appExecErrorObj;
                DelayedSingleton<AppExecFwk::AppMgrClient>::GetInstance()->NotifyAppFault(faultData);
                int result = HiSysEventWrite(HiviewDFX::HiSysEvent::Domain::FRAMEWORK, "PROCESS_KILL",
                    HiviewDFX::HiSysEvent::EventType::FAULT, "PID", pid, "PROCESS_NAME", processName,
                    "MSG", KILL_REASON);
                TAG_LOGI(AAFwkTag::APPKIT, "hisysevent write result=%{public}d, send event [FRAMEWORK,PROCESS_KILL],"
                    " pid=%{public}d, processName=%{public}s, msg=%{public}s", result, pid, processName.c_str(),
                    KILL_REASON);
    
                if (ApplicationDataManager::GetInstance().NotifyUnhandledException(summary) &&
                    ApplicationDataManager::GetInstance().NotifyExceptionObject(appExecErrorObj)) {
                    return;
                }
                // if app's callback has been registered, let app decide whether exit or not.
                TAG_LOGE(AAFwkTag::APPKIT,
                    "\n%{public}s is about to exit due to RuntimeError\nError type:%{public}s\n%{public}s",
                    bundleName.c_str(), errorObj.name.c_str(), summary.c_str());
                AAFwk::ExitReason exitReason = { REASON_JS_ERROR, errorObj.name };
                AbilityManagerClient::GetInstance()->RecordAppExitReason(exitReason);
                _exit(JS_ERROR_EXIT);
            };
            (static_cast<AbilityRuntime::JsRuntime&>(*runtime)).RegisterUncaughtExceptionHandler(uncaughtExceptionInfo);
#ifdef CJ_FRONTEND
        } else {
            auto expectionInfo = CreateCjExceptionInfo(bundleName, versionCode, hapPath);
            (static_cast<AbilityRuntime::CJRuntime&>(*runtime)).RegisterUncaughtExceptionHandler(expectionInfo);
        }
#endif

        application_->SetRuntime(std::move(runtime));

        std::weak_ptr<OHOSApplication> wpApplication = application_;
        AbilityLoader::GetInstance().RegisterUIAbility("UIAbility",
            [wpApplication]() -> AbilityRuntime::UIAbility* {
            auto app = wpApplication.lock();
            if (app != nullptr) {
                return AbilityRuntime::UIAbility::Create(app->GetRuntime());
            }
            TAG_LOGE(AAFwkTag::APPKIT, "failed");
            return nullptr;
        });
#ifdef CJ_FRONTEND
        if (!isCJApp) {
#endif
            auto& jsEngine = (static_cast<AbilityRuntime::JsRuntime&>(*application_->GetRuntime())).GetNativeEngine();
            if (application_ != nullptr) {
                LoadAllExtensions(jsEngine);
            }

            IdleTimeCallback callback = [wpApplication](int32_t idleTime) {
                auto app = wpApplication.lock();
                if (app == nullptr) {
                    TAG_LOGE(AAFwkTag::APPKIT, "app is nullptr");
                    return;
                }
                auto &runtime = app->GetRuntime();
                if (runtime == nullptr) {
                    TAG_LOGE(AAFwkTag::APPKIT, "runtime is nullptr");
                    return;
                }
                auto& nativeEngine = (static_cast<AbilityRuntime::JsRuntime&>(*runtime)).GetNativeEngine();
                nativeEngine.NotifyIdleTime(idleTime);
            };
            idleTime_ = std::make_shared<IdleTime>(mainHandler_, callback);
            idleTime_->Start();

            IdleNotifyStatusCallback cb = idleTime_->GetIdleNotifyFunc();
            jsEngine.NotifyIdleStatusControl(cb);

            auto helper = std::make_shared<DumpRuntimeHelper>(application_);
            helper->SetAppFreezeFilterCallback();
        }
#ifdef CJ_FRONTEND
    }
#endif

    auto usertestInfo = appLaunchData.GetUserTestInfo();
    if (usertestInfo) {
        if (!PrepareAbilityDelegator(usertestInfo, isStageBased, entryHapModuleInfo)) {
            TAG_LOGE(AAFwkTag::APPKIT, "Failed to prepare ability delegator");
            return;
        }
    }

    // init resourceManager.

    auto moduleName = entryHapModuleInfo.moduleName;
    std::string loadPath =
        entryHapModuleInfo.hapPath.empty() ? entryHapModuleInfo.resourcePath : entryHapModuleInfo.hapPath;
    std::regex inner_pattern(std::string(ABS_CODE_PATH) + std::string(FILE_SEPARATOR) + bundleInfo.name);
    loadPath = std::regex_replace(loadPath, inner_pattern, LOCAL_CODE_PATH);
    std::vector<OverlayModuleInfo> overlayModuleInfos;
    auto res = GetOverlayModuleInfos(bundleInfo.name, moduleName, overlayModuleInfos);
    std::vector<std::string> overlayPaths;
    if (res == ERR_OK) {
        overlayPaths = GetAddOverlayPaths(overlayModuleInfos);
    }
    std::unique_ptr<Global::Resource::ResConfig> resConfig(Global::Resource::CreateResConfig());
    int32_t appType;
    if (bundleInfo.applicationInfo.codePath == std::to_string(TYPE_RESERVE)) {
        appType = TYPE_RESERVE;
    } else if (bundleInfo.applicationInfo.codePath == std::to_string(TYPE_OTHERS)) {
        appType = TYPE_OTHERS;
    } else {
        appType = 0;
    }
    std::shared_ptr<Global::Resource::ResourceManager> resourceManager(Global::Resource::CreateResourceManager(
        bundleInfo.name, moduleName, loadPath, overlayPaths, *resConfig, appType));

    if (resourceManager == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "create resourceManager failed");
        return;
    }

    Configuration appConfig = config;
    ParseAppConfigurationParams(bundleInfo.applicationInfo.configuration, appConfig);
    std::string systemSizeScale = appConfig.GetItem(AAFwk::GlobalConfigurationKey::APP_FONT_SIZE_SCALE);
    if (!systemSizeScale.empty() && systemSizeScale.compare(DEFAULT_APP_FONT_SIZE_SCALE) == 0) {
        appConfig.AddItem(AAFwk::GlobalConfigurationKey::SYSTEM_FONT_SIZE_SCALE, SYSTEM_DEFAULT_FONTSIZE_SCALE);
    }

    if (!InitResourceManager(resourceManager, entryHapModuleInfo, bundleInfo.name,
        bundleInfo.applicationInfo.multiProjects, appConfig)) {
        TAG_LOGE(AAFwkTag::APPKIT, "InitResourceManager failed");
        return;
    }
    contextImpl->SetResourceManager(resourceManager);
    AbilityBase::ExtractResourceManager::GetExtractResourceManager().SetGlobalObject(resourceManager);

    contextDeal->initResourceManager(resourceManager);
    contextDeal->SetApplicationContext(application_);
    application_->AttachBaseContext(contextDeal);
    application_->SetAbilityRecordMgr(abilityRecordMgr_);
    application_->SetConfiguration(appConfig);
    contextImpl->SetConfiguration(application_->GetConfiguration());

    applicationImpl_->SetRecordId(appLaunchData.GetRecordId());
    applicationImpl_->SetApplication(application_);
    mainThreadState_ = MainThreadState::READY;
    if (!applicationImpl_->PerformAppReady()) {
        TAG_LOGE(AAFwkTag::APPKIT, "applicationImpl_->PerformAppReady failed");
        return;
    }
    // L1 needs to add corresponding interface
    ApplicationEnvImpl *pAppEvnIml = ApplicationEnvImpl::GetInstance();

    if (pAppEvnIml) {
        pAppEvnIml->SetAppInfo(*applicationInfo_.get());
    } else {
        TAG_LOGE(AAFwkTag::APPKIT, "pAppEvnIml is null");
    }

#if defined(NWEB)
    // start nwebspawn process
    std::weak_ptr<OHOSApplication> weakApp = application_;
    wptr<IAppMgr> weakMgr = appMgr_;
    std::thread([weakApp, weakMgr] {
        auto app = weakApp.lock();
        auto appmgr = weakMgr.promote();
        if (app == nullptr || appmgr == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "app or appmgr is null");
            return;
        }

        if (prctl(PR_SET_NAME, "preStartNWeb") < 0) {
            TAG_LOGW(AAFwkTag::APPKIT, "Set thread name failed with %{public}d", errno);
        }

        std::string nwebPath = app->GetAppContext()->GetCacheDir() + "/web";
        bool isFirstStartUpWeb = (access(nwebPath.c_str(), F_OK) != 0);
        if (!isFirstStartUpWeb) {
            appmgr->PreStartNWebSpawnProcess();
        }
        OHOS::NWeb::NWebHelper::TryPreReadLib(isFirstStartUpWeb, app->GetAppContext()->GetBundleCodeDir());
    }).detach();
#endif
}

#ifdef ABILITY_LIBRARY_LOADER
void MainThread::CalcNativeLiabraryEntries(const BundleInfo &bundleInfo, std::string &nativeLibraryPath)
{
    bool loadSoFromDir = bundleInfo.hapModuleInfos.empty();
    std::vector<std::string> nativeFileEntries;
    for (const auto &item: bundleInfo.hapModuleInfos) {
        if (!item.compressNativeLibs) {
            TAG_LOGD(AAFwkTag::APPKIT, "handle entries for: %{public}s, with path: %{public}s",
                item.moduleName.c_str(), item.nativeLibraryPath.c_str());
            if (item.nativeLibraryPath.empty()) {
                TAG_LOGD(AAFwkTag::APPKIT, "nativeLibraryPath empty: %{public}s", item.moduleName.c_str());
                continue;
            }
            std::string libPath = GetLibPath(item.hapPath, bundleInfo.isPreInstallApp);
            libPath += (libPath.back() == '/') ? item.nativeLibraryPath : "/" + item.nativeLibraryPath;
            TAG_LOGI(AAFwkTag::APPKIT, "module lib path: %{public}s", libPath.c_str());
            if (libPath.back() != '/') {
                libPath.push_back('/');
            }
            for (const auto &entryName : item.nativeLibraryFileNames) {
                TAG_LOGD(AAFwkTag::APPKIT, "add entry: %{public}s", entryName.c_str());
                nativeFileEntries.emplace_back(libPath + entryName);
            }
        } else {
            TAG_LOGD(AAFwkTag::APPKIT, "compressNativeLibs flag true for: %{public}s", item.moduleName.c_str());
            loadSoFromDir = true;
        }
    }

    if (loadSoFromDir) {
        if (nativeLibraryPath.empty()) {
            TAG_LOGW(AAFwkTag::APPKIT, "Native library path is empty");
            return;
        }

        if (nativeLibraryPath.back() == '/') {
            nativeLibraryPath.pop_back();
        }
        std::string libPath = LOCAL_CODE_PATH;
        libPath += (libPath.back() == '/') ? nativeLibraryPath : "/" + nativeLibraryPath;
        TAG_LOGD(AAFwkTag::APPKIT, "native library path = %{public}s", libPath.c_str());

        if (!ScanDir(libPath, nativeFileEntries_)) {
            TAG_LOGW(AAFwkTag::APPKIT, "scanDir %{public}s not exits", libPath.c_str());
        }
    }

    if (!nativeFileEntries.empty()) {
        nativeFileEntries_.insert(nativeFileEntries_.end(), nativeFileEntries.begin(), nativeFileEntries.end());
    }
}

void MainThread::LoadNativeLiabrary(const BundleInfo &bundleInfo, std::string &nativeLibraryPath)
{
    CalcNativeLiabraryEntries(bundleInfo, nativeLibraryPath);
    if (nativeFileEntries_.empty()) {
        TAG_LOGW(AAFwkTag::APPKIT, "No native library");
        return;
    }

    void *handleAbilityLib = nullptr;
    for (auto fileEntry : nativeFileEntries_) {
        if (fileEntry.empty()) {
            continue;
        }
        handleAbilityLib = dlopen(fileEntry.c_str(), RTLD_NOW | RTLD_GLOBAL);
        if (handleAbilityLib == nullptr) {
            if (fileEntry.find("libformrender.z.so") == std::string::npos) {
                TAG_LOGE(AAFwkTag::APPKIT, "fail to dlopen %{public}s, [%{public}s]",
                    fileEntry.c_str(), dlerror());
                exit(-1);
            } else {
                TAG_LOGD(AAFwkTag::APPKIT, "Load libformrender.z.so from native lib path.");
                handleAbilityLib = dlopen(FORM_RENDER_LIB_PATH, RTLD_NOW | RTLD_GLOBAL);
                if (handleAbilityLib == nullptr) {
                    TAG_LOGE(AAFwkTag::APPKIT, "fail to dlopen %{public}s, [%{public}s]",
                        FORM_RENDER_LIB_PATH, dlerror());
                    exit(-1);
                }
                fileEntry = FORM_RENDER_LIB_PATH;
            }
        }
        TAG_LOGD(AAFwkTag::APPKIT, "success to dlopen %{public}s", fileEntry.c_str());
        handleAbilityLib_.emplace_back(handleAbilityLib);
    }
}
#endif

void MainThread::ChangeToLocalPath(const std::string &bundleName,
    const std::vector<std::string> &sourceDirs, std::vector<std::string> &localPath)
{
    std::regex pattern(std::string(ABS_CODE_PATH) + std::string(FILE_SEPARATOR) + bundleName
        + std::string(FILE_SEPARATOR));
    for (auto item : sourceDirs) {
        if (item.empty()) {
            continue;
        }
        localPath.emplace_back(
            std::regex_replace(item, pattern, std::string(LOCAL_CODE_PATH) + std::string(FILE_SEPARATOR)));
    }
}

void MainThread::ChangeToLocalPath(const std::string &bundleName,
    const std::string &sourceDir, std::string &localPath)
{
    std::regex pattern(std::string(ABS_CODE_PATH) + std::string(FILE_SEPARATOR) + bundleName);
    if (sourceDir.empty()) {
        return;
    }
    if (std::regex_search(localPath, std::regex(bundleName))) {
        localPath = std::regex_replace(localPath, pattern, std::string(LOCAL_CODE_PATH));
    } else {
        localPath = std::regex_replace(localPath, std::regex(ABS_CODE_PATH), LOCAL_BUNDLES);
    }
}

void MainThread::HandleUpdateApplicationInfoInstalled(const ApplicationInfo &appInfo)
{
    TAG_LOGD(AAFwkTag::APPKIT, "called");
    if (!application_) {
        TAG_LOGE(AAFwkTag::APPKIT, "application_ is nullptr");
        return;
    }
    application_->UpdateApplicationInfoInstalled(appInfo);

    if (!appMgr_ || !applicationImpl_) {
        TAG_LOGE(AAFwkTag::APPKIT, "appMgr_ is nullptr");
        return;
    }
}

void MainThread::HandleAbilityStage(const HapModuleInfo &abilityStage)
{
    TAG_LOGD(AAFwkTag::APPKIT, "called");
    if (!application_) {
        TAG_LOGE(AAFwkTag::APPKIT, "application_ is nullptr");
        return;
    }

    application_->AddAbilityStage(abilityStage);

    if (!appMgr_ || !applicationImpl_) {
        TAG_LOGE(AAFwkTag::APPKIT, "appMgr_ is nullptr");
        return;
    }

    appMgr_->AddAbilityStageDone(applicationImpl_->GetRecordId());
}

void MainThread::LoadAllExtensions(NativeEngine &nativeEngine)
{
    HITRACE_METER_NAME(HITRACE_TAG_APP, __PRETTY_FUNCTION__);
    TAG_LOGD(AAFwkTag::APPKIT, "LoadAllExtensions");
    if (!extensionConfigMgr_) {
        TAG_LOGE(AAFwkTag::APPKIT, "ExtensionConfigMgr is invalid");
        return;
    }

    auto extensionPlugins = AbilityRuntime::ExtensionPluginInfo::GetInstance().GetExtensionPlugins();
    if (extensionPlugins.empty()) {
        TAG_LOGE(AAFwkTag::APPKIT, "no extension type map");
        return;
    }

    std::map<int32_t, std::string> extensionTypeMap;
    for (auto& item : extensionPlugins) {
        extensionTypeMap.insert(std::pair<int32_t, std::string>(item.extensionType, item.extensionName));
        AddExtensionBlockItem(item.extensionName, item.extensionType);

        std::string file = item.extensionLibFile;
        std::weak_ptr<OHOSApplication> wApp = application_;
        AbilityLoader::GetInstance().RegisterExtension(item.extensionName,
            [wApp, file]() -> AbilityRuntime::Extension* {
            auto app = wApp.lock();
            if (app != nullptr) {
                return AbilityRuntime::ExtensionModuleLoader::GetLoader(file.c_str()).Create(app->GetRuntime());
            }
            TAG_LOGE(AAFwkTag::APPKIT, "failed.");
            return nullptr;
        });
    }
    application_->SetExtensionTypeMap(extensionTypeMap);
}

bool MainThread::PrepareAbilityDelegator(const std::shared_ptr<UserTestRecord> &record, bool isStageBased,
    const AppExecFwk::HapModuleInfo &entryHapModuleInfo)
{
    TAG_LOGD(AAFwkTag::APPKIT, "enter, isStageBased = %{public}d", isStageBased);
    if (!record) {
        TAG_LOGE(AAFwkTag::APPKIT, "Invalid UserTestRecord");
        return false;
    }
    auto args = std::make_shared<AbilityDelegatorArgs>(record->want);
    if (isStageBased) { // Stage model
        TAG_LOGD(AAFwkTag::APPKIT, "Stage model");
        auto testRunner = TestRunner::Create(application_->GetRuntime(), args, false);
        auto delegator = std::make_shared<AbilityDelegator>(
            application_->GetAppContext(), std::move(testRunner), record->observer);
        AbilityDelegatorRegistry::RegisterInstance(delegator, args);
        delegator->Prepare();
    } else { // FA model
        TAG_LOGD(AAFwkTag::APPKIT, "FA model");
        AbilityRuntime::Runtime::Options options;
        options.codePath = LOCAL_CODE_PATH;
        options.eventRunner = mainHandler_->GetEventRunner();
        options.hapPath = entryHapModuleInfo.hapPath;
        options.loadAce = false;
        options.isStageModel = false;
        options.isTestFramework = true;
        if (applicationInfo_) {
            options.apiTargetVersion = applicationInfo_->apiTargetVersion;
        }
        if (entryHapModuleInfo.abilityInfos.empty()) {
            TAG_LOGE(AAFwkTag::APPKIT, "Failed to abilityInfos");
            return false;
        }
        bool isFaJsModel = entryHapModuleInfo.abilityInfos.front().srcLanguage == "js" ? true : false;
        static auto runtime = AbilityRuntime::Runtime::Create(options);
        auto testRunner = TestRunner::Create(runtime, args, isFaJsModel);
        if (testRunner == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "Failed to Create testRunner");
            return false;
        }
        if (!testRunner->Initialize()) {
            TAG_LOGE(AAFwkTag::APPKIT, "Failed to Initialize testRunner");
            return false;
        }
        auto delegator = std::make_shared<AbilityDelegator>(
            application_->GetAppContext(), std::move(testRunner), record->observer);
        AbilityDelegatorRegistry::RegisterInstance(delegator, args);
        delegator->Prepare();
    }
    return true;
}

/**
 *
 * @brief launch the ability.
 *
 * @param abilityRecord The abilityRecord which belongs to the ability launched.
 *
 */
void MainThread::HandleLaunchAbility(const std::shared_ptr<AbilityLocalRecord> &abilityRecord)
{
    TAG_LOGD(AAFwkTag::APPKIT, "called");
    CHECK_POINTER_LOG(abilityRecord, "parameter(abilityRecord) is null");
    std::string connector = "##";
    std::string traceName = __PRETTY_FUNCTION__ + connector;
    if (abilityRecord->GetWant() != nullptr) {
        traceName += abilityRecord->GetWant()->GetElement().GetBundleName();
    } else {
        TAG_LOGE(AAFwkTag::APPKIT, "Want is nullptr, cant not get abilityName");
    }
    HITRACE_METER_NAME(HITRACE_TAG_APP, traceName);
    CHECK_POINTER_LOG(applicationImpl_, "applicationImpl_ is null");
    CHECK_POINTER_LOG(abilityRecordMgr_, "abilityRecordMgr_ is null");

    auto abilityToken = abilityRecord->GetToken();
    CHECK_POINTER_LOG(abilityToken, "abilityRecord->GetToken failed");
    FreezeUtil::LifecycleFlow flow = { abilityToken, FreezeUtil::TimeoutState::LOAD };
    std::string entry = std::to_string(AbilityRuntime::TimeUtil::SystemTimeMillisecond()) +
        "; MainThread::HandleLaunchAbility; the load lifecycle.";
    FreezeUtil::GetInstance().AddLifecycleEvent(flow, entry);

    abilityRecordMgr_->SetToken(abilityToken);
    abilityRecordMgr_->AddAbilityRecord(abilityToken, abilityRecord);

    if (!IsApplicationReady()) {
        TAG_LOGE(AAFwkTag::APPKIT, "should launch application first");
        return;
    }

    if (!CheckAbilityItem(abilityRecord)) {
        TAG_LOGE(AAFwkTag::APPKIT, "record is invalid");
        return;
    }

    mainThreadState_ = MainThreadState::RUNNING;
    wptr<MainThread> weak = this;
    auto callback = [weak, abilityRecord](const std::shared_ptr<AbilityRuntime::Context> &stageContext) {
        auto appThread = weak.promote();
        if (appThread == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "abilityThread is nullptr");
            return;
        }
        appThread->SetProcessExtensionType(abilityRecord);
        auto application = appThread->GetApplication();
        if (application == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "application is nullptr");
            return;
        }
        auto& runtime = application->GetRuntime();
        appThread->UpdateRuntimeModuleChecker(runtime);
#ifdef APP_ABILITY_USE_TWO_RUNNER
        AbilityThread::AbilityThreadMain(application, abilityRecord, stageContext);
#else
        AbilityThread::AbilityThreadMain(application, abilityRecord, mainHandler_->GetEventRunner(), stageContext);
#endif
    };
    bool isAsyncCallback = false;
    std::shared_ptr<AbilityRuntime::Context> stageContext = application_->AddAbilityStage(
        abilityRecord, callback, isAsyncCallback);
    if (isAsyncCallback) {
        return;
    }
    SetProcessExtensionType(abilityRecord);
    auto& runtime = application_->GetRuntime();
    UpdateRuntimeModuleChecker(runtime);
#ifdef APP_ABILITY_USE_TWO_RUNNER
    AbilityThread::AbilityThreadMain(application_, abilityRecord, stageContext);
#else
    AbilityThread::AbilityThreadMain(application_, abilityRecord, mainHandler_->GetEventRunner(), stageContext);
#endif
}

/**
 *
 * @brief Clean the ability but don't notify ams.
 *
 * @param token The token which belongs to the ability launched.
 *
 */
void MainThread::HandleCleanAbilityLocal(const sptr<IRemoteObject> &token)
{
    TAG_LOGD(AAFwkTag::APPKIT, "start");
    if (!IsApplicationReady()) {
        TAG_LOGE(AAFwkTag::APPKIT, "should launch application first");
        return;
    }

    if (token == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "token is null");
        return;
    }

    std::shared_ptr<AbilityLocalRecord> record = abilityRecordMgr_->GetAbilityItem(token);
    if (record == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "abilityRecord not found");
        return;
    }
    std::shared_ptr<AbilityInfo> abilityInfo = record->GetAbilityInfo();
    if (abilityInfo == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "record->GetAbilityInfo() failed");
        return;
    }
    TAG_LOGD(AAFwkTag::APPKIT, "ability name: %{public}s", abilityInfo->name.c_str());

    abilityRecordMgr_->RemoveAbilityRecord(token);
    application_->CleanAbilityStage(token, abilityInfo, false);
#ifdef APP_ABILITY_USE_TWO_RUNNER
    std::shared_ptr<EventRunner> runner = record->GetEventRunner();
    if (runner != nullptr) {
        int ret = runner->Stop();
        if (ret != ERR_OK) {
            TAG_LOGE(AAFwkTag::APPKIT, "MainThread::main failed. ability runner->Run failed ret = %{public}d", ret);
        }
        abilityRecordMgr_->RemoveAbilityRecord(token);
        application_->CleanAbilityStage(token, abilityInfo, false);
    } else {
        TAG_LOGW(AAFwkTag::APPKIT, "runner not found");
    }
#endif
}

/**
 *
 * @brief Clean the ability.
 *
 * @param token The token which belongs to the ability launched.
 *
 */
void MainThread::HandleCleanAbility(const sptr<IRemoteObject> &token, bool isCacheProcess)
{
    HITRACE_METER_NAME(HITRACE_TAG_APP, __PRETTY_FUNCTION__);
    if (applicationInfo_ == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "applicationInfo is null");
        return;
    }
    TAG_LOGD(AAFwkTag::APPKIT, "Handle clean ability start, app is %{public}s.", applicationInfo_->name.c_str());

    if (!IsApplicationReady()) {
        TAG_LOGE(AAFwkTag::APPKIT, "should launch application first");
        return;
    }

    if (token == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "token is null");
        return;
    }

    std::shared_ptr<AbilityLocalRecord> record = abilityRecordMgr_->GetAbilityItem(token);
    if (record == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "abilityRecord not found");
        return;
    }
    std::shared_ptr<AbilityInfo> abilityInfo = record->GetAbilityInfo();
    if (abilityInfo == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "record->GetAbilityInfo() failed");
        return;
    }

#ifdef SUPPORT_GRAPHICS
    if (abilityInfo->type == AbilityType::PAGE && abilityInfo->isStageBasedModel) {
        AppRecovery::GetInstance().RemoveAbility(token);
    }
#endif

    abilityRecordMgr_->RemoveAbilityRecord(token);
    application_->CleanAbilityStage(token, abilityInfo, isCacheProcess);
#ifdef APP_ABILITY_USE_TWO_RUNNER
    std::shared_ptr<EventRunner> runner = record->GetEventRunner();
    if (runner != nullptr) {
        int ret = runner->Stop();
        if (ret != ERR_OK) {
            TAG_LOGE(AAFwkTag::APPKIT, "MainThread::main failed. ability runner->Run failed ret = %{public}d", ret);
        }
        abilityRecordMgr_->RemoveAbilityRecord(token);
        application_->CleanAbilityStage(token, abilityInfo, isCacheProcess);
    } else {
        TAG_LOGW(AAFwkTag::APPKIT, "runner not found");
    }
#endif
    appMgr_->AbilityCleaned(token);
    TAG_LOGD(AAFwkTag::APPKIT, "end. app: %{public}s, ability: %{public}s.",
        applicationInfo_->name.c_str(), abilityInfo->name.c_str());
}

/**
 *
 * @brief Foreground the application.
 *
 */
void MainThread::HandleForegroundApplication()
{
    HITRACE_METER_NAME(HITRACE_TAG_APP, __PRETTY_FUNCTION__);
    TAG_LOGI(AAFwkTag::APPKIT, "called");
    if ((application_ == nullptr) || (appMgr_ == nullptr)) {
        TAG_LOGE(AAFwkTag::APPKIT, "handleForegroundApplication error!");
        return;
    }

    if (!applicationImpl_->PerformForeground()) {
        TAG_LOGE(AAFwkTag::APPKIT, "applicationImpl_->PerformForeground() failed");
    }

    // Start accessing PurgeableMem if the event of foreground is successful.
#ifdef IMAGE_PURGEABLE_PIXELMAP
    PurgeableMem::PurgeableResourceManager::GetInstance().BeginAccessPurgeableMem();
#endif

    TAG_LOGD(AAFwkTag::APPKIT, "to foreground success, recordId is %{public}d", applicationImpl_->GetRecordId());
    appMgr_->ApplicationForegrounded(applicationImpl_->GetRecordId());
}

/**
 *
 * @brief Background the application.
 *
 */
void MainThread::HandleBackgroundApplication()
{
    HITRACE_METER_NAME(HITRACE_TAG_APP, __PRETTY_FUNCTION__);
    TAG_LOGD(AAFwkTag::APPKIT, "start");

    if ((application_ == nullptr) || (appMgr_ == nullptr)) {
        TAG_LOGE(AAFwkTag::APPKIT, "error");
        return;
    }

    if (!applicationImpl_->PerformBackground()) {
        TAG_LOGE(AAFwkTag::APPKIT, "applicationImpl_->PerformBackground() failed");
    }

    // End accessing PurgeableMem if the event of background is successful.
#ifdef IMAGE_PURGEABLE_PIXELMAP
    PurgeableMem::PurgeableResourceManager::GetInstance().EndAccessPurgeableMem();
#endif

    appMgr_->ApplicationBackgrounded(applicationImpl_->GetRecordId());
}

/**
 *
 * @brief Terminate the application.
 *
 * @param isLastProcess When it is the last application process, pass in true.
 */
void MainThread::HandleTerminateApplication(bool isLastProcess)
{
    HITRACE_METER_NAME(HITRACE_TAG_APP, __PRETTY_FUNCTION__);
    TAG_LOGD(AAFwkTag::APPKIT, "start");
    if ((application_ == nullptr) || (appMgr_ == nullptr)) {
        TAG_LOGE(AAFwkTag::APPKIT, "error");
        return;
    }

    if (!applicationImpl_->PerformTerminate(isLastProcess)) {
        TAG_LOGD(AAFwkTag::APPKIT, "PerformTerminate() failed");
    }

    std::shared_ptr<EventRunner> runner = mainHandler_->GetEventRunner();
    if (runner == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "get manHandler error");
        return;
    }

    if (watchdog_ != nullptr && !watchdog_->IsStopWatchdog()) {
        watchdog_->Stop();
        watchdog_ = nullptr;
    }

    int ret = runner->Stop();
    if (ret != ERR_OK) {
        TAG_LOGE(AAFwkTag::APPKIT, "runner->Run failed ret = %{public}d", ret);
    }
    SetRunnerStarted(false);
    appMgr_->ApplicationTerminated(applicationImpl_->GetRecordId());
}

/**
 *
 * @brief Shrink the memory which used by application.
 *
 * @param level Indicates the memory trim level, which shows the current memory usage status.
 *
 */
void MainThread::HandleShrinkMemory(const int level)
{
    HITRACE_METER_NAME(HITRACE_TAG_APP, __PRETTY_FUNCTION__);

    if (applicationImpl_ == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "applicationImpl_ is null");
        return;
    }

    applicationImpl_->PerformMemoryLevel(level);
}

/**
 *
 * @brief Handle NotifyMemoryLevel.
 *
 * @param level Indicates the memory trim level, which shows the current memory usage status.
 *
 */
void MainThread::HandleMemoryLevel(int level)
{
    HITRACE_METER_NAME(HITRACE_TAG_APP, __PRETTY_FUNCTION__);
    TAG_LOGD(AAFwkTag::APPKIT, "start");

    if (application_ == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "application_ is null");
        return;
    }

    application_->OnMemoryLevel(level);
}

/**
 *
 * @brief send the new config to the application.
 *
 * @param config The updated config.
 *
 */
void MainThread::HandleConfigurationUpdated(const Configuration &config)
{
    HITRACE_METER_NAME(HITRACE_TAG_APP, __PRETTY_FUNCTION__);
    if (applicationImpl_ == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "applicationImpl_ is null");
        return;
    }

    applicationImpl_->PerformConfigurationUpdated(config);
}

void MainThread::TaskTimeoutDetected(const std::shared_ptr<EventRunner> &runner)
{
    HITRACE_METER_NAME(HITRACE_TAG_APP, __PRETTY_FUNCTION__);
    TAG_LOGD(AAFwkTag::APPKIT, "start");

    auto deliveryTimeoutCallback = []() {
        TAG_LOGD(AAFwkTag::APPKIT, "delivery timeout");
    };
    auto distributeTimeoutCallback = []() {
        TAG_LOGD(AAFwkTag::APPKIT, "distribute timeout");
    };

    if (runner !=nullptr && mainHandler_ != nullptr) {
        runner->SetDeliveryTimeout(DELIVERY_TIME);
        mainHandler_->SetDeliveryTimeoutCallback(deliveryTimeoutCallback);

        runner->SetDistributeTimeout(DISTRIBUTE_TIME);
        mainHandler_->SetDistributeTimeoutCallback(distributeTimeoutCallback);
    }
}

void MainThread::Init(const std::shared_ptr<EventRunner> &runner)
{
    HITRACE_METER_NAME(HITRACE_TAG_APP, __PRETTY_FUNCTION__);
    TAG_LOGD(AAFwkTag::APPKIT, "Start");
    mainHandler_ = std::make_shared<MainHandler>(runner, this);
    watchdog_ = std::make_shared<Watchdog>();
    extensionConfigMgr_ = std::make_unique<AbilityRuntime::ExtensionConfigMgr>();
    wptr<MainThread> weak = this;
    auto task = [weak]() {
        auto appThread = weak.promote();
        if (appThread == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "abilityThread is nullptr");
            return;
        }
        appThread->SetRunnerStarted(true);
    };
    if (!mainHandler_->PostTask(task, "MainThread:SetRunnerStarted")) {
        TAG_LOGE(AAFwkTag::APPKIT, "PostTask task failed");
    }
    TaskTimeoutDetected(runner);

    watchdog_->Init(mainHandler_);
    AppExecFwk::AppfreezeInner::GetInstance()->SetMainHandler(mainHandler_);
    extensionConfigMgr_->Init();
}

void MainThread::HandleSignal(int signal, [[maybe_unused]] siginfo_t *siginfo, void *context)
{
    if (signal != MUSL_SIGNAL_JSHEAP) {
        TAG_LOGE(AAFwkTag::APPKIT, "signal is %{public}d", signal);
        return;
    }
    TAG_LOGI(AAFwkTag::APPKIT, "sival_int is %{public}d", siginfo->si_value.sival_int);
    if (static_cast<SignalType>(siginfo->si_value.sival_int) != SignalType::SIGNAL_FORCE_FULLGC) {
        HandleDumpHeapPrepare();
    }
    switch (static_cast<SignalType>(siginfo->si_value.sival_int)) {
        case SignalType::SIGNAL_JSHEAP_OLD: {
            auto heapFunc = []() { return MainThread::HandleDumpHeap(false); };
            mainHandler_->PostTask(heapFunc, "MainThread::SIGNAL_JSHEAP_OLD");
            break;
        }
        case SignalType::SIGNAL_JSHEAP: {
            auto heapFunc = []() { return MainThread::HandleDumpHeap(false); };
            mainHandler_->PostTask(heapFunc, "MainThread::SIGNAL_JSHEAP");
            break;
        }
        case SignalType::SIGNAL_JSHEAP_PRIV: {
            auto privateHeapFunc = []() { return MainThread::HandleDumpHeap(true); };
            mainHandler_->PostTask(privateHeapFunc, "MainThread:SIGNAL_JSHEAP_PRIV");
            break;
        }
        case SignalType::SIGNAL_NO_TRIGGERID: {
            auto heapFunc = []() { return MainThread::HandleDumpHeap(false); };
            mainHandler_->PostTask(heapFunc, "MainThread::SIGNAL_JSHEAP");

            auto noTriggerIdFunc = []() { MainThread::DestroyHeapProfiler(); };
            mainHandler_->PostTask(noTriggerIdFunc, "MainThread::SIGNAL_NO_TRIGGERID");
            break;
        }
        case SignalType::SIGNAL_NO_TRIGGERID_PRIV: {
            auto privateHeapFunc = []() { return MainThread::HandleDumpHeap(true); };
            mainHandler_->PostTask(privateHeapFunc, "MainThread:SIGNAL_JSHEAP_PRIV");

            auto noTriggerIdFunc = []() { MainThread::DestroyHeapProfiler(); };
            mainHandler_->PostTask(noTriggerIdFunc, "MainThread::SIGNAL_NO_TRIGGERID_PRIV");
            break;
        }
        case SignalType::SIGNAL_FORCE_FULLGC: {
            auto forceFullGCFunc = []() { MainThread::ForceFullGC(); };
            ffrt::submit(forceFullGCFunc);
            break;
        }
        default:
            break;
    }
}

void MainThread::HandleDumpHeapPrepare()
{
    TAG_LOGD(AAFwkTag::APPKIT, "start");
    if (mainHandler_ == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "mainHandler is nullptr");
        return;
    }
    auto app = applicationForDump_.lock();
    if (app == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "app is nullptr");
        return;
    }
    auto &runtime = app->GetRuntime();
    if (runtime == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "runtime is nullptr");
        return;
    }
    runtime->GetHeapPrepare();
}

void MainThread::HandleDumpHeap(bool isPrivate)
{
    TAG_LOGD(AAFwkTag::APPKIT, "start");
    if (mainHandler_ == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "mainHandler is nullptr");
        return;
    }
    auto app = applicationForDump_.lock();
    if (app == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "app is nullptr");
        return;
    }
    auto &runtime = app->GetRuntime();
    if (runtime == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "runtime is nullptr");
        return;
    }
    auto taskFork = [&runtime, &isPrivate] {
        TAG_LOGD(AAFwkTag::APPKIT, "HandleDump Heap taskFork start");
        time_t startTime = time(nullptr);
        int pid = -1;
        if ((pid = fork()) < 0) {
            TAG_LOGE(AAFwkTag::APPKIT, "HandleDumpHeap Fork error, err:%{public}d", errno);
            return;
        }
        if (pid == 0) {
            runtime->AllowCrossThreadExecution();
            runtime->DumpHeapSnapshot(isPrivate);
            TAG_LOGI(AAFwkTag::APPKIT, "HandleDumpHeap successful, now you can check some file");
            _exit(0);
        }
        while (true) {
            int status = 0;
            pid_t p = waitpid(pid, &status, 0);
            if (p < 0) {
                TAG_LOGE(AAFwkTag::APPKIT, "HandleDumpHeap waitpid return p=%{public}d, err:%{public}d", p, errno);
                break;
            }
            if (p == pid) {
                TAG_LOGE(AAFwkTag::APPKIT, "HandleDumpHeap dump process exited status is %{public}d", status);
                break;
            }
            if (time(nullptr) > startTime + TIME_OUT) {
                TAG_LOGE(AAFwkTag::APPKIT, "time out to wait childprocess, killing forkpid %{public}d", pid);
                kill(pid, SIGKILL);
                break;
            }
            usleep(DEFAULT_SLEEP_TIME);
        }
    };

    ffrt::submit(taskFork, {}, {}, ffrt::task_attr().qos(ffrt::qos_user_initiated));
    runtime->DumpCpuProfile();
}

void MainThread::DestroyHeapProfiler()
{
    TAG_LOGD(AAFwkTag::APPKIT, "called");
    if (mainHandler_ == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "mainHandler is nullptr");
        return;
    }

    auto task = [] {
        auto app = applicationForDump_.lock();
        if (app == nullptr || app->GetRuntime() == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "runtime is nullptr");
            return;
        }
        app->GetRuntime()->DestroyHeapProfiler();
    };
    mainHandler_->PostTask(task, "MainThread:DestroyHeapProfiler");
}

void MainThread::ForceFullGC()
{
    TAG_LOGD(AAFwkTag::APPKIT, "Force fullGC");
    if (mainHandler_ == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "mainHandler is nullptr");
        return;
    }

    auto task = [] {
        auto app = applicationForDump_.lock();
        if (app == nullptr || app->GetRuntime() == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "runtime is nullptr");
            return;
        }
        app->GetRuntime()->ForceFullGC();
    };
    mainHandler_->PostTask(task, "MainThread:ForceFullGC");
}

void MainThread::Start()
{
    HITRACE_METER_NAME(HITRACE_TAG_APP, __PRETTY_FUNCTION__);
    TAG_LOGI(AAFwkTag::APPKIT, "App main thread create, pid:%{public}d", getprocpid());

    std::shared_ptr<EventRunner> runner = EventRunner::GetMainEventRunner();
    if (runner == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "runner is nullptr");
        return;
    }
    sptr<MainThread> thread = sptr<MainThread>(new (std::nothrow) MainThread());
    if (thread == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "new MainThread failed");
        return;
    }

    struct sigaction sigAct;
    sigemptyset(&sigAct.sa_mask);
    sigAct.sa_flags = SA_SIGINFO;
    sigAct.sa_sigaction = &MainThread::HandleSignal;
    sigaction(MUSL_SIGNAL_JSHEAP, &sigAct, NULL);

    thread->Init(runner);

    thread->Attach();

    int ret = runner->Run();
    if (ret != ERR_OK) {
        TAG_LOGE(AAFwkTag::APPKIT, "runner->Run failed ret = %{public}d", ret);
    }

    thread->RemoveAppMgrDeathRecipient();
}

void MainThread::StartChild(const std::map<std::string, int32_t> &fds)
{
    TAG_LOGI(AAFwkTag::APPKIT, "MainThread StartChild, fds size:%{public}zu", fds.size());
    ChildMainThread::Start(fds);
}

void MainThread::PreloadExtensionPlugin()
{
    AbilityRuntime::ExtensionPluginInfo::GetInstance().Preload();
}

MainThread::MainHandler::MainHandler(const std::shared_ptr<EventRunner> &runner, const sptr<MainThread> &thread)
    : AppExecFwk::EventHandler(runner), mainThreadObj_(thread)
{}

/**
 *
 * @brief Process the event.
 *
 * @param event the event want to be processed.
 *
 */
void MainThread::MainHandler::ProcessEvent(const OHOS::AppExecFwk::InnerEvent::Pointer &event)
{
    auto eventId = event->GetInnerEventId();
    if (eventId == CHECK_MAIN_THREAD_IS_ALIVE) {
        auto mt = mainThreadObj_.promote();
        if (mt != nullptr) {
            mt->CheckMainThreadIsAlive();
        }
    }
}

/**
 *
 * @brief Check whether the OHOSApplication is ready.
 *
 * @return if the record is legal, return true. else return false.
 *
 */
bool MainThread::IsApplicationReady() const
{
    TAG_LOGD(AAFwkTag::APPKIT, "start");
    if (application_ == nullptr || applicationImpl_ == nullptr) {
        TAG_LOGW(AAFwkTag::APPKIT, "application_=null or applicationImpl_=null");
        return false;
    }

    return true;
}

#ifdef ABILITY_LIBRARY_LOADER
/**
 *
 * @brief Load the ability library.
 *
 * @param libraryPaths the library paths.
 *
 */
void MainThread::LoadAbilityLibrary(const std::vector<std::string> &libraryPaths)
{
    HITRACE_METER_NAME(HITRACE_TAG_APP, __PRETTY_FUNCTION__);
#ifdef ABILITY_LIBRARY_LOADER
    TAG_LOGD(AAFwkTag::APPKIT, "start");
#ifdef SUPPORT_GRAPHICS
    LoadAceAbilityLibrary();
#endif
    size_t size = libraryPaths.size();
    for (size_t index = 0; index < size; index++) {
        std::string libraryPath = libraryPaths[index];
        TAG_LOGD(AAFwkTag::APPKIT, "Try to scanDir %{public}s", libraryPath.c_str());
        if (!ScanDir(libraryPath, fileEntries_)) {
            TAG_LOGW(AAFwkTag::APPKIT, "scanDir %{public}s not exits", libraryPath.c_str());
        }
        libraryPath = libraryPath + "/libs";
        if (!ScanDir(libraryPath, fileEntries_)) {
            TAG_LOGW(AAFwkTag::APPKIT, "scanDir %{public}s not exits", libraryPath.c_str());
        }
    }

    if (fileEntries_.empty()) {
        TAG_LOGD(AAFwkTag::APPKIT, "No ability library");
        return;
    }

    char resolvedPath[PATH_MAX] = {0};
    void *handleAbilityLib = nullptr;
    for (const auto& fileEntry : fileEntries_) {
        if (fileEntry.empty() || fileEntry.size() >= PATH_MAX) {
            continue;
        }
        if (realpath(fileEntry.c_str(), resolvedPath) == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "Failed to get realpath, errno = %{public}d", errno);
            continue;
        }

        handleAbilityLib = dlopen(resolvedPath, RTLD_NOW | RTLD_GLOBAL);
        if (handleAbilityLib == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "Fail to dlopen %{public}s, [%{public}s]",
                resolvedPath, dlerror());
            exit(-1);
        }
        TAG_LOGI(AAFwkTag::APPKIT, "Success to dlopen %{public}s", fileEntry.c_str());
        handleAbilityLib_.emplace_back(handleAbilityLib);
    }
#endif  // ABILITY_LIBRARY_LOADER
}

void MainThread::LoadAceAbilityLibrary()
{
    void *AceAbilityLib = nullptr;
    const char *path = Ace::AceForwardCompatibility::GetAceLibName();
    AceAbilityLib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (AceAbilityLib == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "Fail to dlopen %{public}s, [%{public}s]", path, dlerror());
    } else {
        TAG_LOGD(AAFwkTag::APPKIT, "Success to dlopen %{public}s", path);
        handleAbilityLib_.emplace_back(AceAbilityLib);
    }
}

void MainThread::LoadAppLibrary()
{
    HITRACE_METER_NAME(HITRACE_TAG_APP, __PRETTY_FUNCTION__);
#ifdef APPLICATION_LIBRARY_LOADER
    std::string appPath = applicationLibraryPath;
    TAG_LOGI(AAFwkTag::APPKIT, "calling dlopen. appPath=%{public}s", appPath.c_str());
    handleAppLib_ = dlopen(appPath.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (handleAppLib_ == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "Fail to dlopen %{public}s, [%{public}s]", appPath.c_str(), dlerror());
        exit(-1);
    }
#endif  // APPLICATION_LIBRARY_LOADER
}

void MainThread::LoadAppDetailAbilityLibrary(std::string &nativeLibraryPath)
{
    HITRACE_METER_NAME(HITRACE_TAG_APP, __PRETTY_FUNCTION__);
#ifdef ABILITY_LIBRARY_LOADER
    TAG_LOGD(AAFwkTag::APPKIT, "try to scanDir %{public}s", nativeLibraryPath.c_str());
    std::vector<std::string> fileEntries;
    if (!ScanDir(nativeLibraryPath, fileEntries)) {
        TAG_LOGW(AAFwkTag::APPKIT, "scanDir %{public}s not exits", nativeLibraryPath.c_str());
    }
    if (fileEntries.empty()) {
        TAG_LOGW(AAFwkTag::APPKIT, "No ability library");
        return;
    }
    char resolvedPath[PATH_MAX] = {0};
    void *handleAbilityLib = nullptr;
    for (const auto& fileEntry : fileEntries) {
        if (fileEntry.empty() || fileEntry.size() >= PATH_MAX) {
            continue;
        }
        if (realpath(fileEntry.c_str(), resolvedPath) == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "Failed to get realpath, errno = %{public}d", errno);
            continue;
        }

        handleAbilityLib = dlopen(resolvedPath, RTLD_NOW | RTLD_GLOBAL);
        if (handleAbilityLib == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "Fail to dlopen %{public}s, [%{public}s]",
                resolvedPath, dlerror());
            exit(-1);
        }
        TAG_LOGI(AAFwkTag::APPKIT, "Success to dlopen %{public}s", fileEntry.c_str());
        handleAbilityLib_.emplace_back(handleAbilityLib);
    }
#endif // ABILITY_LIBRARY_LOADER
}

bool MainThread::ScanDir(const std::string &dirPath, std::vector<std::string> &files)
{
    DIR *dirp = opendir(dirPath.c_str());
    if (dirp == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "MainThread::ScanDir open dir:%{public}s fail", dirPath.c_str());
        return false;
    }
    struct dirent *df = nullptr;
    for (;;) {
        df = readdir(dirp);
        if (df == nullptr) {
            break;
        }

        std::string currentName(df->d_name);
        if (currentName.compare(".") == 0 || currentName.compare("..") == 0) {
            continue;
        }

        if (CheckFileType(currentName, abilityLibraryType_)) {
            files.emplace_back(dirPath + pathSeparator_ + currentName);
        }
    }

    if (closedir(dirp) == -1) {
        TAG_LOGW(AAFwkTag::APPKIT, "close dir fail");
    }
    return true;
}

/**
 *
 * @brief Check the fileType.
 *
 * @param fileName The fileName of the lib.
 * @param extensionName The extensionName of the lib.
 *
 * @return if the FileType is legal, return true. else return false.
 *
 */
bool MainThread::CheckFileType(const std::string &fileName, const std::string &extensionName)
{
    TAG_LOGD(AAFwkTag::APPKIT, "path is %{public}s, support suffix is %{public}s",
        fileName.c_str(),
        extensionName.c_str());

    if (fileName.empty()) {
        TAG_LOGE(AAFwkTag::APPKIT, "the file name is empty");
        return false;
    }

    auto position = fileName.rfind('.');
    if (position == std::string::npos) {
        TAG_LOGW(AAFwkTag::APPKIT, "filename no extension name");
        return false;
    }

    std::string suffixStr = fileName.substr(position);
    return LowerStr(suffixStr) == extensionName;
}

void MainThread::HandleScheduleAcceptWant(const AAFwk::Want &want, const std::string &moduleName)
{
    TAG_LOGD(AAFwkTag::APPKIT, "called");
    if (!application_) {
        TAG_LOGE(AAFwkTag::APPKIT, "application_ is nullptr");
        return;
    }

    std::string specifiedFlag;
    application_->ScheduleAcceptWant(want, moduleName, specifiedFlag);

    if (!appMgr_ || !applicationImpl_) {
        TAG_LOGE(AAFwkTag::APPKIT, "appMgr_ is nullptr");
        return;
    }

    appMgr_->ScheduleAcceptWantDone(applicationImpl_->GetRecordId(), want, specifiedFlag);
}

void MainThread::ScheduleAcceptWant(const AAFwk::Want &want, const std::string &moduleName)
{
    TAG_LOGD(AAFwkTag::APPKIT, "start");
    wptr<MainThread> weak = this;
    auto task = [weak, want, moduleName]() {
        auto appThread = weak.promote();
        if (appThread == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "abilityThread is nullptr");
            return;
        }
        appThread->HandleScheduleAcceptWant(want, moduleName);
    };
    if (!mainHandler_->PostTask(task, "MainThread:AcceptWant")) {
        TAG_LOGE(AAFwkTag::APPKIT, "PostTask task failed");
    }
}

void MainThread::HandleScheduleNewProcessRequest(const AAFwk::Want &want, const std::string &moduleName)
{
    TAG_LOGD(AAFwkTag::APPKIT, "called");
    if (!application_) {
        TAG_LOGE(AAFwkTag::APPKIT, "application_ is nullptr");
        return;
    }

    std::string specifiedProcessFlag;
    application_->ScheduleNewProcessRequest(want, moduleName, specifiedProcessFlag);

    if (!appMgr_ || !applicationImpl_) {
        TAG_LOGE(AAFwkTag::APPKIT, "appMgr_ is nullptr");
        return;
    }

    appMgr_->ScheduleNewProcessRequestDone(applicationImpl_->GetRecordId(), want, specifiedProcessFlag);
}

void MainThread::ScheduleNewProcessRequest(const AAFwk::Want &want, const std::string &moduleName)
{
    TAG_LOGD(AAFwkTag::APPKIT, "start");
    wptr<MainThread> weak = this;
    auto task = [weak, want, moduleName]() {
        auto appThread = weak.promote();
        if (appThread == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "abilityThread is nullptr");
            return;
        }
        appThread->HandleScheduleNewProcessRequest(want, moduleName);
    };
    if (!mainHandler_->PostTask(task, "MainThread:ScheduleNewProcessRequest")) {
        TAG_LOGE(AAFwkTag::APPKIT, "PostTask task failed");
    }
}

void MainThread::CheckMainThreadIsAlive()
{
    auto tmpWatchdog = watchdog_;
    if (tmpWatchdog == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "Watch dog is nullptr.");
        return;
    }

    tmpWatchdog->SetAppMainThreadState(true);
    tmpWatchdog->AllowReportEvent();
    tmpWatchdog = nullptr;
}
#endif  // ABILITY_LIBRARY_LOADER

int32_t MainThread::ScheduleNotifyLoadRepairPatch(const std::string &bundleName,
    const sptr<IQuickFixCallback> &callback, const int32_t recordId)
{
    HITRACE_METER_NAME(HITRACE_TAG_ABILITY_MANAGER, __PRETTY_FUNCTION__);
    TAG_LOGD(AAFwkTag::APPKIT, "called");
    wptr<MainThread> weak = this;
    auto task = [weak, bundleName, callback, recordId]() {
        auto appThread = weak.promote();
        if (appThread == nullptr || appThread->application_ == nullptr || callback == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "ScheduleNotifyLoadRepairPatch, parameter is nullptr.");
            return;
        }

        bool ret = true;
        std::vector<std::pair<std::string, std::string>> hqfFilePair;
        if (appThread->GetHqfFileAndHapPath(bundleName, hqfFilePair)) {
            for (auto it = hqfFilePair.begin(); it != hqfFilePair.end(); it++) {
                TAG_LOGI(AAFwkTag::APPKIT, "hqfFile: %{private}s, hapPath: %{private}s",
                    it->first.c_str(), it->second.c_str());
                ret = appThread->application_->NotifyLoadRepairPatch(it->first, it->second);
            }
        } else {
            TAG_LOGD(AAFwkTag::APPKIT, "ScheduleNotifyLoadRepairPatch, There's no hqfFile need to load");
        }

        callback->OnLoadPatchDone(ret ? NO_ERROR : ERR_INVALID_OPERATION, recordId);
    };
    if (mainHandler_ == nullptr || !mainHandler_->PostTask(task, "MainThread:NotifyLoadRepairPatch")) {
        TAG_LOGE(AAFwkTag::APPKIT, "ScheduleNotifyLoadRepairPatch, Post task failed");
        return ERR_INVALID_VALUE;
    }

    return NO_ERROR;
}

int32_t MainThread::ScheduleNotifyHotReloadPage(const sptr<IQuickFixCallback> &callback, const int32_t recordId)
{
    HITRACE_METER_NAME(HITRACE_TAG_ABILITY_MANAGER, __PRETTY_FUNCTION__);
    TAG_LOGD(AAFwkTag::APPKIT, "called");
    wptr<MainThread> weak = this;
    auto task = [weak, callback, recordId]() {
        auto appThread = weak.promote();
        if (appThread == nullptr || appThread->application_ == nullptr || callback == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "parameter is nullptr");
            return;
        }
        auto ret = appThread->application_->NotifyHotReloadPage();
        callback->OnReloadPageDone(ret ? NO_ERROR : ERR_INVALID_OPERATION, recordId);
    };
    if (mainHandler_ == nullptr || !mainHandler_->PostTask(task, "MainThread:NotifyHotReloadPage")) {
        TAG_LOGE(AAFwkTag::APPKIT, "Post task failed");
        return ERR_INVALID_VALUE;
    }

    return NO_ERROR;
}

bool MainThread::GetHqfFileAndHapPath(const std::string &bundleName,
    std::vector<std::pair<std::string, std::string>> &fileMap)
{
    TAG_LOGD(AAFwkTag::APPKIT, "called");
    auto bundleMgrHelper = DelayedSingleton<BundleMgrHelper>::GetInstance();
    if (bundleMgrHelper == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "The bundleMgrHelper is nullptr");
        return false;
    }

    BundleInfo bundleInfo;
    if (bundleMgrHelper->GetBundleInfoForSelf(
        (static_cast<int32_t>(AppExecFwk::GetBundleInfoFlag::GET_BUNDLE_INFO_WITH_HAP_MODULE) +
        static_cast<int32_t>(AppExecFwk::GetBundleInfoFlag::GET_BUNDLE_INFO_WITH_ABILITY) +
        static_cast<int32_t>(AppExecFwk::GetBundleInfoFlag::GET_BUNDLE_INFO_WITH_APPLICATION) +
        static_cast<int32_t>(AppExecFwk::GetBundleInfoFlag::GET_BUNDLE_INFO_WITH_DISABLE) +
        static_cast<int32_t>(AppExecFwk::GetBundleInfoFlag::GET_BUNDLE_INFO_WITH_SIGNATURE_INFO) +
        static_cast<int32_t>(AppExecFwk::GetBundleInfoFlag::GET_BUNDLE_INFO_WITH_EXTENSION_ABILITY) +
        static_cast<int32_t>(AppExecFwk::GetBundleInfoFlag::GET_BUNDLE_INFO_WITH_METADATA)), bundleInfo) != ERR_OK) {
        TAG_LOGE(AAFwkTag::APPKIT, "Get bundle info of %{public}s failed", bundleName.c_str());
        return false;
    }

    for (auto hapInfo : bundleInfo.hapModuleInfos) {
        if ((processInfo_ != nullptr) && (processInfo_->GetProcessName() == hapInfo.process) &&
            (!hapInfo.hqfInfo.hqfFilePath.empty())) {
            std::string resolvedHapPath(AbilityBase::GetLoadPath(hapInfo.hapPath));
            std::string resolvedHqfFile(AbilityBase::GetLoadPath(hapInfo.hqfInfo.hqfFilePath));
            TAG_LOGD(AAFwkTag::APPKIT, "bundleName: %{public}s, moduleName: %{public}s, processName: %{private}s, "
                "hqf file: %{private}s, hap path: %{private}s", bundleName.c_str(), hapInfo.moduleName.c_str(),
                hapInfo.process.c_str(), resolvedHqfFile.c_str(), resolvedHapPath.c_str());
            fileMap.push_back(std::pair<std::string, std::string>(resolvedHqfFile, resolvedHapPath));
        }
    }

    return true;
}

int32_t MainThread::ScheduleNotifyUnLoadRepairPatch(const std::string &bundleName,
    const sptr<IQuickFixCallback> &callback, const int32_t recordId)
{
    HITRACE_METER_NAME(HITRACE_TAG_ABILITY_MANAGER, __PRETTY_FUNCTION__);
    TAG_LOGD(AAFwkTag::APPKIT, "called");
    wptr<MainThread> weak = this;
    auto task = [weak, bundleName, callback, recordId]() {
        auto appThread = weak.promote();
        if (appThread == nullptr || appThread->application_ == nullptr || callback == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, " parameter is nullptr");
            return;
        }

        bool ret = true;
        std::vector<std::pair<std::string, std::string>> hqfFilePair;
        if (appThread->GetHqfFileAndHapPath(bundleName, hqfFilePair)) {
            for (auto it = hqfFilePair.begin(); it != hqfFilePair.end(); it++) {
                TAG_LOGI(AAFwkTag::APPKIT, "hqfFile: %{private}s", it->first.c_str());
                ret = appThread->application_->NotifyUnLoadRepairPatch(it->first);
            }
        } else {
            TAG_LOGD(AAFwkTag::APPKIT, "ScheduleNotifyUnLoadRepairPatch, There's no hqfFile need to unload");
        }

        callback->OnUnloadPatchDone(ret ? NO_ERROR : ERR_INVALID_OPERATION, recordId);
    };
    if (mainHandler_ == nullptr || !mainHandler_->PostTask(task, "MainThread:NotifyUnLoadRepairPatch")) {
        TAG_LOGE(AAFwkTag::APPKIT, "ScheduleNotifyUnLoadRepairPatch, Post task failed");
        return ERR_INVALID_VALUE;
    }

    return NO_ERROR;
}

int32_t MainThread::ScheduleNotifyAppFault(const FaultData &faultData)
{
    if (mainHandler_ == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "mainHandler is nullptr");
        return ERR_INVALID_VALUE;
    }

    if (faultData.faultType == FaultDataType::APP_FREEZE) {
        return AppExecFwk::AppfreezeInner::GetInstance()->AppfreezeHandle(faultData, false);
    }

    wptr<MainThread> weak = this;
    auto task = [weak, faultData] {
        auto appThread = weak.promote();
        if (appThread == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "appThread is nullptr, NotifyAppFault failed");
            return;
        }
        appThread->NotifyAppFault(faultData);
    };
    mainHandler_->PostTask(task, "MainThread:NotifyAppFault");
    return NO_ERROR;
}

void MainThread::NotifyAppFault(const FaultData &faultData)
{
    if (faultData.notifyApp) {
        ErrorObject faultErrorObj = {
            .name = faultData.errorObject.name,
            .message = faultData.errorObject.message,
            .stack = faultData.errorObject.stack
        };
        ApplicationDataManager::GetInstance().NotifyExceptionObject(faultErrorObj);
    }
}

void MainThread::SetProcessExtensionType(const std::shared_ptr<AbilityLocalRecord> &abilityRecord)
{
    if (!extensionConfigMgr_) {
        TAG_LOGE(AAFwkTag::APPKIT, "extensionConfigMgr_ is null");
        return;
    }
    if (!abilityRecord) {
        TAG_LOGE(AAFwkTag::APPKIT, "abilityRecord is null");
        return;
    }
    if (!abilityRecord->GetAbilityInfo()) {
        TAG_LOGE(AAFwkTag::APPKIT, "abilityInfo is null");
        return;
    }
    TAG_LOGD(AAFwkTag::APPKIT, "type = %{public}d",
        static_cast<int32_t>(abilityRecord->GetAbilityInfo()->extensionAbilityType));
    extensionConfigMgr_->SetProcessExtensionType(
        static_cast<int32_t>(abilityRecord->GetAbilityInfo()->extensionAbilityType));
}

void MainThread::AddExtensionBlockItem(const std::string &extensionName, int32_t type)
{
    if (!extensionConfigMgr_) {
        TAG_LOGE(AAFwkTag::APPKIT, "extensionConfigMgr_ is null");
        return;
    }
    extensionConfigMgr_->AddBlockListItem(extensionName, type);
}

void MainThread::UpdateRuntimeModuleChecker(const std::unique_ptr<AbilityRuntime::Runtime> &runtime)
{
    if (!extensionConfigMgr_) {
        TAG_LOGE(AAFwkTag::APPKIT, "extensionConfigMgr_ is null");
        return;
    }
    extensionConfigMgr_->UpdateRuntimeModuleChecker(runtime);
}

int MainThread::GetOverlayModuleInfos(const std::string &bundleName, const std::string &moduleName,
    std::vector<OverlayModuleInfo> &overlayModuleInfos) const
{
    auto bundleMgrHelper = DelayedSingleton<BundleMgrHelper>::GetInstance();
    if (bundleMgrHelper == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "The bundleMgrHelper is nullptr");
        return ERR_INVALID_VALUE;
    }

    auto overlayMgrProxy = bundleMgrHelper->GetOverlayManagerProxy();
    if (overlayMgrProxy == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "The overlayMgrProxy is nullptr");
        return ERR_INVALID_VALUE;
    }

    auto ret = overlayMgrProxy->GetTargetOverlayModuleInfo(moduleName, overlayModuleInfos);
    if (ret != ERR_OK) {
        TAG_LOGE(AAFwkTag::APPKIT, "failed");
        return ret;
    }
    std::sort(overlayModuleInfos.begin(), overlayModuleInfos.end(),
        [](const OverlayModuleInfo& lhs, const OverlayModuleInfo& rhs) -> bool {
        return lhs.priority > rhs.priority;
    });
    TAG_LOGD(AAFwkTag::APPKIT, "the size of overlay is: %{public}zu", overlayModuleInfos.size());
    return ERR_OK;
}

std::vector<std::string> MainThread::GetAddOverlayPaths(const std::vector<OverlayModuleInfo> &overlayModuleInfos)
{
    std::vector<std::string> addPaths;
    for (auto it : overlayModuleInfos) {
        auto iter = std::find_if(
            overlayModuleInfos_.begin(), overlayModuleInfos_.end(), [it](OverlayModuleInfo item) {
                return it.moduleName == item.moduleName;
            });
        if ((iter != overlayModuleInfos_.end()) && (it.state == AppExecFwk::OverlayState::OVERLAY_ENABLE)) {
            iter->state = it.state;
            ChangeToLocalPath(iter->bundleName, iter->hapPath, iter->hapPath);
            TAG_LOGD(AAFwkTag::APPKIT, "add path:%{public}s", iter->hapPath.c_str());
            addPaths.emplace_back(iter->hapPath);
        }
    }
    return addPaths;
}

std::vector<std::string> MainThread::GetRemoveOverlayPaths(const std::vector<OverlayModuleInfo> &overlayModuleInfos)
{
    std::vector<std::string> removePaths;
    for (auto it : overlayModuleInfos) {
        auto iter = std::find_if(
            overlayModuleInfos_.begin(), overlayModuleInfos_.end(), [it](OverlayModuleInfo item) {
                return it.moduleName == item.moduleName;
            });
        if ((iter != overlayModuleInfos_.end()) && (it.state != AppExecFwk::OverlayState::OVERLAY_ENABLE)) {
            iter->state = it.state;
            ChangeToLocalPath(iter->bundleName, iter->hapPath, iter->hapPath);
            TAG_LOGD(AAFwkTag::APPKIT, "remove path:%{public}s", iter->hapPath.c_str());
            removePaths.emplace_back(iter->hapPath);
        }
    }

    return removePaths;
}

int32_t MainThread::ScheduleChangeAppGcState(int32_t state)
{
    TAG_LOGD(AAFwkTag::APPKIT, "called, state is %{public}d", state);
    if (mainHandler_ == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "mainHandler is nullptr");
        return ERR_INVALID_VALUE;
    }

    wptr<MainThread> weak = this;
    auto task = [weak, state] {
        auto appThread = weak.promote();
        if (appThread == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "appThread is nullptr, ChangeAppGcState failed.");
            return;
        }
        appThread->ChangeAppGcState(state);
    };

    if (state == START_HIGH_SENSITIVE || state == EXIT_HIGH_SENSITIVE) {
        ChangeAppGcState(state);
    } else {
        mainHandler_->PostTask(task, "MainThread:ChangeAppGcState");
    }
    return NO_ERROR;
}

int32_t MainThread::ChangeAppGcState(int32_t state)
{
    TAG_LOGD(AAFwkTag::APPKIT, "called");
    if (application_ == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "application_ is nullptr");
        return ERR_INVALID_VALUE;
    }
    auto &runtime = application_->GetRuntime();
    if (runtime == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "runtime is nullptr");
        return ERR_INVALID_VALUE;
    }
    if (runtime->GetLanguage() == AbilityRuntime::Runtime::Language::CJ) {
        return NO_ERROR;
    }
    auto& nativeEngine = (static_cast<AbilityRuntime::JsRuntime&>(*runtime)).GetNativeEngine();
    nativeEngine.NotifyForceExpandState(state);
    return NO_ERROR;
}

void MainThread::AttachAppDebug()
{
    TAG_LOGD(AAFwkTag::APPKIT, "called");
    SetAppDebug(AbilityRuntime::AppFreezeState::AppFreezeFlag::ATTACH_DEBUG_MODE, true);
}

void MainThread::DetachAppDebug()
{
    TAG_LOGD(AAFwkTag::APPKIT, "called");
    SetAppDebug(AbilityRuntime::AppFreezeState::AppFreezeFlag::ATTACH_DEBUG_MODE, false);
}

bool MainThread::NotifyDeviceDisConnect()
{
    TAG_LOGD(AAFwkTag::APPKIT, "called");
    bool isLastProcess = appMgr_->IsFinalAppProcess();
    ScheduleTerminateApplication(isLastProcess);
    return true;
}

void MainThread::AssertFaultPauseMainThreadDetection()
{
    TAG_LOGD(AAFwkTag::APPKIT, "called");
    SetAppDebug(AbilityRuntime::AppFreezeState::AppFreezeFlag::ASSERT_DEBUG_MODE, true);
    if (appMgr_ == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "appMgr is nullptr");
        return;
    }
    appMgr_->SetAppAssertionPauseState(true);
}

void MainThread::AssertFaultResumeMainThreadDetection()
{
    TAG_LOGD(AAFwkTag::APPKIT, "called");
    SetAppDebug(AbilityRuntime::AppFreezeState::AppFreezeFlag::ASSERT_DEBUG_MODE, false);
    if (appMgr_ == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "appMgr is nullptr");
        return;
    }
    appMgr_->SetAppAssertionPauseState(false);
}

void MainThread::HandleInitAssertFaultTask(bool isDebugModule, bool isDebugApp)
{
    if (!system::GetBoolParameter(PRODUCT_ASSERT_FAULT_DIALOG_ENABLED, false)) {
        TAG_LOGD(AAFwkTag::APPKIT, "Unsupport assert fault dialog");
        return;
    }
    if (!system::GetBoolParameter(DEVELOPER_MODE_STATE, false)) {
        TAG_LOGE(AAFwkTag::APPKIT, "Developer Mode is false");
        return;
    }
    if (!isDebugApp) {
        TAG_LOGE(AAFwkTag::APPKIT, "Non-debug version application");
        return;
    }
    auto assertThread = DelayedSingleton<AbilityRuntime::AssertFaultTaskThread>::GetInstance();
    if (assertThread == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "Get assert thread instance is nullptr");
        return;
    }
    assertThread->InitAssertFaultTask(this, isDebugModule);
    assertThread_ = assertThread;
}

void MainThread::SetAppDebug(uint32_t modeFlag, bool isDebug)
{
    TAG_LOGD(AAFwkTag::APPKIT, "called");
    auto state = DelayedSingleton<AbilityRuntime::AppFreezeState>::GetInstance();
    if (state == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "Get app freeze state instance is nullptr");
        return;
    }

    if (!isDebug) {
        TAG_LOGD(AAFwkTag::APPKIT, "Call Cancel modeFlag is %{public}u", modeFlag);
        state->CancelAppFreezeState(modeFlag);
        return;
    }

    TAG_LOGD(AAFwkTag::APPKIT, "Call Set modeFlag is %{public}u", modeFlag);
    state->SetAppFreezeState(modeFlag);
}

void MainThread::HandleCancelAssertFaultTask()
{
    auto assertThread = assertThread_.lock();
    if (assertThread == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "Get assert thread instance is nullptr");
        return;
    }
    assertThread->Stop();
}

int32_t MainThread::ScheduleDumpIpcStart(std::string& result)
{
    TAG_LOGD(AAFwkTag::APPKIT, "MainThread::ScheduleDumpIpcStart::pid:%{public}d", getprocpid());
    DumpIpcHelper::DumpIpcStart(result);
    return ERR_OK;
}

int32_t MainThread::ScheduleDumpIpcStop(std::string& result)
{
    TAG_LOGD(AAFwkTag::APPKIT, "pid:%{public}d", getprocpid());
    DumpIpcHelper::DumpIpcStop(result);
    return ERR_OK;
}

int32_t MainThread::ScheduleDumpIpcStat(std::string& result)
{
    TAG_LOGD(AAFwkTag::APPKIT, "pid:%{public}d", getprocpid());
    DumpIpcHelper::DumpIpcStat(result);
    return ERR_OK;
}

/**
 *
 * @brief Notify application to prepare for process caching.
 *
 */
void MainThread::ScheduleCacheProcess()
{
    TAG_LOGD(AAFwkTag::APPKIT, "ScheduleCacheProcess");
    wptr<MainThread> weak = this;
    auto task = [weak]() {
        auto appThread = weak.promote();
        if (appThread == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "appThread is nullptr");
            return;
        }
        appThread->HandleCacheProcess();
    };
    if (mainHandler_ == nullptr) {
        TAG_LOGE(AAFwkTag::APPKIT, "handler nullptr");
        return;
    }
    if (!mainHandler_->PostTask(task, "MainThread:ScheduleCacheProcess")) {
        TAG_LOGE(AAFwkTag::APPKIT, "PostTask task failed");
    }
}

void MainThread::ParseAppConfigurationParams(const std::string configuration, Configuration &appConfig)
{
    TAG_LOGD(AAFwkTag::APPKIT, "start");
    appConfig.AddItem(AAFwk::GlobalConfigurationKey::APP_FONT_SIZE_SCALE, DEFAULT_APP_FONT_SIZE_SCALE);
    if (configuration.empty()) {
        TAG_LOGE(AAFwkTag::ABILITYMGR, "emptye config");
        return;
    }
    nlohmann::json configurationJson = nlohmann::json::parse(configuration, nullptr, false);
    if (configurationJson.is_discarded()) {
        TAG_LOGE(AAFwkTag::ABILITYMGR, "discarded error");
        return;
    }
    if (!configurationJson.contains(JSON_KEY_APP_CONFIGURATION)) {
        TAG_LOGE(AAFwkTag::ABILITYMGR, "app config not exist");
        return;
    }
    nlohmann::json jsonObject = configurationJson.at(JSON_KEY_APP_CONFIGURATION).get<nlohmann::json>();
    if (jsonObject.empty()) {
        TAG_LOGE(AAFwkTag::ABILITYMGR, "null app config");
        return;
    }
    if (jsonObject.contains(JSON_KEY_APP_FONT_SIZE_SCALE)
        && jsonObject[JSON_KEY_APP_FONT_SIZE_SCALE].is_string()) {
        appConfig.AddItem(AAFwk::GlobalConfigurationKey::APP_FONT_SIZE_SCALE,
            jsonObject.at(JSON_KEY_APP_FONT_SIZE_SCALE).get<std::string>());
    }
    if (jsonObject.contains(JSON_KEY_APP_FONT_MAX_SCALE)
        && jsonObject[JSON_KEY_APP_FONT_MAX_SCALE].is_string()) {
        std::string appFontMaxScale = jsonObject.at(JSON_KEY_APP_FONT_MAX_SCALE).get<std::string>();
        const std::regex INTEGER_REGEX("^[-+]?([0-9]+)([.]([0-9]+))?$");
        if (std::regex_match(appFontMaxScale, INTEGER_REGEX)) {
            appConfig.AddItem(AAFwk::GlobalConfigurationKey::APP_FONT_MAX_SCALE, appFontMaxScale);
        }
    }
    TAG_LOGD(AAFwkTag::APPKIT, "configuration_: %{public}s", appConfig.GetName().c_str());
}

/**
 *
 * @brief Notify application to prepare for process caching.
 *
 */
void MainThread::HandleCacheProcess()
{
    HITRACE_METER_NAME(HITRACE_TAG_APP, __PRETTY_FUNCTION__);
    TAG_LOGD(AAFwkTag::APPKIT, "start");

    // force gc
    if (application_ != nullptr) {
        auto &runtime = application_->GetRuntime();
        if (runtime == nullptr) {
            TAG_LOGE(AAFwkTag::APPKIT, "null runtime");
            return;
        }
        runtime->ForceFullGC();
    }
}

int32_t MainThread::ScheduleDumpFfrt(std::string& result)
{
    TAG_LOGD(AAFwkTag::APPKIT, "MainThread::ScheduleDumpFfrt::pid:%{public}d", getprocpid());
    return DumpFfrtHelper::DumpFfrt(result);
}
}  // namespace AppExecFwk
}  // namespace OHOS
