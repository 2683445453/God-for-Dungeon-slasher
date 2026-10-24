#include "protocol.h"
#include "profile.h"
#include "MinHook.h"
#ifndef DS_RUNTIME_TEST
#include "overlay.h"
#endif
#include <cstdio>
#include <cstring>
#include <iterator>
#include <intrin.h>
#include <array>
#include <vector>
#include <string>
#include "artifact-plan.h"
#include "managed-runtime.h"

namespace {
ds::Shared* shared = nullptr;
uintptr_t gameBase = 0;
using Hit = void(__fastcall*)(void*, void*, const void*);
using EventHit = void(__fastcall*)(void*, int, bool, const void*);
using StatusHit = void(__fastcall*)(void*, int, const void*);
using HealthDamage = void(__fastcall*)(void*, int, const void*);
using DirectDamage = int(__fastcall*)(void*, int, void*, int, const void*);
using GoldGain = void(__fastcall*)(void*, int, bool, bool, const void*);
using RewardGain = int(__fastcall*)(void*, const void*);
// Nullable<Vector3> and Color are both 16-byte value types. The Windows x64
// ABI passes each via a pointer to the caller's value-type temporary.
using DamageFont = void(__fastcall*)(void*, int, bool, void*, void*, int, bool, float, const void*);
Hit originalHit = nullptr;
EventHit originalEventHit = nullptr;
StatusHit originalStatusHit = nullptr;
HealthDamage originalHealthDamage = nullptr;
DirectDamage originalDirectDamage = nullptr;
GoldGain originalGold = nullptr;
RewardGain originalSoul = nullptr, originalPrayer = nullptr, originalBreach = nullptr;
RewardGain originalSinLevel = nullptr;
using Instance = void*(__fastcall*)(const void*);
using GameAction = void(__fastcall*)(void*, const void*);
using DashAfter = void(__fastcall*)(void*, bool, const void*);
using MovementCheck = bool(__fastcall*)(void*, const void*);
// This build's 20-byte ObscuredInt is returned through an explicit result
// pointer in RCX; integer in EDX and MethodInfo in R8 (verified native callers).
struct ObscuredIntBytes { unsigned char bytes[20]; };
static_assert(sizeof(ObscuredIntBytes)==20);
using EncodeInt = ObscuredIntBytes*(__fastcall*)(ObscuredIntBytes*, int, const void*);
using DecodeInt = int(__fastcall*)(ObscuredIntBytes*, const void*);
GameAction originalMovementLateUpdate = nullptr;
DashAfter originalDashAfter = nullptr;
MovementCheck originalDashReady = nullptr;
RewardGain getMaxDashCount = nullptr;
EncodeInt encodeMovementInt = nullptr;
DecodeInt decodeMovementInt = nullptr;
GameAction refillWeaponCooldown = nullptr;
using SkillCooldown = float(__fastcall*)(void*,int,void*,void*,const void*);
using FloatQuery = float(__fastcall*)(void*,const void*);
struct ObscuredFloatBytes { unsigned char bytes[24]; };
using EncodeFloat = ObscuredFloatBytes*(__fastcall*)(ObscuredFloatBytes*,float,const void*);
EncodeFloat encodeCooldownFloat=nullptr;
SkillCooldown originalSkillCooldown=nullptr;
FloatQuery originalWeaponCooldown=nullptr;
float __fastcall OnSkillCooldown(void* self,int index,void* other,void* data,const void* method) {
    const float native=originalSkillCooldown(self,index,other,data,method);
    return ds::Active(shared,ds::NoCooldown) && std::isfinite(native) && native>0?0.0f:native;
}
float __fastcall OnWeaponCooldown(void* self,const void* method) {
    const float native=originalWeaponCooldown(self,method);
    return ds::Active(shared,ds::NoCooldown) && std::isfinite(native) && native>0?0.0f:native;
}
Instance getPlayerData = nullptr;
using MetadataInit = void(__fastcall*)(void*);
using CurrencySave = void(__fastcall*)(void*, int, bool, const void*);
enum class Currency : size_t { Prayer, Soul, Breach, Gem };
MetadataInit initializeMetadata = nullptr;
Instance getDataController = nullptr;
void* dataControllerMethodUsage = nullptr;
std::array<RewardGain,4> loadCurrency{};
std::array<CurrencySave,4> originalCurrencySave{};
GameAction originalSetReward = nullptr, originalGetReward = nullptr, originalClearDisable = nullptr, originalSinUpdate = nullptr;
void* rewardScreen = nullptr;
int displayedRewardFactor = 1000;
int displayedGemFactor = 1000;
DamageFont originalDamageFont = nullptr;
thread_local bool editingWallet=false;
using UnlockOne = bool(__fastcall*)(void*, int, const void*);
using UnlockTwo = bool(__fastcall*)(void*, int, int, const void*);
using CharacterAsset = void*(__fastcall*)(int, const void*);
using WeaponAsset = void*(__fastcall*)(void*, int, const void*);
UnlockTwo originalSkin = nullptr, originalWeapon = nullptr;
UnlockOne originalPet = nullptr, originalCharacter = nullptr, originalLobby = nullptr;
CharacterAsset getCharacter = nullptr, getPet = nullptr;
WeaponAsset getWeapon = nullptr;

// Asset layouts come from this exact build's metadata. Validate reads and reject
// missing, disabled and excluded content; never change the asset or ownership save.
template<class T> bool Field(void* object, size_t offset, T& value) {
    if (!object) return false;
    __try { memcpy(&value, static_cast<unsigned char*>(object) + offset, sizeof(T)); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T> bool WriteField(void* object, size_t offset, const T& value) {
    if (!object) return false;
    __try { memcpy(static_cast<unsigned char*>(object) + offset, &value, sizeof(T)); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void RefillMovement(void* self) {
    if(!self)return;
    if(ds::Active(shared,ds::NoCooldown) && refillWeaponCooldown) {
        float timer=0;int count=0;
        if(Field(self,0x620,timer) && Field(self,0x628,count) && (timer>0 || count<=0))
            refillWeaponCooldown(self,nullptr);
    }
    if(ds::Active(shared,ds::InfiniteDash) && getMaxDashCount) {
        int current=0;
        if(Field(self,0x630,current)) {
            const int maximum=getMaxDashCount(self,nullptr);
            if(maximum>0 && current<maximum && WriteField(self,0x630,maximum))InterlockedIncrement64(&shared->dashRefills);
        }
    }
    if(ds::Active(shared,ds::InfiniteJump) && encodeMovementInt && decodeMovementInt) {
        ObscuredIntBytes current{};
        if(Field(self,0x4c4,current) && decodeMovementInt(&current,nullptr)<=0) {
            ObscuredIntBytes refill{};
            // Construct a valid encrypted integer; never overwrite ciphertext
            // with a plain int or invoke the reset's eventInGround callbacks.
            if(encodeMovementInt(&refill,1,nullptr)==&refill && WriteField(self,0x4c4,refill))InterlockedIncrement64(&shared->jumpRefills);
        }
    }
}
void __fastcall OnMovementLateUpdate(void* self,const void* method) {
    originalMovementLateUpdate(self,method);
    RefillMovement(self); // Player fields only, on the game's main thread.
}
void __fastcall OnDashAfter(void* self,bool useCount,const void* method) {
    if(self && useCount && ds::Active(shared,ds::InfiniteDash))useCount=false;
    originalDashAfter(self,useCount,method); // Keep effects, callbacks and character-specific handling.
}
bool __fastcall OnDashReady(void* self,const void* method) {
    return self && ds::Active(shared,ds::InfiniteDash)?true:originalDashReady(self,method);
}
void* CharacterData(int character) {
    if (!getCharacter || character < 0 || character > 100000) return nullptr;
    void* asset = getCharacter(character, nullptr); bool disabled = true;
    int name = -1;
    return Field(asset, 0x18, name) && name == character && Field(asset, 0x1c, disabled) && !disabled ? asset : nullptr;
}
bool ValidSkin(int character, int skin) {
    void* asset = CharacterData(character); void* list = nullptr; void* items = nullptr;
    int size = 0; uintptr_t length = 0;
    if (skin < 0 || !Field(asset, 0x48, list) || !Field(list, 0x18, size) || skin >= size || size > 4096 ||
        !Field(list, 0x10, items) || !Field(items, 0x18, length) || uintptr_t(skin) >= length) return false;
    void* info = nullptr; bool cantUse = true;
    return Field(items, 0x20 + sizeof(void*) * skin, info) && Field(info, 0x1c, cantUse) && !cantUse;
}
bool ValidWeapon(int character, int weapon) {
    void* asset = CharacterData(character);
    if (!asset || !getWeapon || weapon < 0 || weapon > 100000) return false;
    void* info = getWeapon(asset, weapon, nullptr); bool cantUse = true, excluded = true; int id = -1;
    return Field(info, 0x18, id) && id == weapon && Field(info, 0x1c, cantUse) && !cantUse &&
        Field(info, 0x25, excluded) && !excluded;
}
bool ValidPet(int pet) {
    if (!getPet || pet < 0 || pet > 100000) return false;
    void* asset = getPet(pet, nullptr); bool disabled = true; int id = -1;
    return Field(asset, 0x18, id) && id == pet && Field(asset, 0x1d, disabled) && !disabled;
}
bool UnlockResult(bool owned, bool permitted) {
    if (!owned && permitted) { InterlockedIncrement64(&shared->unlockHits); return true; }
    return owned;
}
bool __fastcall OnSkin(void* self, int character, int skin, const void* method) {
    bool owned = originalSkin(self, character, skin, method);
    return UnlockResult(owned, !owned && ds::Active(shared, ds::Skins) && ValidSkin(character, skin));
}
bool __fastcall OnPet(void* self, int pet, const void* method) {
    bool owned = originalPet(self, pet, method);
    return UnlockResult(owned, !owned && ds::Active(shared, ds::Pets) && ValidPet(pet));
}
bool __fastcall OnWeapon(void* self, int character, int weapon, const void* method) {
    bool owned = originalWeapon(self, character, weapon, method);
    return UnlockResult(owned, !owned && ds::Active(shared, ds::Weapons) && ValidWeapon(character, weapon));
}
bool __fastcall OnCharacter(void* self, int character, const void* method) {
    bool owned = originalCharacter(self, character, method);
    return UnlockResult(owned, !owned && ds::Active(shared, ds::Characters) && CharacterData(character));
}
bool __fastcall OnLobby(void* self, int skin, const void* method) {
    bool owned = originalLobby(self, skin, method);
    return UnlockResult(owned, !owned && ds::Active(shared, ds::Skins) && skin >= -1 && skin <= 4);
}

void __fastcall OnHit(void* self, void* info, const void* method) {
    if (ds::Active(shared, ds::God)) { InterlockedIncrement64(&shared->godHits); return; }
    originalHit(self, info, method);
}
void __fastcall OnEventHit(void* self, int damage, bool force, const void* method) {
    if (damage > 0 && ds::Active(shared, ds::God)) { InterlockedIncrement64(&shared->godHits); return; }
    originalEventHit(self, damage, force, method);
}
void __fastcall OnStatusHit(void* self, int type, const void* method) {
    if (ds::Active(shared, ds::God)) { InterlockedIncrement64(&shared->godHits); return; }
    originalStatusHit(self, type, method);
}
int DamageValue(int damage) {
    if (damage <= 0 || !ds::Active(shared, ds::Damage)) return damage;
    int result = ds::ScalePositive(damage, ds::Read(shared->damageMultiplier));
    ds::Set(shared->lastDamageBefore, damage); ds::Set(shared->lastDamageAfter, result);
    InterlockedIncrement64(&shared->damageHits);
    return result;
}
void __fastcall OnHealthDamage(void* self, int damage, const void* method) {
    originalHealthDamage(self, DamageValue(damage), method);
}
int __fastcall OnDirectDamage(void* self, int damage, void* info, int minimumHealth, const void* method) {
    return originalDirectDamage(self, DamageValue(damage), info, minimumHealth, method);
}
int DamageFontValue(int damage, uintptr_t caller) {
    if (damage <= 0 || !gameBase || caller < gameBase || !ds::Active(shared, ds::Damage)) return damage;
    // Only these verified call sites pass the unscaled hit amount. Special
    // scripted displays / already-scaled totals must retain their original value.
    for (const auto& source : ds::FontCallSites) {
        if (caller - gameBase == source.rva + 5)
            return ds::ScalePositive(damage, ds::Read(shared->damageMultiplier));
    }
    return damage;
}
__declspec(noinline) void __fastcall OnDamageFont(void* self, int damage, bool critical, void* position,
    void* color, int specialID, bool additional, float delay, const void* method) {
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    // This changes presentation only. Never call DamageValue/health hooks here,
    // or rewrite shared last-hit counters: rendering may query a hit repeatedly.
    originalDamageFont(self, DamageFontValue(damage, caller), critical, position, color, specialID, additional, delay, method);
}
void __fastcall OnGold(void* self, int amount, bool ignoreMore, bool ignoreMag, const void* method) {
    if (amount > 0 && ds::Active(shared, ds::Gold)) {
        int result = ds::ScalePositive(amount, ds::Read(shared->goldMultiplier), ds::MaxGoldGain);
        ds::Set(shared->lastGoldBefore, amount); ds::Set(shared->lastGoldAfter, result);
        InterlockedIncrement64(&shared->goldHits); amount = result;
    }
    originalGold(self, amount, ignoreMore, ignoreMag, method);
}
int RewardFactor() { return ds::Active(shared, ds::Reward) ? ds::NormalizeMultiplier(ds::Read(shared->rewardMultiplier)) : 1000; }
int DiamondFactor(int fallback = 1000) { return ds::Active(shared,ds::Diamond) ? ds::NormalizeMultiplier(ds::Read(shared->diamondMultiplier)) : fallback; }
using RewardLimits = std::array<int,4>;
void* CurrentDataController() {
    if (!initializeMetadata || !getDataController || !dataControllerMethodUsage) return nullptr;
    // This is the same metadata usage initialized by ClearScreen.GetSoulStone.
    // Do not call a shared generic singleton getter with a null MethodInfo.
    initializeMetadata(dataControllerMethodUsage);
    void* method = nullptr;
    if (!Field(dataControllerMethodUsage, 0, method) || !method) return nullptr;
    return getDataController(method);
}
RewardLimits CurrentRewardLimits() {
    RewardLimits limits{INT_MAX,INT_MAX,INT_MAX,INT_MAX};
    void* controller = CurrentDataController();
    if (!controller) return limits;
    for (size_t i = 0; i < limits.size(); ++i)
        if (loadCurrency[i]) limits[i] = ds::RewardRoom(loadCurrency[i](controller, nullptr));
    return limits;
}
int RewardValue(int value, int factor, int room = INT_MAX) {
    if (value <= 0) return value;
    int result = ds::ScaleReward(value, factor, room);
    if (result == value) return value;
    ds::Set(shared->lastRewardBefore, value); ds::Set(shared->lastRewardAfter, result);
    InterlockedIncrement64(&shared->rewardHits);
    return result;
}
// SetClearReward inlines reads of PlayerData's four run buffers (including gem).
// GetClearReward inlines the gem read too. Give each operation a single factor,
// then restore the buffers: repeated preview/claim queries cannot compound them.
struct RewardBuffers { int prayer, soul, breach, gem; };
static_assert(sizeof(RewardBuffers) == 16);
class SettlementScope;
thread_local SettlementScope* settlement = nullptr;
class SettlementScope {
    void* player = nullptr;
    RewardBuffers raw{}, shown{};
    bool entered = false;
    int calculating = 0;
public:
    const int factor;
    const bool diamondActive;
    const int diamondFactor, gemFactor;
    const bool protectBalance;
    RewardLimits limits{INT_MAX,INT_MAX,INT_MAX,INT_MAX};
    explicit SettlementScope(int multiplier) : factor(multiplier),
        diamondActive(settlement ? settlement->diamondActive : ds::Active(shared,ds::Diamond)),
        diamondFactor(settlement ? settlement->diamondFactor : (diamondActive ? DiamondFactor() : 1000)),
        gemFactor(settlement ? settlement->gemFactor : (diamondActive ? diamondFactor : multiplier)),
        protectBalance(multiplier != 1000 || ds::Active(shared,ds::Reward) || diamondActive) {
        if (settlement || !getPlayerData) return;
        player = getPlayerData(nullptr);
        if (!Field(player, 0x218, raw)) return;
        if (protectBalance) limits = CurrentRewardLimits();
        shown = { ds::ScaleReward(raw.prayer, factor, limits[0]), ds::ScaleReward(raw.soul, factor, limits[1]),
            ds::ScaleReward(raw.breach, factor, limits[2]), ds::ScaleReward(raw.gem, gemFactor, limits[3]) };
        if (!WriteField(player, 0x218, shown)) return;
        entered = true; settlement = this;
    }
    ~SettlementScope() {
        if (!entered) return;
        // Preserve changes the original operation made (e.g. clearing a buffer).
        Restore(0x218, raw.prayer, shown.prayer); Restore(0x21c, raw.soul, shown.soul);
        Restore(0x220, raw.breach, shown.breach); Restore(0x224, raw.gem, shown.gem);
        settlement = nullptr;
    }
    void Restore(size_t offset, int before, int temporary) {
        int current = 0;
        if (Field(player, offset, current) && current == temporary) WriteField(player, offset, before);
    }
    int Calculate(RewardGain original, void* self, const void* method, Currency kind) {
        // Native stone bonuses/coupons round before the trainer multiplier.
        // Calculate against the original stone buffers and scale the final result
        // once; retain scaled gem for the inlined display/grant path.
        if (calculating) return original(self, method);
        ++calculating;
        WriteField(player, 0x218, raw.prayer); WriteField(player, 0x21c, raw.soul); WriteField(player, 0x220, raw.breach);
        int value = original(self, method);
        Restore(0x218, shown.prayer, raw.prayer); Restore(0x21c, shown.soul, raw.soul); Restore(0x220, shown.breach, raw.breach);
        --calculating;
        return RewardValue(value, factor, limits[size_t(kind)]);
    }
};
int FinalReward(RewardGain original, void* self, const void* method, Currency kind) {
    if (settlement) return settlement->Calculate(original, self, method, kind);
    const int factor = RewardFactor();
    const int value = original(self, method);
    const int room = (factor != 1000 || ds::Active(shared,ds::Reward)) ? CurrentRewardLimits()[size_t(kind)] : INT_MAX;
    return RewardValue(value, factor, room);
}
int __fastcall OnSoul(void* self, const void* method) { return FinalReward(originalSoul, self, method, Currency::Soul); }
int __fastcall OnPrayer(void* self, const void* method) { return FinalReward(originalPrayer, self, method, Currency::Prayer); }
int __fastcall OnBreach(void* self, const void* method) { return FinalReward(originalBreach, self, method, Currency::Breach); }
void SaveReward(Currency kind, void* self, int amount, bool option, const void* method) {
    const size_t index = size_t(kind);
    // Native Save* adds signed 32-bit integers without a saturating check.
    // Read the current balance again for every settlement credit, including
    // quests credited after the main reward. Do not multiply a Save* a second time.
    if (!editingWallet && amount > 0 && settlement && settlement->protectBalance && loadCurrency[index])
        amount = ds::ClampRewardGain(amount, ds::RewardRoom(loadCurrency[index](self, nullptr)));
    originalCurrencySave[index](self, amount, option, method);
}
void __fastcall OnSaveSoul(void* self, int amount, bool tracked, const void* method) { SaveReward(Currency::Soul,self,amount,tracked,method); }
void __fastcall OnSavePrayer(void* self, int amount, bool tracked, const void* method) { SaveReward(Currency::Prayer,self,amount,tracked,method); }
void __fastcall OnSaveBreach(void* self, int amount, bool tracked, const void* method) { SaveReward(Currency::Breach,self,amount,tracked,method); }
int DiamondGain(void* self, int amount, uintptr_t callerRva) {
    if(editingWallet || amount<=0)return amount;
    // Only the verified main-run call consumes the already scaled PlayerData
    // gem buffer. Other credits (including quests inside this claim) scale once.
    const bool alreadyScaled=settlement && callerRva==ds::RunGemSaveCallRva+5;
    const bool active=settlement ? settlement->diamondActive : ds::Active(shared,ds::Diamond);
    if(!active || alreadyScaled)return amount;
    const int factor=settlement ? settlement->diamondFactor : DiamondFactor();
    const int room=loadCurrency[size_t(Currency::Gem)] ? ds::RewardRoom(loadCurrency[size_t(Currency::Gem)](self,nullptr)) : INT_MAX;
    return ds::ScaleReward(amount,factor,room);
}
void __fastcall OnSaveGem(void* self, int amount, bool sync, const void* method) {
    SaveReward(Currency::Gem,self,DiamondGain(self,amount,reinterpret_cast<uintptr_t>(_ReturnAddress())-gameBase),sync,method);
}
void __fastcall OnSetReward(void* self, const void* method) {
    const int factor = settlement ? settlement->factor : RewardFactor();
    SettlementScope scope(factor);
    originalSetReward(self, method);
    rewardScreen = self; displayedRewardFactor = factor; displayedGemFactor=scope.gemFactor;
}
void __fastcall OnGetReward(void* self, const void* method) {
    // No refresh once claiming begins, including any nested game events.
    rewardScreen = nullptr;
    SettlementScope scope(settlement ? settlement->factor : RewardFactor());
    originalGetReward(self, method);
    rewardScreen = nullptr;
}
void __fastcall OnClearDisable(void* self, const void* method) {
    if (rewardScreen == self) rewardScreen = nullptr;
    originalClearDisable(self, method);
}
void __fastcall OnSinUpdate(void* self, const void* method) {
    originalSinUpdate(self, method);
    // Unity objects are touched on the game's own Update thread only.
    if (rewardScreen && !settlement && (displayedRewardFactor != RewardFactor() || displayedGemFactor != DiamondFactor(RewardFactor()))) OnSetReward(rewardScreen, nullptr);
}
int __fastcall OnSinLevel(void* self, const void* method) {
    int original = originalSinLevel(self, method);
    if (!ds::Active(shared, ds::Sin)) return original;
    const int total = std::clamp(int(ds::Read(shared->sinPoints)), 0, ds::MaxSinPoints);
    ds::Set(shared->lastSinBefore, original); ds::Set(shared->lastSinAfter, total);
    return total;
}

#include "currency-runtime.inl"
#include "artifact-runtime.inl"
#include "inventory-skills-runtime.inl"
#include "wallet-runtime.inl"
#include "rune-runtime.inl"
#include "sync-runtime.inl"
#include "unlock-runtime.inl"

#ifndef DS_RUNTIME_TEST
void Fail(LONG code, const wchar_t* message) {
    ds::Disable(shared);
    wcsncpy_s(shared->message, message, _TRUNCATE);
    ds::Set(shared->errorCode, code); ds::Set(shared->state, ds::Failed);
}
DWORD WINAPI Worker(void*) {
    wchar_t name[96]; ds::MappingName(name, GetCurrentProcessId());
    HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name);
    if (!mapping) return 1;
    shared = static_cast<ds::Shared*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ds::Shared)));
    if (!shared) { CloseHandle(mapping); return 2; }
    if (shared->magic != ds::Magic || shared->version != ds::Version || shared->size != sizeof(ds::Shared) || shared->gamePid != GetCurrentProcessId()) return 3;
    HMODULE assembly = nullptr;
    for (int i = 0; i < 600 && !assembly; ++i) { assembly = GetModuleHandleW(L"GameAssembly.dll"); if (!assembly) Sleep(100); }
    if (!assembly) { Fail(10, L"GameAssembly.dll 尚未加载，请完全重启游戏。"); return 10; }
    auto* base = reinterpret_cast<unsigned char*>(assembly);
    // A verified build-specific profile; every target is checked before any patch.
    auto matches = [base](const ds::Target& target) {
        MEMORY_BASIC_INFORMATION page{};
        return VirtualQuery(base + target.rva, &page, sizeof(page)) && page.State == MEM_COMMIT &&
            (page.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) &&
            !(page.Protect & PAGE_GUARD) && memcmp(base + target.rva, target.prefix, sizeof(target.prefix)) == 0;
    };
    for (const auto& target : ds::Targets) {
        if (!matches(target)) {
            Fail(11, L"游戏函数校验失败，此版本尚未适配；未启用任何修改。"); return 11;
        }
    }
    for (const auto& helper : ds::Helpers) if (!matches(helper)) {
        Fail(11, L"游戏资源接口校验失败；未启用任何修改。"); return 11;
    }
    for (const auto& source : ds::FontCallSites) if (!matches(source)) {
        Fail(11, L"伤害浮字调用点校验失败；未启用任何修改。"); return 11;
    }
    for (const auto& source : ds::RewardMetadataSites) if (!matches(source)) {
        Fail(11, L"奖励余额接口调用点校验失败；未启用任何修改。"); return 11;
    }
    for (const auto& source : ds::ArtifactSaveSites) if (!matches(source)) {
        Fail(11, L"圣器续局接口调用点校验失败；未启用任何修改。"); return 11;
    }
    for(const auto& source:ds::GemSaveSites)if(!matches(source)){Fail(11,L"钻石领取调用点不匹配，未启用功能。");return 11;}
    if(!artifactApi.Load(reinterpret_cast<HMODULE>(base))) {
        Fail(11,L"圣器托管接口不可用；请重启游戏并核对版本。");return 11;
    }
    gameBase = reinterpret_cast<uintptr_t>(base);
    for(const auto& patch:ds::SkillCapacityPatches) {
        if(memcmp(base+patch.rva,patch.prefix,16)!=0){Fail(11,L"技能容量指令不匹配，未启用功能。");return 11;}
    }
    for(const auto& patch:ds::ArtifactCapacityPatches)if(memcmp(base+patch.rva,patch.prefix,16)!=0){Fail(11,L"圣器获取容量指令不匹配。");return 11;}
    for(const auto& source:ds::ArtifactInventoryCalls)if(!matches(source)){Fail(11,L"背包容量调用点不匹配，未启用功能。");return 11;}
    for(const auto& source:ds::SkillEquipmentCalls)if(!matches(source)){Fail(11,L"技能容量调用点不匹配，未启用功能。");return 11;}
    for(const auto& source:ds::SkillNormalCalls)if(!matches(source)){Fail(11,L"普通技能容量调用点不匹配。");return 11;}
    for(const auto& source:ds::SkillInventoryCalls)if(!matches(source)){Fail(11,L"技能背包容量调用点不匹配。");return 11;}
    for(const auto& record:ds::WalletMethods)if(!matches(record)){Fail(11,L"货币原生接口不匹配，未启用功能。");return 11;}
    for(const auto& record:ds::ChallengeRollSites)if(!matches(record)){Fail(11,L"挑战符文概率调用点不匹配，未启用功能。");return 11;}
    refillWeaponCooldown=reinterpret_cast<GameAction>(base+ds::Helpers[33].rva);
    encodeCooldownFloat=reinterpret_cast<EncodeFloat>(base+ds::Helpers[34].rva);
    getChallengeChance=reinterpret_cast<ChallengeChance>(base+ds::Helpers[31].rva);
    getPendingChallengeCount=reinterpret_cast<RewardGain>(base+ds::Helpers[32].rva);
    getCharacter = reinterpret_cast<CharacterAsset>(base + ds::Helpers[0].rva);
    getWeapon = reinterpret_cast<WeaponAsset>(base + ds::Helpers[1].rva);
    getPet = reinterpret_cast<CharacterAsset>(base + ds::Helpers[2].rva);
    getPlayerData = reinterpret_cast<Instance>(base + ds::Helpers[3].rva);
    initializeMetadata = reinterpret_cast<MetadataInit>(base + ds::Helpers[4].rva);
    getDataController = reinterpret_cast<Instance>(base + ds::Helpers[5].rva);
    dataControllerMethodUsage = base + ds::DataControllerMethodUsageRva;
    for (size_t i=0;i<loadCurrency.size();++i) loadCurrency[i]=reinterpret_cast<RewardGain>(base + ds::Helpers[6+i].rva);
    getMaxDashCount = reinterpret_cast<RewardGain>(base + ds::Helpers[10].rva);
    encodeMovementInt = reinterpret_cast<EncodeInt>(base + ds::Helpers[11].rva);
    decodeMovementInt = reinterpret_cast<DecodeInt>(base + ds::Helpers[12].rva);
    getArtifactManager=reinterpret_cast<Instance>(base+ds::Helpers[13].rva);
    constructArtifact=reinterpret_cast<ArtifactCtor>(base+ds::Helpers[14].rva);
    cloneArtifactBox=reinterpret_cast<UnityClone>(base+ds::Helpers[15].rva);
    componentTransform=reinterpret_cast<UnityComponent>(base+ds::Helpers[16].rva);
    componentGameObject=reinterpret_cast<UnityComponent>(base+ds::Helpers[17].rva);
    getTypeComponent=reinterpret_cast<UnityTypeComponent>(base+ds::Helpers[18].rva);
    addTypeComponent=reinterpret_cast<UnityTypeComponent>(base+ds::Helpers[19].rva);
    destroyArtifactObject=reinterpret_cast<UnityDestroy>(base+ds::Helpers[20].rva);
    originalEquipValidate=reinterpret_cast<GameAction>(base+ds::Helpers[21].rva);
    checkArtifactInventoryPool=reinterpret_cast<InventoryPool>(base+ds::Helpers[22].rva);
    getSkillManager=reinterpret_cast<Instance>(base+ds::Helpers[23].rva);
    constructSkill=reinterpret_cast<ArtifactCtor>(base+ds::Helpers[24].rva);
    skillChanged=reinterpret_cast<GameAction>(base+ds::Helpers[25].rva);
    loadRuneDust=reinterpret_cast<RewardGain>(base+ds::Helpers[26].rva);
    loadRuneState=reinterpret_cast<RuneStateLoad>(base+ds::Helpers[27].rva);
    checkArtifactAllPool=reinterpret_cast<InventoryPool>(base+ds::Helpers[28].rva);
    checkSkillAllPool=reinterpret_cast<InventoryPool>(base+ds::Helpers[29].rva);
    checkSkillInventoryPool=reinterpret_cast<InventoryPool>(base+ds::Helpers[30].rva);
    void* replacements[] = {reinterpret_cast<void*>(&OnHit), reinterpret_cast<void*>(&OnEventHit), reinterpret_cast<void*>(&OnStatusHit),
        reinterpret_cast<void*>(&OnHealthDamage), reinterpret_cast<void*>(&OnDirectDamage), reinterpret_cast<void*>(&OnGold),
        reinterpret_cast<void*>(&OnSoul), reinterpret_cast<void*>(&OnPrayer), reinterpret_cast<void*>(&OnBreach),
        reinterpret_cast<void*>(&OnSkin), reinterpret_cast<void*>(&OnPet), reinterpret_cast<void*>(&OnWeapon),
        reinterpret_cast<void*>(&OnCharacter), reinterpret_cast<void*>(&OnLobby), reinterpret_cast<void*>(&OnDamageFont),
        reinterpret_cast<void*>(&OnSinLevel), reinterpret_cast<void*>(&OnSetReward), reinterpret_cast<void*>(&OnGetReward),
        reinterpret_cast<void*>(&OnClearDisable), reinterpret_cast<void*>(&OnSinUpdate),
        reinterpret_cast<void*>(&OnSaveSoul), reinterpret_cast<void*>(&OnSavePrayer), reinterpret_cast<void*>(&OnSaveBreach), reinterpret_cast<void*>(&OnSaveGem),
        reinterpret_cast<void*>(&OnMovementLateUpdate),reinterpret_cast<void*>(&OnDashAfter),reinterpret_cast<void*>(&OnDashReady),
        reinterpret_cast<void*>(&OnArtifactUpdate),reinterpret_cast<void*>(&OnArtifactSet),reinterpret_cast<void*>(&OnArtifactLoad),
        reinterpret_cast<void*>(&OnArtifactReset),reinterpret_cast<void*>(&OnArtifactDestroy),reinterpret_cast<void*>(&OnEquipInit),
        reinterpret_cast<void*>(&OnViewerEquipInit),reinterpret_cast<void*>(&OnArtifactPopupEnable),reinterpret_cast<void*>(&OnArtifactPopupDisable),
        reinterpret_cast<void*>(&OnCharacterEnable),reinterpret_cast<void*>(&OnCharacterDisable),reinterpret_cast<void*>(&OnArtifactSaveStates),
        reinterpret_cast<void*>(&OnAutoSave),reinterpret_cast<void*>(&OnLoadPreview),
        reinterpret_cast<void*>(&OnCapacitySix),reinterpret_cast<void*>(&OnCapacityEight),reinterpret_cast<void*>(&OnArtifactHaveCount),reinterpret_cast<void*>(&OnArtifactBackFull),reinterpret_cast<void*>(&OnArtifactAllFull),reinterpret_cast<void*>(&OnInventoryInit),reinterpret_cast<void*>(&OnViewerInventoryInit),reinterpret_cast<void*>(&OnSkillUpdate),reinterpret_cast<void*>(&OnSkillInit),reinterpret_cast<void*>(&OnSkillDestroy),reinterpret_cast<void*>(&OnSkillLoad),reinterpret_cast<void*>(&OnSkillEquipInit),reinterpret_cast<void*>(&OnViewerSkillEquipInit),reinterpret_cast<void*>(&OnRuneCheck),reinterpret_cast<void*>(&OnRuneOverflow),reinterpret_cast<void*>(&OnRuneSingle),reinterpret_cast<void*>(&OnRuneStateSave),
        reinterpret_cast<void*>(&OnArtifactAllListInit),reinterpret_cast<void*>(&OnArtifactSellListInit),reinterpret_cast<void*>(&OnSkillAllListInit),reinterpret_cast<void*>(&OnSkillSellListInit),reinterpret_cast<void*>(&OnSkillLevelListInit),reinterpret_cast<void*>(&OnSkillInventoryInit),reinterpret_cast<void*>(&OnViewerSkillInventoryInit),reinterpret_cast<void*>(&OnDataUpdate),reinterpret_cast<void*>(&OnRuneRefine),reinterpret_cast<void*>(&OnRuneRefresh),reinterpret_cast<void*>(&OnRuneDraw),reinterpret_cast<void*>(&OnChallengeBuild),reinterpret_cast<void*>(&OnChallengeRegister),reinterpret_cast<void*>(&OnChallengeDisplay),reinterpret_cast<void*>(&OnRandomNext),reinterpret_cast<void*>(&OnSkillCooldown),reinterpret_cast<void*>(&OnWeaponCooldown),reinterpret_cast<void*>(&OnConnectFail),reinterpret_cast<void*>(&OnTimeOut),reinterpret_cast<void*>(&OnSkillBuffEquipInit),reinterpret_cast<void*>(&OnViewerSkillBuffEquipInit)};
    void** originals[] = {reinterpret_cast<void**>(&originalHit), reinterpret_cast<void**>(&originalEventHit), reinterpret_cast<void**>(&originalStatusHit),
        reinterpret_cast<void**>(&originalHealthDamage), reinterpret_cast<void**>(&originalDirectDamage), reinterpret_cast<void**>(&originalGold),
        reinterpret_cast<void**>(&originalSoul), reinterpret_cast<void**>(&originalPrayer), reinterpret_cast<void**>(&originalBreach),
        reinterpret_cast<void**>(&originalSkin), reinterpret_cast<void**>(&originalPet), reinterpret_cast<void**>(&originalWeapon),
        reinterpret_cast<void**>(&originalCharacter), reinterpret_cast<void**>(&originalLobby), reinterpret_cast<void**>(&originalDamageFont),
        reinterpret_cast<void**>(&originalSinLevel), reinterpret_cast<void**>(&originalSetReward), reinterpret_cast<void**>(&originalGetReward),
        reinterpret_cast<void**>(&originalClearDisable), reinterpret_cast<void**>(&originalSinUpdate),
        reinterpret_cast<void**>(&originalCurrencySave[size_t(Currency::Soul)]), reinterpret_cast<void**>(&originalCurrencySave[size_t(Currency::Prayer)]),
        reinterpret_cast<void**>(&originalCurrencySave[size_t(Currency::Breach)]), reinterpret_cast<void**>(&originalCurrencySave[size_t(Currency::Gem)]),
        reinterpret_cast<void**>(&originalMovementLateUpdate),reinterpret_cast<void**>(&originalDashAfter),reinterpret_cast<void**>(&originalDashReady),
        reinterpret_cast<void**>(&originalArtifactUpdate),reinterpret_cast<void**>(&originalArtifactSet),reinterpret_cast<void**>(&originalArtifactLoad),
        reinterpret_cast<void**>(&originalArtifactReset),reinterpret_cast<void**>(&originalArtifactDestroy),reinterpret_cast<void**>(&originalEquipInit),
        reinterpret_cast<void**>(&originalViewerEquipInit),reinterpret_cast<void**>(&originalArtifactPopupEnable),reinterpret_cast<void**>(&originalArtifactPopupDisable),
        reinterpret_cast<void**>(&originalCharacterEnable),reinterpret_cast<void**>(&originalCharacterDisable),reinterpret_cast<void**>(&originalArtifactSaveStates),
        reinterpret_cast<void**>(&originalAutoSave),reinterpret_cast<void**>(&originalLoadPreview),
        reinterpret_cast<void**>(&originalCapacitySix),reinterpret_cast<void**>(&originalCapacityEight),reinterpret_cast<void**>(&originalArtifactHaveCount),reinterpret_cast<void**>(&originalArtifactBackFull),reinterpret_cast<void**>(&originalArtifactAllFull),reinterpret_cast<void**>(&originalInventoryInit),reinterpret_cast<void**>(&originalViewerInventoryInit),reinterpret_cast<void**>(&originalSkillUpdate),reinterpret_cast<void**>(&originalSkillInit),reinterpret_cast<void**>(&originalSkillDestroy),reinterpret_cast<void**>(&originalSkillLoad),reinterpret_cast<void**>(&originalSkillEquipInit),reinterpret_cast<void**>(&originalViewerSkillEquipInit),reinterpret_cast<void**>(&originalRuneCheck),reinterpret_cast<void**>(&originalRuneOverflow),reinterpret_cast<void**>(&originalRuneSingle),reinterpret_cast<void**>(&originalRuneStateSave),
        reinterpret_cast<void**>(&originalArtifactAllListInit),reinterpret_cast<void**>(&originalArtifactSellListInit),reinterpret_cast<void**>(&originalSkillAllListInit),reinterpret_cast<void**>(&originalSkillSellListInit),reinterpret_cast<void**>(&originalSkillLevelListInit),reinterpret_cast<void**>(&originalSkillInventoryInit),reinterpret_cast<void**>(&originalViewerSkillInventoryInit),reinterpret_cast<void**>(&originalDataUpdate),reinterpret_cast<void**>(&originalRuneRefine),reinterpret_cast<void**>(&originalRuneRefresh),reinterpret_cast<void**>(&originalRuneDraw),reinterpret_cast<void**>(&originalChallengeBuild),reinterpret_cast<void**>(&originalChallengeRegister),reinterpret_cast<void**>(&originalChallengeDisplay),reinterpret_cast<void**>(&originalRandomNext),reinterpret_cast<void**>(&originalSkillCooldown),reinterpret_cast<void**>(&originalWeaponCooldown),reinterpret_cast<void**>(&originalConnectFail),reinterpret_cast<void**>(&originalTimeOut),reinterpret_cast<void**>(&originalSkillBuffEquipInit),reinterpret_cast<void**>(&originalViewerSkillBuffEquipInit)};
    static_assert(std::size(ds::Targets) == std::size(replacements));
    static_assert(std::size(ds::Targets) == std::size(originals));
    auto status = MH_Initialize();
    if (status != MH_OK) { Fail(12, L"钩子库初始化失败，请重启游戏。"); return 12; }
    size_t created = 0;
    for (size_t i = 0; i < std::size(ds::Targets); ++i) {
        status = MH_CreateHook(base + ds::Targets[i].rva, replacements[i], originals[i]);
        if (status != MH_OK) break;
        ++created;
    }
    if (status != MH_OK) {
        for (size_t i = 0; i < created; ++i) MH_RemoveHook(base + ds::Targets[i].rva);
        MH_Uninitialize(); Fail(13, L"钩子创建失败；未启用任何修改。"); return 13;
    }
    bool overlay = ds::InstallOverlay(shared);
    for (const auto& target : ds::Targets) MH_QueueEnableHook(base + target.rva);
    status = MH_ApplyQueued();
    if (status != MH_OK) {
        for (const auto& target : ds::Targets) MH_DisableHook(base + target.rva);
        ds::Set(shared->available, 0); Fail(14, L"钩子启用失败，请完全重启游戏。"); return 14;
    }
    if (!overlay) ds::Set(shared->overlayState, -1);
    wcsncpy_s(shared->message, overlay ? L"77 个游戏入口已校验；技能无冷却 / 自动重试；F2 打开面板。" : L"功能可用，但 D3D11 面板安装失败。请重启游戏。", _TRUNCATE);
    ds::Set(shared->available, ds::All); ds::Set(shared->state, ds::Ready);
    // The DLL stays resident. Closing the controller makes hooks pass through;
    // we never free trampolines while a game thread could still be executing one.
    for (;;) {
        ds::Set(shared->runtimeHeartbeat, LONG(GetTickCount()));
        if (!ds::Fresh(GetTickCount(), DWORD(ds::Read(shared->clientHeartbeat)))) ds::Disable(shared);
        Sleep(100);
    }
}
#endif
}

#ifndef DS_RUNTIME_TEST
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void*) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        HANDLE thread = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
#endif
