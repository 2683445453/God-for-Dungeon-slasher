#include <map>
namespace walletFixture {
using Bytes=std::array<unsigned char,0x250>;
Bytes controller{},file{},boxed{},player{};
std::map<std::string,int> balances;
DWORD threadId=0;int saves=0,syncs=0,updates=0,failAfterSaves=-1;
bool failSync=false,failRead=false,failSave=false,wrongValue=false,playerReady=true,clampNegative=false;
std::string Key(const char* name,void** args) {
    std::string key=name;if(key.starts_with("Save"))key.replace(0,4,"Load");
    if(key=="LoadSoulStoneCouponCount" || key=="LoadEventGoods" || key=="LoadPeriodEventGoods")key+=':'+std::to_string(*static_cast<int*>(args[0]));
    if(key.starts_with("LoadPeriodEventGoods"))key+=':'+std::to_string(*static_cast<int*>(args[1]));
    return key;
}
void* __cdecl ObjectClass(void* object){return object;}
const void* __cdecl Method(void*,const char* name,int count) {
    // Independent native signature list; wrong argc must fail like IL2CPP.
    int expected=name[0]=='L'?0:1;
    if(!strcmp(name,"get_ES3File") || !strcmp(name,"Sync"))expected=0;
    if(strstr(name,"SoulStoneCouponCount"))expected=name[0]=='L'?1:2;
    if(strstr(name,"PeriodEventGoods"))expected=name[0]=='L'?2:4;
    else if(strstr(name,"EventGoods"))expected=name[0]=='L'?1:3;
    const char* two[]={"SaveGem","SaveSoulStone","SavePrayerStone","SaveBreachStone","SaveMileagePoint","SaveRuneDust","SaveRuneKey","SaveRuneRerollScroll","SaveRuneRefineTool","SaveUniqueRuneGachaCoupon","SaveUniqueRuneSelectorCoupon"};
    for(auto* entry:two)if(!strcmp(name,entry))expected=2;
    assert(count==expected);return name;
}
std::string activeGemKey="LoadGem";
void AddBalance(int& balance,int amount) {
    const int64_t next=int64_t(balance)+amount;
    assert(next>=INT_MIN && next<=INT_MAX); // A native int add must never wrap.
    balance=clampNegative?int(std::max<int64_t>(0,next)):int(next);
}
void __fastcall GemSave(void*,int amount,bool sync,const void*) {assert(!sync);AddBalance(balances[activeGemKey],amount);}
void* __cdecl Invoke(const void* method,void* object,void** args,void** exception) {
    assert(GetCurrentThreadId()==threadId);*exception=nullptr;const char* name=static_cast<const char*>(method);
    if(!strcmp(name,"get_ES3File")){assert(object==controller.data());return file.data();}
    if(!strcmp(name,"Sync")){assert(object==file.data());++syncs;if(failSync)*exception=&file;return nullptr;}
    if(!strcmp(name,"SetGold")) {assert(object==player.data());Put(player.data(),0x1e4,*static_cast<int*>(args[0]));++saves;return nullptr;}
    assert(object==controller.data());auto key=Key(name,args);
    if(name[0]=='L') {if(failRead){*exception=&boxed;return nullptr;}Put(boxed.data(),0x10,balances[key]);return boxed.data();}
    if(failSave || (failAfterSaves>=0 && saves>=failAfterSaves)){*exception=&controller;return nullptr;}
    ++saves;int value=*static_cast<int*>(args[0]);
    bool absolute=false;
    const char* absoluteSaves[]={"SaveRuneDust","SaveRuneKey","SaveRuneRefineTool","SaveRuneRerollScroll","SaveUniqueRuneGachaCoupon","SaveUniqueRuneSelectorCoupon"};
    for(auto* entry:absoluteSaves)if(!strcmp(name,entry)){absolute=true;assert(!*static_cast<bool*>(args[1]));}
    if(!strcmp(name,"SaveSoulStoneCouponCount")){absolute=true;value=*static_cast<int*>(args[1]);const int type=*static_cast<int*>(args[0]);assert(type==150 || type==151 || type==200 || type==300);}
    if(!strcmp(name,"SaveEventGoods")){value=*static_cast<int*>(args[1]);assert(!*static_cast<bool*>(args[2]));assert(*static_cast<int*>(args[0])>=2 && *static_cast<int*>(args[0])<=10);}
    if(!strcmp(name,"SavePeriodEventGoods")){value=*static_cast<int*>(args[2]);assert(!*static_cast<bool*>(args[3]));assert(*static_cast<int*>(args[0])==1 || *static_cast<int*>(args[0])==2);assert(*static_cast<int*>(args[1])>=0 && *static_cast<int*>(args[1])<=2);}
    if(!strcmp(name,"SaveGem"))OnSaveGem(object,value,*static_cast<bool*>(args[1]),nullptr);
    else if(absolute)balances[key]=value;
    else AddBalance(balances[key],value);
    if(wrongValue)--balances[key];return nullptr;
}
void* __fastcall Player(const void*){return playerReady?player.data():nullptr;}
int __fastcall Decode(ObscuredIntBytes* value,const void*){int result=0;memcpy(&result,value->bytes,4);return result;}
uintptr_t __cdecl Root(void* object,bool){return reinterpret_cast<uintptr_t>(object);}
void __cdecl Free(uintptr_t){}
void* __cdecl RootTarget(uintptr_t handle){return reinterpret_cast<void*>(handle);}
__declspec(noinline) void __fastcall Update(void* object,const void*){assert(object==controller.data());InterlockedIncrement(reinterpret_cast<volatile LONG*>(&updates));}
void RunUpdate(){GameAction volatile call=Update;call(controller.data(),nullptr);InterlockedIncrement(&fixtureInvocationCount);}
void SeedBalance(int selected,int value) {
    const auto& entry=ds::Wallets[selected];int argument=entry.argument,index=entry.index;void* args[]={&argument,&index};
    if(entry.mode==ds::WalletMode::Gold)Put(player.data(),0x1e4,value);
    else balances[Key(entry.load,args)]=value;
}
void Tests(ds::Shared& state) {
    const auto oldApi=artifactApi;auto oldUpdate=originalDataUpdate;auto oldPlayer=getPlayerData;auto oldDecode=decodeMovementInt;auto oldGem=originalCurrencySave[3];
    threadId=GetCurrentThreadId();artifactApi={};artifactApi.objectClass=ObjectClass;artifactApi.findMethod=Method;artifactApi.invoke=Invoke;artifactApi.rootNew=Root;artifactApi.rootFree=Free;artifactApi.rootTarget=RootTarget;
    getPlayerData=Player;decodeMovementInt=Decode;originalCurrencySave[3]=GemSave;state.unlockCommand=0;state.panelVisible=0;
    state.enabled=ds::Diamond|ds::Reward;state.diamondMultiplier=10000;state.state=ds::Ready;state.clientHeartbeat=LONG(GetTickCount());
    assert(MH_CreateHook(reinterpret_cast<void*>(&Update),reinterpret_cast<void*>(&OnDataUpdate),reinterpret_cast<void**>(&originalDataUpdate))==MH_OK);assert(MH_EnableHook(reinterpret_cast<void*>(&Update))==MH_OK);
    // Every row: add, subtract to zero, INT_MAX, then restore. This catches
    // absolute rune saves being treated as delta, and index / enum collisions.
    for(int i=0;i<ds::WalletCount;++i) {
        state.walletSelection=i;
        for(int target:{37,0,INT_MAX,29}) {
            state.walletTargets[i]=target;assert(ds::RequestWallet(&state));assert(!ds::RequestWallet(&state));RunUpdate();
            assert(state.walletStatus==3 && state.walletBalance==target && state.walletReadIndex==i && !editingWallet);
        }
        const int previous=saves;assert(ds::RequestWallet(&state));RunUpdate();assert(saves==previous); // same target never adds twice
    }
    // Inactive activity storage remains independent, including zero-based tube
    // / coconut / answer-sheet indices and both distinct event years.
    assert(balances["LoadPeriodEventGoods:1:0"]==29 && balances["LoadPeriodEventGoods:1:1"]==29 && balances["LoadPeriodEventGoods:2:2"]==29);
    // Reproduce overflowed saves across every actual currency route, including
    // the INT_MIN -> INT_MAX span, absolute setters, and native zero clamps.
    for(int i=0;i<ds::WalletCount;++i) {
        state.walletSelection=i;
        for(bool nativeClamp:{false,true})for(int negative:{INT_MIN,-INT_MAX,-2500,-1})for(int target:{0,37,INT_MAX}) {
            clampNegative=nativeClamp;SeedBalance(i,negative);
            state.panelVisible=1;lastWalletSelection=-1;RunUpdate();state.panelVisible=0;
            assert(state.walletReadStatus==1 && state.walletBalance==negative);
            const int beforeWrites=saves,beforeSyncs=syncs;
            state.walletTargets[i]=target;assert(ds::RequestWallet(&state));RunUpdate();
            assert(state.walletStatus==3 && state.walletBalance==target && !editingWallet);
            assert(saves>beforeWrites && saves<=beforeWrites+3);
            assert(syncs==beforeSyncs+(i==4?0:1)); // Flush only the completed repair.
            const int repairedWrites=saves;assert(ds::RequestWallet(&state));RunUpdate();assert(saves==repairedWrites);
        }
        clampNegative=false;state.walletTargets[i]=29;assert(ds::RequestWallet(&state));RunUpdate();assert(state.walletStatus==3);
    }
    // A partial repair or failed local flush can be retried without adding
    // completed increments again. Keep genuine read/save errors distinct.
    state.walletSelection=0;state.walletTargets[0]=INT_MAX;SeedBalance(0,INT_MIN);
    failAfterSaves=saves+1;assert(ds::RequestWallet(&state));RunUpdate();
    assert(state.walletError==4 && balances["LoadGem"]==-1 && !editingWallet);
    failAfterSaves=-1;assert(ds::RequestWallet(&state));RunUpdate();assert(state.walletStatus==3 && balances["LoadGem"]==INT_MAX);
    SeedBalance(0,INT_MIN);failSync=true;assert(ds::RequestWallet(&state));RunUpdate();
    assert(state.walletError==6 && balances["LoadGem"]==INT_MAX);const int repairedWrites=saves;
    failSync=false;assert(ds::RequestWallet(&state));RunUpdate();assert(state.walletStatus==3 && saves==repairedWrites);
    state.walletTargets[0]=-1;assert(!ds::RequestWallet(&state));
    state.walletSelection=0;state.walletTargets[0]=100;assert(ds::RequestWallet(&state));state.walletSelection=7;state.walletTargets[0]=200;RunUpdate();assert(state.walletBalance==100 && state.walletRequestIndex==0);
    failSync=true;state.walletSelection=0;state.walletTargets[0]=111;assert(ds::RequestWallet(&state));RunUpdate();assert(state.walletStatus==-1 && state.walletError==6 && balances["LoadGem"]==111);
    failSync=false;const int writes=saves;assert(ds::RequestWallet(&state));RunUpdate();assert(state.walletStatus==3 && saves==writes && balances["LoadGem"]==111);
    failRead=true;assert(ds::RequestWallet(&state));RunUpdate();assert(state.walletStatus==-1 && state.walletError==3 && saves==writes);failRead=false;
    failSave=true;state.walletTargets[0]=112;assert(ds::RequestWallet(&state));RunUpdate();assert(state.walletError==4 && !editingWallet);failSave=false;
    wrongValue=true;assert(ds::RequestWallet(&state));RunUpdate();assert(state.walletError==5);wrongValue=false;
    state.walletSelection=4;playerReady=false;assert(ds::RequestWallet(&state));RunUpdate();assert(state.walletError==2);playerReady=true;
    state.walletSelection=0;state.walletTargets[0]=120;assert(ds::RequestWallet(&state));state.clientHeartbeat=LONG(GetTickCount()-4000);RunUpdate();assert(!state.walletCommand && state.walletStatus==0 && balances["LoadGem"]==111);
    assert(!ds::RequestWallet(&state));state.clientHeartbeat=LONG(GetTickCount());assert(ds::RequestWallet(&state));ds::Disable(&state);RunUpdate();assert(balances["LoadGem"]==111);
    state.panelVisible=1;state.walletSelection=35;lastWalletSelection=-1;RunUpdate();assert(state.walletReadIndex==35 && state.walletReadStatus==1 && state.walletBalance==29);state.panelVisible=0;
    assert(MH_DisableHook(reinterpret_cast<void*>(&Update))==MH_OK && MH_RemoveHook(reinterpret_cast<void*>(&Update))==MH_OK);
    artifactApi=oldApi;originalDataUpdate=oldUpdate;getPlayerData=oldPlayer;decodeMovementInt=oldDecode;originalCurrencySave[3]=oldGem;
    std::cout<<"PASS: all 41 currency rows through real x64 main-thread detour, signed negative-balance reads and INT_MIN..INT_MAX repair, checked native additions with/without zero clamp, absolute/delta routes, partial-save/flush retry without double credit, enum/date/index isolation, request capture, multiplier bypass, read/write/mismatch diagnostics, gold unavailable, cancellation/heartbeat\n";
}
}
