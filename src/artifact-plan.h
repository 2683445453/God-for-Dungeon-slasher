#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace ds::artifacts {
constexpr int NativePhysical = 7, NativeAllowed = 6, NativeBackpack = 6;
constexpr int MaxPhysical = 50, MaxItems = 128;
constexpr int SaveTag = 0x44530000;
constexpr int LayoutTag = 0x44540000;
constexpr int InventoryLayoutTag = 0x44600000, InventoryLayoutMask = 0xffe00000;
constexpr int ExtendedLayoutTag = 0x45000000, ExtendedLayoutMask = 0xff800000;
constexpr int FullLayoutTag = 0x48000000, FullLayoutMask = 0xfc000000;
inline bool Tagged(int value) {return (value&0xffff0000)==SaveTag || (value&0xffff0000)==LayoutTag || (value&InventoryLayoutMask)==InventoryLayoutTag || (value&ExtendedLayoutMask)==ExtendedLayoutTag || (value&FullLayoutMask)==FullLayoutTag;}
inline int SaveLayout(int physical,int nativeAllowed,int backpack,int skills,int skillBackpack=6,int buffs=2) {
    // Four six-bit counts, native seventh artifact, and two/six buff slots.
    // Keep older encodings when their layout is sufficient.
    if(backpack>30 || skillBackpack>30 || buffs!=2)return FullLayoutTag|physical|(backpack<<6)|(skills<<12)|(skillBackpack<<18)|((nativeAllowed==7?1:0)<<24)|((buffs==6?1:0)<<25);
    if(physical>30 || skills>30)return ExtendedLayoutTag|physical|(backpack<<6)|(skills<<11)|(skillBackpack<<17)|((nativeAllowed==7?1:0)<<22);
    if(skillBackpack!=6)return InventoryLayoutTag|physical|(backpack<<5)|(skills<<10)|(skillBackpack<<15)|((nativeAllowed==7?1:0)<<20);
    return LayoutTag|physical|(backpack<<5)|(skills<<10)|((nativeAllowed==7?1:0)<<15);
}
inline bool LoadLayout(int value,int& physical,int& nativeAllowed,int& backpack,int& skills,int& skillBackpack,int& buffs) {
    skillBackpack=6;buffs=2;
    if((value&0xffff0000)==SaveTag) {
        physical=(value>>8)&255;nativeAllowed=value&255;backpack=skills=6;
        return physical>=7&&physical<=MaxPhysical&&(nativeAllowed==6||nativeAllowed==7);
    }
    if((value&FullLayoutMask)==FullLayoutTag) {
        physical=value&63;backpack=(value>>6)&63;skills=(value>>12)&63;skillBackpack=(value>>18)&63;
        nativeAllowed=(value&0x1000000)?7:6;buffs=(value&0x2000000)?6:2;
        return physical>=7&&physical<=MaxPhysical&&backpack>=6&&backpack<=50&&skills>=6&&skills<=50&&skillBackpack>=6&&skillBackpack<=50;
    }
    if((value&ExtendedLayoutMask)==ExtendedLayoutTag) {
        physical=value&63;backpack=(value>>6)&31;skills=(value>>11)&63;skillBackpack=(value>>17)&31;
        nativeAllowed=(value&0x400000)?7:6;
        return physical>=7&&physical<=MaxPhysical&&backpack>=6&&backpack<=30&&skills>=6&&skills<=50&&skillBackpack>=6&&skillBackpack<=30;
    }
    const bool inventory=(value&InventoryLayoutMask)==InventoryLayoutTag;
    if(!inventory&&(value&0xffff0000)!=LayoutTag)return false;
    physical=value&31;backpack=(value>>5)&31;skills=(value>>10)&31;
    if(inventory)skillBackpack=(value>>15)&31;
    nativeAllowed=(value&(inventory?0x100000:0x8000))?7:6;
    return physical>=7&&physical<=30&&backpack>=6&&backpack<=30&&skills>=6&&skills<=30&&skillBackpack>=6&&skillBackpack<=30;
}
inline bool LoadLayout(int value,int& physical,int& nativeAllowed,int& backpack,int& skills,int& skillBackpack) {int buffs;return LoadLayout(value,physical,nativeAllowed,backpack,skills,skillBackpack,buffs);}
inline bool LoadLayout(int value,int& physical,int& nativeAllowed,int& backpack,int& skills) {int skillBackpack;return LoadLayout(value,physical,nativeAllowed,backpack,skills,skillBackpack);}
inline int SaveCapacity(int physical,int nativeAllowed) {return SaveTag|(physical<<8)|nativeAllowed;}
inline bool LoadCapacity(int value,int& physical,int& nativeAllowed) {
    int backpack=6,skills=6;return LoadLayout(value,physical,nativeAllowed,backpack,skills);
}
// Reserve slots are retained: this build allocates 20 entries, of which 7
// belong to equipment and 13 to the inventory; only 6 inventory items allowed.
struct Plan { int target=0; bool blocked=false; std::vector<int> order; };
inline Plan ResizePlan(const std::vector<bool>& occupied, int physical, int requested,int backpack=0) {
    Plan plan;
    if(physical<NativePhysical || physical>MaxPhysical || int(occupied.size())<physical+NativeBackpack || occupied.size()>MaxItems)return plan;
    requested=std::clamp(requested,NativePhysical,MaxPhysical); plan.target=requested;
    if(requested<physical)for(int i=requested;i<physical;++i)if(occupied[i]) {plan.blocked=true;plan.target=physical;return plan;}
    for(int i=0;i<std::min(requested,physical);++i)plan.order.push_back(i);
    for(int i=physical;i<requested;++i)plan.order.push_back(-1);
    for(int i=physical;i<int(occupied.size());++i)plan.order.push_back(i);
    // Preserve native reserve entries. Extra inventory entries are removed
    // only when empty; equipment changes retain their relative order.
    if(backpack) {
        const int reserve=backpack+7;
        for(int i=physical+reserve;i<int(occupied.size());++i)if(occupied[i]){plan.blocked=true;plan.order.clear();return plan;}
        plan.order.resize(requested+std::min(reserve,int(occupied.size())-physical));
        while(int(plan.order.size())<requested+reserve)plan.order.push_back(-1);
    }
    if(plan.order.size()>MaxItems) {plan={};}
    return plan;
}
struct Grid { float cell=0,spacing=0; int columns=0; };
inline Grid FitGrid(float width,float height,int count) {
    if(!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0||count<=0||count>MaxItems)return {};
    Grid best{};
    // Choose the largest square icons fitting the existing rectangle. Wide
    // HUDs remain a row; taller selection panels become multiple rows.
    for(int columns=1;columns<=count;++columns) {
        int rows=(count+columns-1)/columns;
        float cell=std::min((width-2*(columns-1))/columns,(height-2*(rows-1))/rows);
        if(cell>best.cell)best={cell,2.0f,columns};
    }
    return best;
}
}
