#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <limits>
#include <climits>
#include <cwchar>
#include <iterator>
#include "wallet.h"

namespace ds {
constexpr DWORD Magic = 0x44535452;
constexpr DWORD Version = 19;
constexpr int MaxMultiplier = 1000000; // fixed point, 1000 = 1x; up to 1000x
constexpr int MaxGoldGain = 1000000;
constexpr int MaxSinPoints = 7 * 20;
constexpr int MinArtifactSlots = 6, MaxArtifactSlots = 50;
constexpr int MinInventorySlots = 6, MaxInventorySlots = 50, MinSkillSlots = 6, MaxSkillSlots = 50;
constexpr int NativeBuffSlots = 2, MaxBuffSlots = 6;
constexpr int MaxChallengeQuantity=20, MaxPendingChallengeRunes=2000;
enum Feature : LONG { God = 1, Damage = 2, Gold = 4, Reward = 8,
    Skins = 16, Pets = 32, Weapons = 64, Characters = 128, Sin = 256,
    InfiniteDash = 512, InfiniteJump = 1024, ArtifactCapacity = 2048,
    ArtifactInventoryCapacity = 4096, SkillCapacity = 8192,
    RunePowder = 16384, Diamond = 32768, SkillInventoryCapacity = 65536,
    RuneMaximum=131072, ChallengeRate=262144, ChallengeQuantity=524288,
    NoCooldown=1048576, AutoRetry=2097152, BuffCapacity=4194304, All=8388607 };
enum State : LONG { Waiting = 0, Ready = 1, Failed = -1 };
enum UnlockFailure : LONG { UnlockNone=0, UnlockManagerType, UnlockManagerInstance, UnlockManagerSkills, UnlockManagerArtifacts,
    UnlockSkillType, UnlockSkillLoader, UnlockSkillLoad, UnlockSkillField, UnlockSkillCatalog,
    UnlockArtifactType, UnlockArtifactLoader, UnlockArtifactLoad, UnlockArtifactField, UnlockArtifactCatalog,
    UnlockSkillList, UnlockSkillQuery, UnlockSkillSave, UnlockSkillAppend,
    UnlockArtifactList, UnlockArtifactQuery, UnlockArtifactSave, UnlockArtifactAppend,
    UnlockSaveFile, UnlockFileSync, UnlockTimeout, UnlockAdapterException, UnlockRoot };
inline const char* UnlockErrorLabel(LONG code) {
    static constexpr const char* labels[]={"无错误","解锁管理器类型未找到","解锁管理器尚未就绪","技能解锁列表尚未就绪","圣器解锁列表尚未就绪",
        "技能表类型未找到：SkillTable.SkillData","技能表 Load 方法未找到","技能表加载异常","技能表 SkillDataList 未就绪","技能表为空或条目无效",
        "圣器表类型未找到：ArtifactTable.ArtifactData","圣器表 Load 方法未找到","圣器表加载异常","圣器表 ArtifactDataList 未就绪","圣器表为空或条目无效",
        "技能解锁列表无效","技能解锁状态读取失败","技能解锁状态保存失败","技能掉落列表更新失败",
        "圣器解锁列表无效","圣器解锁状态读取失败","圣器解锁状态保存失败","圣器掉落列表更新失败",
        "本地存档对象未就绪","本地存档写入失败","等待游戏初始化超时","解锁适配器异常","解锁对象保护失败"};
    return code>=0 && size_t(code)<std::size(labels)?labels[code]:"未知解锁错误";
}
struct alignas(8) Shared {
    DWORD magic, version, size, gamePid;
    volatile LONG ownerPid, clientHeartbeat, runtimeHeartbeat;
    volatile LONG state, available, enabled;
    volatile LONG damageMultiplier, goldMultiplier, rewardMultiplier;
    volatile LONG runePowderMultiplier, diamondMultiplier;
    volatile LONG sinPoints, lastSinBefore, lastSinAfter;
    // Status: 0 native, 1 custom, 2 occupied tail, 3 open UI, -1 adapter error,
    // -2 load failure (original autosave paused until successful load/reset).
    volatile LONG artifactSlots, artifactPhysicalSlots, artifactAllowed, artifactEquipped, artifactStatus;
    volatile LONG inventorySlots, inventoryAllowed, inventoryOccupied, inventoryStatus;
    volatile LONG skillSlots, skillAllowed, skillEquipped, skillStatus;
    volatile LONG skillInventorySlots, skillInventoryAllowed, skillInventoryOccupied, skillInventoryStatus;
    volatile LONG buffAllowed, buffEquipped, buffStatus;
    volatile LONG errorCode;
    volatile LONG panelCommand, panelVisible, overlayState;
    // One-shot local unlock request: idle, queued, running, complete, failed.
    volatile LONG unlockCommand, unlockStatus, unlockedSkills, unlockedArtifacts, unlockRequestedAt, unlockError;
    volatile LONG challengeNormalChance,challengeBossChance,challengeQuantity;
    volatile LONG walletSelection,walletTargets[WalletCount];
    volatile LONG walletReadIndex,walletBalance,walletReadStatus;
    volatile LONG walletCommand,walletStatus,walletRequestIndex,walletRequestTarget,walletError;
    volatile LONG syncPending,syncRetries,syncError;
    alignas(8) volatile LONG64 runeMaxDraws,challengeExtraRunes;
    alignas(8) volatile LONG64 godHits, damageHits, goldHits, rewardHits;
    alignas(8) volatile LONG64 unlockHits, overlayFrames;
    alignas(8) volatile LONG64 dashRefills, jumpRefills;
    volatile LONG lastDamageBefore, lastDamageAfter, lastGoldBefore, lastGoldAfter;
    volatile LONG lastRewardBefore, lastRewardAfter;
    wchar_t message[256];
};
static_assert(sizeof(LONG) == 4 && sizeof(LONG64) == 8);
inline LONG Read(volatile LONG& value) { return InterlockedCompareExchange(&value, 0, 0); }
inline LONG64 Read64(volatile LONG64& value) { return InterlockedCompareExchange64(&value, 0, 0); }
inline void Set(volatile LONG& value, LONG next) { InterlockedExchange(&value, next); }
inline int NormalizeMultiplier(int value) { return std::clamp(value, 100, MaxMultiplier); }
inline bool Fresh(DWORD now, DWORD heartbeat) { return DWORD(now - heartbeat) < 3000; }
inline bool Active(Shared* state, Feature feature, DWORD now = GetTickCount()) {
    return state && Read(state->state) == Ready &&
        Fresh(now, DWORD(Read(state->clientHeartbeat))) &&
        (Read(state->enabled) & Read(state->available) & feature) != 0;
}
inline int ScalePositive(int value, int multiplier, int cap = INT_MAX) {
    if (value <= 0) return value;
    const int factor = NormalizeMultiplier(multiplier);
    const int64_t scaled = (int64_t(value) * factor + 500) / 1000;
    // Never reduce an existing above-cap amount when multiplying upward.
    const int bound = std::max(cap, value);
    return int(std::clamp<int64_t>(scaled, 1, bound));
}
inline int RewardRoom(int balance) { return INT_MAX - std::max(0, balance); }
inline int ClampRewardGain(int amount, int room) {
    return amount <= 0 ? amount : std::min(amount, std::max(0, room));
}
inline int ScaleReward(int value, int multiplier, int room = INT_MAX) {
    return ClampRewardGain(ScalePositive(value, multiplier), room);
}
inline void MappingName(wchar_t (&name)[96], DWORD pid) {
    swprintf_s(name, L"Local\\DungeonSlasherTrainer.v19.%lu", pid);
}
inline void Disable(Shared* state) { if (state) { Set(state->enabled, 0); Set(state->unlockCommand,0);InterlockedCompareExchange(&state->unlockStatus,0,1);Set(state->walletCommand,0);InterlockedCompareExchange(&state->walletStatus,0,1); } }
inline bool RequestWallet(Shared* state) {
    if(!state || Read(state->state)!=Ready || !Fresh(GetTickCount(),DWORD(Read(state->clientHeartbeat))))return false;
    const int index=Read(state->walletSelection);if(!ValidWallet(index))return false;
    const int target=Read(state->walletTargets[index]);if(target<0)return false;
    const LONG status=Read(state->walletStatus);if(status==1 || status==2 || InterlockedCompareExchange(&state->walletStatus,1,status)!=status)return false;
    Set(state->walletRequestIndex,index);Set(state->walletRequestTarget,target);Set(state->walletError,0);Set(state->walletCommand,1);return true;
}
inline const char* WalletErrorLabel(int code) {
    static constexpr const char* labels[]={"无错误","类型或余额无效","本局金币尚未就绪","原生余额读取失败","原生保存接口调用失败","原生保存后的余额不一致","本地存档写入失败；可按同一目标重试","本地存档尚未就绪","货币适配器异常"};
    return code>=0 && size_t(code)<std::size(labels)?labels[code]:"未知错误";
}
inline bool RequestUnlockAll(Shared* state) {
    if(!state || Read(state->state)!=Ready || !Fresh(GetTickCount(),DWORD(Read(state->clientHeartbeat))))return false;
    LONG status=Read(state->unlockStatus);
    if(status==1 || status==2 || InterlockedCompareExchange(&state->unlockStatus,1,status)!=status)return false;
    Set(state->unlockedSkills,0);Set(state->unlockedArtifacts,0);Set(state->unlockError,UnlockNone);Set(state->unlockRequestedAt,LONG(GetTickCount()));Set(state->unlockCommand,1);return true;
}
inline void SetFeature(Shared* state, Feature feature, bool enabled) {
    if (!state) return;
    if (enabled) InterlockedOr(&state->enabled, feature & Read(state->available));
    else InterlockedAnd(&state->enabled, ~LONG(feature));
}
inline void PanelKey(Shared* state, bool down, bool focused, bool& previous) {
    if (down && !previous && focused && state) Set(state->panelVisible, !Read(state->panelVisible));
    previous = down;
}
}
