#pragma once
#include <cstddef>
#include <algorithm>
#include <climits>
#include <cstdint>

namespace ds {
enum class WalletMode { Delta, DeltaBool, AbsoluteBool, Coupon, Hunting, Period, Gold };
struct WalletEntry {
    const wchar_t* key;
    const char* label;
    const char* load;
    const char* save;
    WalletMode mode;
    int argument=0, index=0;
};
inline constexpr WalletEntry Wallets[]={
    {L"Gems","钻石 / Gems","LoadGem","SaveGem",WalletMode::DeltaBool},
    {L"Prayerstones","祈祷石 / Prayerstones","LoadPrayerStone","SavePrayerStone",WalletMode::DeltaBool},
    {L"Soulstones","灵魂石 / Soulstones","LoadSoulStone","SaveSoulStone",WalletMode::DeltaBool},
    {L"Breachstones","裂隙石 / Riftstones","LoadBreachStone","SaveBreachStone",WalletMode::DeltaBool},
    {L"Gold","金币（本局）","","SetGold",WalletMode::Gold},
    {L"Bans","禁用点数 / Bans","LoadBanCount","SaveBanCount",WalletMode::Delta},
    {L"RunePowder","符文粉末","LoadRuneDust","SaveRuneDust",WalletMode::AbsoluteBool},
    {L"RuneRefiningTools","符文精炼工具","LoadRuneRefineTool","SaveRuneRefineTool",WalletMode::AbsoluteBool},
    {L"RuneRerollScrolls","符文重铸卷轴","LoadRuneRerollScroll","SaveRuneRerollScroll",WalletMode::AbsoluteBool},
    {L"RuneKeys","符文钥匙","LoadRuneKey","SaveRuneKey",WalletMode::AbsoluteBool},
    {L"PetCoupons","特殊宠物兑换券","LoadPetGiftCoupon","SavePetGiftCoupon",WalletMode::Delta},
    {L"BossEquipmentTickets","首领装备兑换券","LoadSkinSelectorCoupon","SaveSkinSelectorCoupon",WalletMode::Delta},
    {L"SpecialSkinTickets","特殊皮肤兑换券","LoadSkinGiftCoupon","SaveSkinGiftCoupon",WalletMode::Delta},
    {L"PassSkinTickets","通行证皮肤兑换券","LoadPassSkinSelectorCoupon","SavePassSkinSelectorCoupon",WalletMode::Delta},
    {L"SeasonPassTickets","赛季通行证兑换券","LoadRevivalPassCoupon","SaveRevivalPassCoupon",WalletMode::Delta},
    {L"BingoTickets","宾果券","LoadTreasureBingoTicket","SaveTreasureBingoTicket",WalletMode::Delta},
    {L"CharacterTickets","角色兑换券","LoadCharacterSelectorCoupon","SaveCharacterSelectorCoupon",WalletMode::Delta},
    {L"SkinTickets","皮肤兑换券","LoadCharacterSkinSelectorCoupon","SaveCharacterSkinSelectorCoupon",WalletMode::Delta},
    {L"WeaponTickets","武器兑换券","LoadWeaponSelectorCoupon","SaveWeaponSelectorCoupon",WalletMode::Delta},
    {L"UniqueRuneRandomTickets","独特符文随机兑换券","LoadUniqueRuneGachaCoupon","SaveUniqueRuneGachaCoupon",WalletMode::AbsoluteBool},
    {L"Mileage","里程点数","LoadMileagePoint","SaveMileagePoint",WalletMode::DeltaBool},
    {L"Multiplier150","灵魂石倍率券 1.5x","LoadSoulStoneCouponCount","SaveSoulStoneCouponCount",WalletMode::Coupon,150},
    {L"Multiplier151","灵魂石倍率券 1.5x（广告型）","LoadSoulStoneCouponCount","SaveSoulStoneCouponCount",WalletMode::Coupon,151},
    {L"Multiplier200","灵魂石倍率券 2x","LoadSoulStoneCouponCount","SaveSoulStoneCouponCount",WalletMode::Coupon,200},
    {L"Multiplier300","灵魂石倍率券 3x","LoadSoulStoneCouponCount","SaveSoulStoneCouponCount",WalletMode::Coupon,300},
    {L"Songpyeon","松饼（旧版活动）","LoadSongpyeon","SaveSongpyeon",WalletMode::Delta},
    {L"Songpyeon2409","松饼（2024-09 活动）","LoadEventGoods","SaveEventGoods",WalletMode::Hunting,2},
    {L"Songpyeon2508","松饼（2025-08 活动）","LoadEventGoods","SaveEventGoods",WalletMode::Hunting,5},
    {L"Anniversary3","三周年券（2025-01）","LoadEventGoods","SaveEventGoods",WalletMode::Hunting,4},
    {L"Tube2504","游泳圈（2025-04）","LoadPeriodEventGoods","SavePeriodEventGoods",WalletMode::Period,1,0},
    {L"Coconut2504","椰子（2025-04）","LoadPeriodEventGoods","SavePeriodEventGoods",WalletMode::Period,1,1},
    {L"Tube2506","游泳圈（2025-06）","LoadPeriodEventGoods","SavePeriodEventGoods",WalletMode::Period,2,0},
    {L"Coconut2506","椰子（2025-06）","LoadPeriodEventGoods","SavePeriodEventGoods",WalletMode::Period,2,1},
    {L"ManaStones","魔石（2025-09）","LoadEventGoods","SaveEventGoods",WalletMode::Hunting,6},
    {L"FestivalItems","节日物品（2026-01）","LoadEventGoods","SaveEventGoods",WalletMode::Hunting,7},
    {L"GoldenChips","黄金筹码（2026-02）","LoadEventGoods","SaveEventGoods",WalletMode::Hunting,8},
    {L"BossTokens","首领代币（2026-06）","LoadEventGoods","SaveEventGoods",WalletMode::Hunting,9},
    {L"UniqueRuneSelector","独特符文自选券（游戏额外类型）","LoadUniqueRuneSelectorCoupon","SaveUniqueRuneSelectorCoupon",WalletMode::AbsoluteBool},
    {L"AnswerSheet2504","答卷（2025-04，游戏额外类型）","LoadPeriodEventGoods","SavePeriodEventGoods",WalletMode::Period,1,2},
    {L"AnswerSheet2506","答卷（2025-06，游戏额外类型）","LoadPeriodEventGoods","SavePeriodEventGoods",WalletMode::Period,2,2},
    {L"CleaningTokens","清扫代币（2026-09，游戏额外类型）","LoadEventGoods","SaveEventGoods",WalletMode::Hunting,10},
};
inline constexpr int WalletCount=int(sizeof(Wallets)/sizeof(Wallets[0]));
inline bool ValidWallet(int index){return index>=0 && index<WalletCount;}
inline bool WalletUsesDelta(WalletMode mode) {
    return mode==WalletMode::Delta || mode==WalletMode::DeltaBool || mode==WalletMode::Hunting || mode==WalletMode::Period;
}
inline int WalletValue(int target,int current,WalletMode mode) {
    // A negative overflowed balance can be more than INT_MAX below the target.
    // Pass only a representable increment; the main-thread adapter reads back
    // each step before calculating the remainder.
    return WalletUsesDelta(mode)?int(std::clamp<int64_t>(int64_t(target)-current,INT_MIN,INT_MAX)):target;
}
}
