#define DS_RUNTIME_TEST
#include "runtime.cpp"
#include <cassert>
#include <iostream>
void OverlaySmokeTest();
void DiscoveryTests();
int RunDiscoveryFixture(const char* eventName);

namespace {
int hitCalls, eventCalls, statusCalls, damageSeen, goldSeen;
void* selfSeen; void* infoSeen; const void* methodSeen; int minimumSeen;
bool ignoreMoreSeen,ignoreMagSeen;
void __fastcall HitStub(void* s,void* i,const void* m) { ++hitCalls; selfSeen=s;infoSeen=i;methodSeen=m; }
void __fastcall EventStub(void* s,int d,bool force,const void* m) { ++eventCalls; selfSeen=s;damageSeen=d;ignoreMoreSeen=force;methodSeen=m; }
void __fastcall StatusStub(void*,int,const void*) { ++statusCalls; }
void __fastcall HealthStub(void* s,int d,const void* m) { selfSeen=s; damageSeen=d; methodSeen=m; }
int __fastcall DirectStub(void* s,int d,void* i,int min,const void* m) { selfSeen=s;damageSeen=d;infoSeen=i;minimumSeen=min;methodSeen=m; return d+7; }
void __fastcall GoldStub(void* s,int g,bool more,bool mag,const void* m) { selfSeen=s;goldSeen=g;ignoreMoreSeen=more;ignoreMagSeen=mag;methodSeen=m; }
int rewardBase=7;
int __fastcall RewardStub(void*,const void*) { return rewardBase; }
int fontDamageSeen,fontSpecialSeen,fontCalls;
bool fontCriticalSeen,fontAdditionalSeen;
float fontDelaySeen;
void* fontPositionSeen;void* fontColorSeen;
uintptr_t fontCaller=0;
volatile LONG fixtureInvocationCount=0;
void __fastcall FontStub(void* s,int d,bool critical,void* position,void* color,int special,bool additional,float delay,const void* m) {
    ++fontCalls;selfSeen=s;fontDamageSeen=d;fontCriticalSeen=critical;fontPositionSeen=position;fontColorSeen=color;
    fontSpecialSeen=special;fontAdditionalSeen=additional;fontDelaySeen=delay;methodSeen=m;
}
__declspec(noinline) void __fastcall FontFixture(void* s,int d,bool critical,void* position,void* color,int special,bool additional,float delay,const void* m) {
    FontStub(s,d,critical,position,color,special,additional,delay,m);
}
__declspec(noinline) void __fastcall RecordFontCaller(void* s,int d,bool critical,void* position,void* color,int special,bool additional,float delay,const void* m) {
    fontCaller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    FontStub(s,d,critical,position,color,special,additional,delay,m);
}
__declspec(noinline) void InvokeFontFixture(void* s,int d,bool critical,void* position,void* color,int special,bool additional,float delay,const void* m) {
    DamageFont volatile invoke=FontFixture;invoke(s,d,critical,position,color,special,additional,delay,m);
    InterlockedIncrement(&fixtureInvocationCount); // Keep the native call from becoming a tail jump.
}
bool owned=false;
alignas(8) unsigned char characterAsset[256]{},skinAsset[64]{},weaponAsset[64]{},petAsset[64]{},skinList[64]{},skinArray[64]{};
int characterFixtureId=3,petFixtureId=7;
void* __fastcall CharacterStub(int c,const void*) { return c==characterFixtureId?characterAsset:nullptr; }
void* __fastcall PetStub(int p,const void*) { return p==petFixtureId?petAsset:nullptr; }
void* __fastcall WeaponStub(void* c,int w,const void*) { return c==characterAsset && w==912?weaponAsset:nullptr; }
bool __fastcall OneStub(void* s,int id,const void* m) { selfSeen=s;minimumSeen=id;methodSeen=m;return owned; }
bool __fastcall TwoStub(void* s,int c,int id,const void* m) { selfSeen=s;damageSeen=c;minimumSeen=id;methodSeen=m;return owned; }
template<class T> void Put(unsigned char* base,size_t offset,T value) { memcpy(base+offset,&value,sizeof(value)); }
alignas(8) unsigned char playerBuffer[0x240]{};
bool noPlayer = false, changePreviewFactor = false, clearClaimBuffer = false;
RewardBuffers displayedBase{}, displayedFinal{}, granted{};
int previewCalls = 0, grantCalls = 0, sinUpdateCalls = 0, clearDisableCalls = 0, sinBase = 26;
void* __fastcall PlayerStub(const void*) { return noPlayer ? nullptr : playerBuffer; }
int StoneStub(size_t offset) { int value = 0; Field(playerBuffer,offset,value); return value * 3 / 2; }
int __fastcall SoulBufferStub(void*,const void*) { return StoneStub(0x21c); }
int __fastcall PrayerBufferStub(void*,const void*) { return StoneStub(0x218); }
int __fastcall BreachBufferStub(void*,const void*) { return StoneStub(0x220); }
int __fastcall SinStub(void* s,const void* m) { selfSeen=s;methodSeen=m;return sinBase; }
__declspec(noinline) void __fastcall PreviewFixture(void* s,const void* m) {
    ++previewCalls; selfSeen=s; methodSeen=m;
    assert(Field(playerBuffer,0x218,displayedBase));
    if(changePreviewFactor) ds::Set(shared->rewardMultiplier,ds::MaxMultiplier);
    displayedFinal = {OnPrayer(s,m),OnSoul(s,m),OnBreach(s,m),displayedBase.gem};
    RewardBuffers after{}; assert(Field(playerBuffer,0x218,after));
    assert(memcmp(&after,&displayedBase,sizeof(after))==0);
}
__declspec(noinline) void __fastcall ClaimFixture(void* s,const void* m) {
    ++grantCalls; selfSeen=s; methodSeen=m;
    int gem = 0; Field(playerBuffer,0x224,gem);
    granted = {OnPrayer(s,m),OnSoul(s,m),OnBreach(s,m),gem};
    if(clearClaimBuffer) WriteField(playerBuffer,0x224,0);
}
void __fastcall SinUpdateStub(void*,const void*) { ++sinUpdateCalls; }
void __fastcall ClearDisableStub(void*,const void*) { ++clearDisableCalls; }
void ResetBuffers() { WriteField(playerBuffer,0x218,RewardBuffers{3,7,11,5}); }
void ExpectBuffers() {
    RewardBuffers now{}; assert(Field(playerBuffer,0x218,now));
    assert(now.prayer==3 && now.soul==7 && now.breach==11 && now.gem==5 && !settlement);
}
void SinAndSettlementTests(ds::Shared& data,void* s,const void* m) {
    originalSinLevel=SinStub;data.sinPoints=73;data.enabled=ds::Reward;
    assert(OnSinLevel(s,m)==26);
    ds::SetFeature(&data,ds::Sin,true); assert(OnSinLevel(s,m)==73 && data.lastSinBefore==26 && data.lastSinAfter==73);
    assert(selfSeen==s && methodSeen==m);
    data.sinPoints=INT_MAX;assert(OnSinLevel(s,m)==140);
    data.sinPoints=-1;assert(OnSinLevel(s,m)==0);
    data.sinPoints=0;assert(OnSinLevel(s,m)==0);
    data.clientHeartbeat=LONG(GetTickCount()-4000);assert(OnSinLevel(s,m)==26);
    data.clientHeartbeat=LONG(GetTickCount());ds::SetFeature(&data,ds::Sin,false);assert(OnSinLevel(s,m)==26);
    getPlayerData=PlayerStub;originalSoul=SoulBufferStub;originalPrayer=PrayerBufferStub;originalBreach=BreachBufferStub;
    originalSetReward=PreviewFixture;originalGetReward=ClaimFixture;originalSinUpdate=SinUpdateStub;originalClearDisable=ClearDisableStub;
    data.enabled=ds::Reward;data.rewardMultiplier=2500;ResetBuffers();
    OnSetReward(s,m);ExpectBuffers();
    // Native bonuses round before the trainer factor: floor(7*1.5)*2.5=25.
    assert(displayedBase.prayer==8 && displayedBase.soul==18 && displayedBase.breach==28 && displayedBase.gem==13);
    assert(displayedFinal.prayer==10 && displayedFinal.soul==25 && displayedFinal.breach==40 && displayedFinal.gem==13);
    OnSetReward(s,m);OnSetReward(s,m);ExpectBuffers();assert(displayedFinal.soul==25 && grantCalls==0);
    int previews=previewCalls;OnSinUpdate(s,m);assert(previewCalls==previews && grantCalls==0);
    data.rewardMultiplier=5000;OnSinUpdate(s,m);ExpectBuffers();
    assert(previewCalls==previews+1 && grantCalls==0 && displayedFinal.soul==50 && displayedFinal.gem==25);
    OnGetReward(s,m);ExpectBuffers();assert(grantCalls==1 && granted.soul==50 && granted.gem==25 && !rewardScreen);
    OnSinUpdate(s,m);assert(previewCalls==previews+1 && grantCalls==1);
    OnSetReward(s,m);ds::Disable(&data);OnSinUpdate(s,m);ExpectBuffers();
    assert(displayedBase.soul==7 && displayedFinal.soul==10 && displayedFinal.gem==5 && grantCalls==1);
    OnClearDisable(s,m);assert(clearDisableCalls==1 && !rewardScreen);
    data.enabled=ds::Reward;data.rewardMultiplier=2500;OnSinUpdate(s,m);assert(grantCalls==1);
    // A concurrent panel edit cannot mix factors halfway through one operation.
    changePreviewFactor=true;OnSetReward(s,m);ExpectBuffers();changePreviewFactor=false;
    assert(displayedFinal.soul==25 && displayedFinal.gem==13 && data.rewardMultiplier==ds::MaxMultiplier);
    data.rewardMultiplier=2500;clearClaimBuffer=true;OnGetReward(s,m);clearClaimBuffer=false;
    RewardBuffers after{};Field(playerBuffer,0x218,after);assert(after.gem==0 && after.soul==7 && !settlement);
    ResetBuffers();data.enabled=0;OnGetReward(s,m);ExpectBuffers();assert(granted.soul==10 && granted.gem==5);
    data.enabled=ds::Reward;data.clientHeartbeat=LONG(GetTickCount()-4000);OnSetReward(s,m);ExpectBuffers();
    assert(displayedFinal.soul==10 && displayedFinal.gem==5);
    data.clientHeartbeat=LONG(GetTickCount());noPlayer=true;
    {SettlementScope missing(2500);assert(!settlement);} noPlayer=false;
    assert(!WriteField(reinterpret_cast<void*>(1),0x218,0));
    WriteField(playerBuffer,0x218,RewardBuffers{0,0,-2,0});OnSetReward(s,m);
    assert(displayedFinal.prayer==0 && displayedFinal.soul==0 && displayedBase.breach==-2 && displayedFinal.gem==0);
    ResetBuffers();data.enabled=ds::Reward;data.rewardMultiplier=2500;rewardScreen=nullptr;
}
std::array<int,4> bank{};
int balanceReads=0, savedAmount=0, savedKind=-1, saveCalls=0, metadataToken=0;
bool savedOption=false;
void* singletonUsage=nullptr;
void __fastcall MetadataStub(void* slot) { assert(slot==&singletonUsage);singletonUsage=&metadataToken; }
void* __fastcall ControllerStub(const void* method) { assert(method==&metadataToken);return &bank; }
template<Currency kind> int __fastcall BalanceStub(void* self,const void*) {
    assert(self==&bank);++balanceReads;return bank[size_t(kind)];
}
template<Currency kind> __declspec(noinline) void __fastcall SaveFixture(void* self,int amount,bool option,const void* method) {
    assert(self==&bank);savedKind=int(kind);savedAmount=amount;savedOption=option;selfSeen=self;methodSeen=method;++saveCalls;
    const int64_t total=int64_t(bank[size_t(kind)])+amount;
    assert(total>=INT_MIN && total<=INT_MAX);bank[size_t(kind)]=int(total);
}
void __fastcall ClaimAndSaveStub(void* s,const void* m) {
    ClaimFixture(s,m);
    OnSavePrayer(&bank,granted.prayer,false,m);OnSaveSoul(&bank,granted.soul,true,m);
    OnSaveBreach(&bank,granted.breach,false,m);OnSaveGem(&bank,granted.gem,true,m);
}
void RewardLimitTests(ds::Shared& data,void* s,const void* m) {
    initializeMetadata=MetadataStub;getDataController=ControllerStub;dataControllerMethodUsage=&singletonUsage;
    loadCurrency={BalanceStub<Currency::Prayer>,BalanceStub<Currency::Soul>,BalanceStub<Currency::Breach>,BalanceStub<Currency::Gem>};
    originalCurrencySave={SaveFixture<Currency::Prayer>,SaveFixture<Currency::Soul>,SaveFixture<Currency::Breach>,SaveFixture<Currency::Gem>};
    originalGetReward=ClaimAndSaveStub;data.enabled=ds::Reward;data.rewardMultiplier=ds::MaxMultiplier;
    bank.fill(0);WriteField(playerBuffer,0x218,RewardBuffers{3000,7000,11000,5000});
    OnSetReward(s,m);
    assert(displayedBase.soul==7000000 && displayedFinal.soul==10500000 && displayedFinal.gem==5000000);
    OnSetReward(s,m);assert(displayedFinal.soul==10500000);
    OnGetReward(s,m);
    assert(granted.prayer==4500000 && granted.soul==10500000 && granted.breach==16500000 && granted.gem==5000000);
    assert(bank[0]==granted.prayer && bank[1]==granted.soul && bank[2]==granted.breach && bank[3]==granted.gem);
    RewardBuffers after{};Field(playerBuffer,0x218,after);assert(after.soul==7000 && after.gem==5000 && !settlement);
    // Preview and grant clip only to the available signed-int balance space.
    bank={INT_MAX-101,INT_MAX-202,INT_MAX-303,INT_MAX-404};
    OnSetReward(s,m);assert(displayedFinal.prayer==101 && displayedFinal.soul==202 && displayedFinal.breach==303 && displayedFinal.gem==404);
    OnGetReward(s,m);for(int amount:bank)assert(amount==INT_MAX);
    // Main reward can fill the balance; a later quest credit must re-read it.
    {SettlementScope scope(data.rewardMultiplier);
        OnSaveSoul(&bank,1000,true,m);assert(savedAmount==0 && bank[1]==INT_MAX && savedOption && methodSeen==m);
        OnSaveSoul(&bank,-7,false,m);assert(savedAmount==-7 && bank[1]==INT_MAX-7 && !savedOption);
        OnSaveSoul(&bank,1000,true,m);assert(savedAmount==7 && bank[1]==INT_MAX);
    }
    // With a full balance, preview/grant are zero, not one and never negative.
    OnSetReward(s,m);assert(displayedFinal.soul==0 && displayedFinal.gem==0);
    OnGetReward(s,m);for(int amount:bank)assert(amount==INT_MAX);
    bank.fill(1000000);WriteField(playerBuffer,0x218,RewardBuffers{2000000,2000000,2000000,2000000});
    OnSetReward(s,m);assert(displayedFinal.soul==INT_MAX-1000000);
    OnGetReward(s,m);assert(granted.soul==INT_MAX-1000000 && bank[1]==INT_MAX);
    // Actual four-argument save detours preserve sync/tracking and MethodInfo.
    CurrencySave fixtures[]={SaveFixture<Currency::Prayer>,SaveFixture<Currency::Soul>,SaveFixture<Currency::Breach>,SaveFixture<Currency::Gem>};
    CurrencySave detours[]={OnSavePrayer,OnSaveSoul,OnSaveBreach,OnSaveGem};
    for(size_t i=0;i<4;++i) {
        assert(MH_CreateHook(reinterpret_cast<void*>(fixtures[i]),reinterpret_cast<void*>(detours[i]),reinterpret_cast<void**>(&originalCurrencySave[i]))==MH_OK);
        assert(MH_EnableHook(reinterpret_cast<void*>(fixtures[i]))==MH_OK);
    }
    bank.fill(INT_MAX-9);
    {SettlementScope scope(data.rewardMultiplier);
        for(size_t i=0;i<4;++i) {
            CurrencySave volatile invoke=fixtures[i];invoke(&bank,20,i%2==0,m);
            assert(savedAmount==9 && savedKind==int(i) && savedOption==(i%2==0) && selfSeen==&bank && methodSeen==m && bank[i]==INT_MAX);
        }
    }
    for(size_t i=0;i<4;++i) {
        assert(MH_DisableHook(reinterpret_cast<void*>(fixtures[i]))==MH_OK && MH_RemoveHook(reinterpret_cast<void*>(fixtures[i]))==MH_OK);
    }
    originalCurrencySave={fixtures[0],fixtures[1],fixtures[2],fixtures[3]};
    bank.fill(0);const int queries=balanceReads;
    // Outside a settlement, saves pass through; no trainer multiplication.
    OnSaveSoul(&bank,12,false,m);assert(bank[1]==12 && savedAmount==12 && balanceReads==queries);
    data.enabled=0;OnSetReward(s,m);assert(displayedFinal.soul==3000000 && balanceReads==queries);
    data.enabled=ds::Reward;data.rewardMultiplier=1000;bank.fill(INT_MAX);
    OnSetReward(s,m);assert(displayedFinal.soul==0);
    initializeMetadata=nullptr;getDataController=nullptr;dataControllerMethodUsage=nullptr;loadCurrency.fill(nullptr);
    originalGetReward=ClaimFixture;ResetBuffers();rewardScreen=nullptr;data.rewardMultiplier=2500;
}
__declspec(noinline) int Fixture(int value) { volatile int result=value; result=result+1; return result; }
int(__fastcall* originalFixture)(int)=nullptr;
int __fastcall FixtureHook(int value) { return originalFixture(value)*2; }
}
#include "movement-tests.inl"
#include "artifact-tests.inl"
#include "inventory-skills-tests.inl"
#include "currency-tests.inl"
#include "preferences-tests.inl"
#include "unlock-tests.inl"
#include "wallet-tests.inl"
#include "rune-tests.inl"
#include "sync-cooldown-tests.inl"
int main(int argc,char** argv) {
    _set_error_mode(_OUT_TO_STDERR);
    std::cout<<std::unitbuf;
    if(argc==2 && strcmp(argv[1],"--overlay-only")==0){OverlaySmokeTest();return 0;}
    if(argc==3 && strcmp(argv[1],"--discovery-fixture")==0)return RunDiscoveryFixture(argv[2]);
    DiscoveryTests();
    PreferencesTests();
    ds::Shared data{}; shared=&data;
    data.state=ds::Ready; data.available=ds::All; data.enabled=0;
    data.clientHeartbeat=LONG(GetTickCount()); data.damageMultiplier=3500;data.goldMultiplier=5000;data.rewardMultiplier=10000;
    originalHit=HitStub;originalEventHit=EventStub;originalStatusHit=StatusStub;
    originalHealthDamage=HealthStub;originalDirectDamage=DirectStub;originalGold=GoldStub;
    originalSoul=RewardStub;originalPrayer=RewardStub;originalBreach=RewardStub;
    int dummy=0,info=0,method=0;
    OnHit(&dummy,&info,&method); assert(hitCalls==1 && selfSeen==&dummy && infoSeen==&info && methodSeen==&method);
    OnHealthDamage(&dummy,20,&method);assert(damageSeen==20);
    OnGold(&dummy,20,true,false,&method);assert(goldSeen==20 && ignoreMoreSeen && !ignoreMagSeen);
    assert(OnSoul(&dummy,&method)==7);
    data.enabled=ds::All;
    OnHit(&dummy,&info,&method);OnEventHit(&dummy,5,true,&method);OnStatusHit(&dummy,3,&method);
    assert(hitCalls==1 && eventCalls==0 && statusCalls==0 && data.godHits==3);
    OnEventHit(&dummy,0,true,&method); assert(eventCalls==1 && damageSeen==0 && ignoreMoreSeen);
    OnHealthDamage(&dummy,20,&method);assert(damageSeen==70 && data.lastDamageAfter==70);
    assert(OnDirectDamage(&dummy,20,&info,5,&method)==77 && damageSeen==70 && minimumSeen==5 && infoSeen==&info);
    OnGold(&dummy,20,false,true,&method);assert(goldSeen==100 && !ignoreMoreSeen && ignoreMagSeen && selfSeen==&dummy && methodSeen==&method);
    OnGold(&dummy,-20,true,false,&method);assert(goldSeen==-20);
    assert(OnSoul(&dummy,&method)==70 && OnSoul(&dummy,&method)==70 && OnPrayer(&dummy,&method)==70 && OnBreach(&dummy,&method)==70);
    assert(data.rewardHits==4);rewardBase=0;assert(OnSoul(&dummy,&method)==0 && data.rewardHits==4);
    assert(ds::ScalePositive(INT_MAX,1000000)==INT_MAX);
    assert(ds::ScalePositive(2000,1000000,ds::MaxGoldGain)==ds::MaxGoldGain);
    assert(ds::ScaleReward(20000,1000000)==20000000);
    assert(ds::ScaleReward(INT_MAX,1000000)==INT_MAX);
    assert(ds::RewardRoom(INT_MAX)==0 && ds::RewardRoom(INT_MAX-9)==9 && ds::RewardRoom(-1)==INT_MAX);
    assert(ds::ScaleReward(20,1000000,9)==9 && ds::ScaleReward(20,1000000,0)==0 && ds::ScaleReward(-7,1000000,0)==-7);
    assert(ds::ScalePositive(-1,1000000)==-1 && ds::ScalePositive(0,1000000)==0);
    assert(ds::ScalePositive(1,100)==1 && ds::ScalePositive(101,1500)==152);
    assert(ds::Fresh(20,0xfffffff0));assert(!ds::Fresh(4000,500));
    data.clientHeartbeat=LONG(GetTickCount()-4000);
    OnHit(&dummy,&info,&method);OnHealthDamage(&dummy,20,&method);OnGold(&dummy,20,false,false,&method);rewardBase=7;
    assert(hitCalls==2 && damageSeen==20 && goldSeen==20 && OnSoul(&dummy,&method)==7);
    data.clientHeartbeat=LONG(GetTickCount());ds::Disable(&data); assert(data.enabled==0);
    OnHealthDamage(&dummy,20,&method);assert(damageSeen==20);
    originalSkin=TwoStub;originalWeapon=TwoStub;originalPet=OneStub;originalCharacter=OneStub;originalLobby=OneStub;
    getCharacter=CharacterStub;getWeapon=WeaponStub;getPet=PetStub;
    Put(characterAsset,0x18,3);Put(characterAsset,0x48,static_cast<void*>(skinList));
    Put(skinList,0x10,static_cast<void*>(skinArray));Put(skinList,0x18,1);Put(skinArray,0x18,uintptr_t(1));Put(skinArray,0x20,static_cast<void*>(skinAsset));
    Put(weaponAsset,0x18,912);Put(petAsset,0x18,7);
    assert(!OnSkin(&dummy,3,0,&method) && !OnWeapon(&dummy,3,912,&method) && !OnPet(&dummy,7,&method));
    data.enabled=ds::Skins|ds::Weapons|ds::Pets|ds::Characters;
    assert(OnSkin(&dummy,3,0,&method) && OnWeapon(&dummy,3,912,&method) && OnPet(&dummy,7,&method) && OnCharacter(&dummy,3,&method) && OnLobby(&dummy,4,&method));
    assert(data.unlockHits==5 && selfSeen==&dummy && minimumSeen==4 && methodSeen==&method);
    assert(!OnSkin(&dummy,3,-1,&method) && !OnSkin(&dummy,3,1,&method) && !OnSkin(&dummy,22,0,&method));
    assert(!OnWeapon(&dummy,3,999,&method) && !OnPet(&dummy,8,&method) && !OnCharacter(&dummy,1,&method) && !OnLobby(&dummy,5,&method));
    skinAsset[0x1c]=1;weaponAsset[0x25]=1;petAsset[0x1d]=1;
    assert(!OnSkin(&dummy,3,0,&method) && !OnWeapon(&dummy,3,912,&method) && !OnPet(&dummy,7,&method));
    skinAsset[0x1c]=0;weaponAsset[0x25]=0;petAsset[0x1d]=0;characterAsset[0x1c]=1;
    assert(!OnCharacter(&dummy,3,&method) && !OnSkin(&dummy,3,0,&method) && !OnWeapon(&dummy,3,912,&method));
    characterAsset[0x1c]=0;
    data.clientHeartbeat=LONG(GetTickCount()-4000);assert(!OnSkin(&dummy,3,0,&method) && !OnPet(&dummy,7,&method));
    data.clientHeartbeat=LONG(GetTickCount());characterFixtureId=22;petFixtureId=35;
    Put(characterAsset,0x18,22);Put(petAsset,0x18,35);
    assert(OnCharacter(&dummy,22,&method) && OnSkin(&dummy,22,0,&method) && OnWeapon(&dummy,22,912,&method) && OnPet(&dummy,35,&method));
    characterAsset[0x1c]=1;petAsset[0x1d]=1;
    assert(!OnCharacter(&dummy,22,&method) && !OnPet(&dummy,35,&method));
    characterAsset[0x1c]=petAsset[0x1d]=0;characterFixtureId=3;petFixtureId=7;Put(characterAsset,0x18,3);Put(petAsset,0x18,7);
    owned=true;assert(OnWeapon(&dummy,1,999,&method));owned=false;
    int invalid=0;assert(!Field(reinterpret_cast<void*>(1),0x18,invalid));
    data.clientHeartbeat=LONG(GetTickCount());ds::SetFeature(&data,ds::God,true);assert((data.enabled&ds::God)!=0);
    ds::SetFeature(&data,ds::God,false);assert((data.enabled&ds::God)==0 && (data.enabled&ds::Skins)!=0);
    bool keyDown=false;data.panelVisible=0;
    ds::PanelKey(&data,true,true,keyDown);assert(data.panelVisible==1);
    ds::PanelKey(&data,true,true,keyDown);assert(data.panelVisible==1);
    ds::PanelKey(&data,false,true,keyDown);ds::PanelKey(&data,true,true,keyDown);assert(data.panelVisible==0);
    ds::PanelKey(&data,false,false,keyDown);ds::PanelKey(&data,true,false,keyDown);assert(data.panelVisible==0);
    // Display scaling uses the same rounding/cap as HP, but does not change HP,
    // hit counters, a critical flag, color, coordinates or the scheduled delay.
    data.enabled=ds::Damage;data.damageMultiplier=3500;gameBase=0x180000000;
    const auto hitCount=data.damageHits;
    for(const auto& source:ds::FontCallSites) {
        assert(DamageFontValue(20,gameBase+source.rva+5)==70);
        assert(DamageFontValue(20,gameBase+source.rva+5)==70);
        assert(DamageFontValue(0,gameBase+source.rva+5)==0);
        assert(DamageFontValue(-1,gameBase+source.rva+5)==-1);
        assert(DamageFontValue(INT_MAX,gameBase+source.rva+5)==INT_MAX);
        assert(DamageFontValue(20,gameBase+source.rva+6)==20);
    }
    // These scripted fonts bypass scaled health or can already contain totals.
    assert(DamageFontValue(20,gameBase+0x1880511)==20 && DamageFontValue(20,gameBase+0x8c2cdf)==20);
    assert(DamageFontValue(70,gameBase+0x112b825)==70 && DamageFontValue(20,gameBase-1)==20);
    assert(data.damageHits==hitCount);
    data.enabled=0;assert(DamageFontValue(20,gameBase+ds::FontCallSites[0].rva+5)==20);
    data.enabled=ds::Damage;data.clientHeartbeat=LONG(GetTickCount()-4000);
    assert(DamageFontValue(20,gameBase+ds::FontCallSites[0].rva+5)==20);
    data.clientHeartbeat=LONG(GetTickCount());
    alignas(16) unsigned char position[16]{1,0,0,0};float color[4]={.1f,.2f,.3f,.4f};
    originalDamageFont=FontStub;gameBase=0;
    OnDamageFont(&dummy,20,true,position,color,1400,true,1.25f,&method);
    assert(fontDamageSeen==20 && fontCriticalSeen && fontAdditionalSeen && fontDelaySeen==1.25f &&
        selfSeen==&dummy && fontPositionSeen==position && fontColorSeen==color && fontSpecialSeen==1400 && methodSeen==&method);
    // Exercise the actual bundled x64 MinHook implementation and trampoline restore.
    assert(MH_Initialize()==MH_OK);
    assert(MH_CreateHook(reinterpret_cast<void*>(&Fixture),reinterpret_cast<void*>(&FixtureHook),reinterpret_cast<void**>(&originalFixture))==MH_OK);
    int (*volatile invoke)(int)=Fixture;
    assert(invoke(9)==10);assert(MH_EnableHook(reinterpret_cast<void*>(&Fixture))==MH_OK);assert(invoke(9)==20);
    assert(MH_DisableHook(reinterpret_cast<void*>(&Fixture))==MH_OK);assert(invoke(9)==10);
    assert(MH_RemoveHook(reinterpret_cast<void*>(&Fixture))==MH_OK);
    SinAndSettlementTests(data,&dummy,&method);
    RewardLimitTests(data,&dummy,&method);
    CurrencyMultiplierTests(data,&dummy,&method);
    MovementTests(data,&method);
    ArtifactTests(data,&method);
    InventorySkillTests(data,&method);
    // Exercise both settlement detours with the real Windows x64 calling ABI.
    assert(MH_CreateHook(reinterpret_cast<void*>(&PreviewFixture),reinterpret_cast<void*>(&OnSetReward),reinterpret_cast<void**>(&originalSetReward))==MH_OK);
    assert(MH_CreateHook(reinterpret_cast<void*>(&ClaimFixture),reinterpret_cast<void*>(&OnGetReward),reinterpret_cast<void**>(&originalGetReward))==MH_OK);
    assert(MH_EnableHook(reinterpret_cast<void*>(&PreviewFixture))==MH_OK && MH_EnableHook(reinterpret_cast<void*>(&ClaimFixture))==MH_OK);
    data.enabled=ds::Reward;data.rewardMultiplier=2500;ResetBuffers();
    GameAction volatile preview=PreviewFixture, claim=ClaimFixture;
    const int claimsBefore=grantCalls;preview(&dummy,&method);ExpectBuffers();claim(&dummy,&method);ExpectBuffers();
    assert(grantCalls==claimsBefore+1 && displayedFinal.soul==granted.soul && displayedFinal.gem==granted.gem && granted.soul==25 && granted.gem==13);
    assert(selfSeen==&dummy && methodSeen==&method);
    assert(MH_DisableHook(reinterpret_cast<void*>(&PreviewFixture))==MH_OK && MH_RemoveHook(reinterpret_cast<void*>(&PreviewFixture))==MH_OK);
    assert(MH_DisableHook(reinterpret_cast<void*>(&ClaimFixture))==MH_OK && MH_RemoveHook(reinterpret_cast<void*>(&ClaimFixture))==MH_OK);
    rewardScreen=nullptr;data.enabled=ds::Damage;
    // Exercise the real floating-text detour with all nine native arguments and
    // an observed caller address, rather than just testing an arithmetic helper.
    void* ignoredOriginal=nullptr;
    assert(MH_CreateHook(reinterpret_cast<void*>(&FontFixture),reinterpret_cast<void*>(&RecordFontCaller),&ignoredOriginal)==MH_OK);
    assert(MH_EnableHook(reinterpret_cast<void*>(&FontFixture))==MH_OK);
    InvokeFontFixture(&dummy,20,true,position,color,1400,true,1.25f,&method);assert(fontCaller!=0);
    gameBase=fontCaller-(ds::FontCallSites[0].rva+5);
    assert(MH_DisableHook(reinterpret_cast<void*>(&FontFixture))==MH_OK && MH_RemoveHook(reinterpret_cast<void*>(&FontFixture))==MH_OK);
    assert(MH_CreateHook(reinterpret_cast<void*>(&FontFixture),reinterpret_cast<void*>(&OnDamageFont),reinterpret_cast<void**>(&originalDamageFont))==MH_OK);
    assert(MH_EnableHook(reinterpret_cast<void*>(&FontFixture))==MH_OK);
    OnHealthDamage(&dummy,20,&method);assert(damageSeen==70);const auto scaledCount=data.damageHits;
    InvokeFontFixture(&dummy,20,true,position,color,1400,true,1.25f,&method);
    assert(fontDamageSeen==70 && damageSeen==70 && data.damageHits==scaledCount);
    assert(fontCriticalSeen && fontAdditionalSeen && fontDelaySeen==1.25f && fontPositionSeen==position && fontColorSeen==color && fontSpecialSeen==1400 && methodSeen==&method);
    InvokeFontFixture(&dummy,20,false,position,color,17,false,0.0f,&method);
    assert(fontDamageSeen==70 && !fontCriticalSeen && !fontAdditionalSeen && fontDelaySeen==0 && fontSpecialSeen==17 && data.damageHits==scaledCount);
    ds::Disable(&data);InvokeFontFixture(&dummy,20,true,position,color,1400,true,1.25f,&method);assert(fontDamageSeen==20);
    assert(MH_DisableHook(reinterpret_cast<void*>(&FontFixture))==MH_OK && MH_RemoveHook(reinterpret_cast<void*>(&FontFixture))==MH_OK);
    InvokeFontFixture(&dummy,20,true,position,color,1400,true,1.25f,&method);assert(fontDamageSeen==20);
    unlockFixture::Tests(data);
    walletFixture::Tests(data);
    runeFixture::Tests(data);
    syncCooldownFixture::Tests(data);
    assert(MH_Uninitialize()==MH_OK);
    std::cout<<"PASS: reward over 1 million (10.5M soul preview/credit), signed-int balance headroom, zero/full balances, sequential quest credits, 4 native save detours and argument forwarding, native routes, sin total 0..140, settlement preview/claim parity incl. gems, native bonus rounding, no compounded buffers, main-thread refresh without grants, native settlement detours, gains/caps, heartbeat, asset-bound unlocks, floating damage routes + nine-argument detour, no double HP scaling, x64 restore\n";
    OverlaySmokeTest();
}
