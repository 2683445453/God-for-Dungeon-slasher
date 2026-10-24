#pragma once
#include "protocol.h"
#include <array>
#include <string>
#include <cwctype>

namespace ds {
struct Preferences {
    LONG enabled=AutoRetry;
    std::array<LONG,5> factors{2000,2000,2000,2000,2000};
    LONG sin=140;
    std::array<LONG,4> slots{12,12,12,12}; // artifact equip/backpack, skill equip/backpack
    std::array<LONG,3> challenge{1000,5000,3}; // basis points, basis points, count
    LONG walletSelection=0;
    std::array<LONG,WalletCount> walletTargets=[] {std::array<LONG,WalletCount> value{};value.fill(1000);return value;}();
    bool operator==(const Preferences& other)const {
        return enabled==other.enabled && factors==other.factors && sin==other.sin && slots==other.slots && challenge==other.challenge && walletSelection==other.walletSelection && walletTargets==other.walletTargets;
    }
};
inline constexpr const wchar_t* FactorKeys[]={L"Damage",L"Gold",L"Reward",L"RunePowder",L"Diamond"};
inline constexpr const wchar_t* SlotSections[]={L"Artifacts",L"ArtifactInventory",L"Skills",L"SkillInventory"};
inline constexpr LONG SlotMaximums[]={MaxArtifactSlots,MaxInventorySlots,MaxSkillSlots,MaxInventorySlots};
inline Preferences ReadPreferences(const wchar_t* path) {
    Preferences result;
    auto integer=[&](const wchar_t* section,const wchar_t* key,LONG fallback,LONG low,LONG high) {
        wchar_t text[64];GetPrivateProfileStringW(section,key,L"",text,64,path);
        wchar_t* end=nullptr;const long long number=wcstoll(text,&end,10);
        while(end && iswspace(*end))++end;
        return end==text || !end || *end || number<low || number>high?fallback:LONG(number);
    };
    result.enabled=integer(L"Features",L"Enabled",0,0,LONG_MAX)&All;
    // Existing r14 INIs have no sync key. Enable quiet retries on upgrade;
    // an explicit F8 / unchecked switch is saved separately and stays off.
    if(integer(L"Sync",L"AutoRetry",1,0,1))result.enabled|=AutoRetry;
    else result.enabled&=~AutoRetry;
    result.sin=integer(L"Sin",L"Total",140,0,MaxSinPoints);
    result.challenge={integer(L"Challenge",L"NormalChance",1000,0,10000),integer(L"Challenge",L"BossChance",5000,0,10000),integer(L"Challenge",L"Quantity",3,1,MaxChallengeQuantity)};
    result.walletSelection=integer(L"Wallet",L"Selection",0,0,WalletCount-1);
    for(int i=0;i<WalletCount;++i)result.walletTargets[i]=integer(L"WalletTargets",Wallets[i].key,1000,0,INT_MAX);
    for(size_t i=0;i<result.slots.size();++i)result.slots[i]=integer(SlotSections[i],L"Slots",12,6,SlotMaximums[i]);
    for(size_t i=0;i<result.factors.size();++i) {
        wchar_t text[64];GetPrivateProfileStringW(L"Multipliers",FactorKeys[i],L"2",text,64,path);
        wchar_t* end=nullptr;const double number=wcstod(text,&end);while(end && iswspace(*end))++end;
        if(end!=text && end && !*end && std::isfinite(number) && number>=.1 && number<=1000)
            result.factors[i]=LONG(std::lround(number*1000));
    }
    return result;
}
inline Preferences SnapshotPreferences(Shared* state) {
    Preferences result;
    result.enabled=Read(state->enabled)&All;
    result.factors={Read(state->damageMultiplier),Read(state->goldMultiplier),Read(state->rewardMultiplier),
        Read(state->runePowderMultiplier),Read(state->diamondMultiplier)};
    result.sin=Read(state->sinPoints);
    result.slots={Read(state->artifactSlots),Read(state->inventorySlots),Read(state->skillSlots),Read(state->skillInventorySlots)};
    result.challenge={Read(state->challengeNormalChance),Read(state->challengeBossChance),Read(state->challengeQuantity)};
    result.walletSelection=Read(state->walletSelection);
    for(int i=0;i<WalletCount;++i)result.walletTargets[i]=Read(state->walletTargets[i]);
    return result;
}
inline bool WritePreferences(const wchar_t* path,const Preferences& value) {
    bool ok=true;
    auto write=[&](const wchar_t* section,const wchar_t* key,const std::wstring& text) {
        if(!WritePrivateProfileStringW(section,key,text.c_str(),path))ok=false;
    };
    // Commit parameters before the switches that depend on them.
    for(size_t i=0;i<value.factors.size();++i) {
        wchar_t number[32];swprintf_s(number,L"%.3f",NormalizeMultiplier(value.factors[i])/1000.0);
        write(L"Multipliers",FactorKeys[i],number);
    }
    write(L"Sin",L"Total",std::to_wstring(std::clamp(value.sin,0L,LONG(MaxSinPoints))));
    write(L"Challenge",L"NormalChance",std::to_wstring(std::clamp(value.challenge[0],0L,10000L)));
    write(L"Challenge",L"BossChance",std::to_wstring(std::clamp(value.challenge[1],0L,10000L)));
    write(L"Challenge",L"Quantity",std::to_wstring(std::clamp(value.challenge[2],1L,LONG(MaxChallengeQuantity))));
    write(L"Wallet",L"Selection",std::to_wstring(std::clamp(value.walletSelection,0L,LONG(WalletCount-1))));
    for(int i=0;i<WalletCount;++i)write(L"WalletTargets",Wallets[i].key,std::to_wstring(std::max(0L,value.walletTargets[i])));
    for(size_t i=0;i<value.slots.size();++i)write(SlotSections[i],L"Slots",std::to_wstring(std::clamp(value.slots[i],6L,SlotMaximums[i])));
    write(L"Features",L"Enabled",std::to_wstring(value.enabled&All));
    write(L"Sync",L"AutoRetry",(value.enabled&AutoRetry)?L"1":L"0");
    return ok;
}
inline void RestorePreferences(Shared* state,const Preferences& value) {
    Disable(state);
    volatile LONG* factors[]={&state->damageMultiplier,&state->goldMultiplier,&state->rewardMultiplier,&state->runePowderMultiplier,&state->diamondMultiplier};
    volatile LONG* slots[]={&state->artifactSlots,&state->inventorySlots,&state->skillSlots,&state->skillInventorySlots};
    for(size_t i=0;i<value.factors.size();++i)Set(*factors[i],NormalizeMultiplier(value.factors[i]));
    Set(state->sinPoints,std::clamp(value.sin,0L,LONG(MaxSinPoints)));
    Set(state->challengeNormalChance,std::clamp(value.challenge[0],0L,10000L));Set(state->challengeBossChance,std::clamp(value.challenge[1],0L,10000L));Set(state->challengeQuantity,std::clamp(value.challenge[2],1L,LONG(MaxChallengeQuantity)));
    Set(state->walletSelection,std::clamp(value.walletSelection,0L,LONG(WalletCount-1)));
    for(int i=0;i<WalletCount;++i)Set(state->walletTargets[i],std::max(0L,value.walletTargets[i]));
    for(size_t i=0;i<value.slots.size();++i)Set(*slots[i],std::clamp(value.slots[i],6L,SlotMaximums[i]));
    // Commands, panel visibility and counters are deliberately not preferences.
    Set(state->enabled,value.enabled&Read(state->available)&All);
}
}
