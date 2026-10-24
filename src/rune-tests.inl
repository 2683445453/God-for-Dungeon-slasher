#include <set>
namespace runeFixture {
using Bytes=std::array<unsigned char,0xd0>;
Bytes rune{},row{};std::array<Bytes,3> options{};
int nativeDraws=0,refines=0,refreshes=0,material=100;bool refuse=false;
const void* expectedMethod=nullptr;
__declspec(noinline) int __fastcall Draw(void* table,const void* method) {assert(table==row.data() && method==expectedMethod);++nativeDraws;return 6;}
__declspec(noinline) bool __fastcall Refine(void* object,int* reason,const void* method) {
    assert(object==rune.data() && method==expectedMethod);++refines;*reason=0;
    if(refuse){*reason=3;return false;}
    for(auto& option:options) {bool locked=false;Field(option.data(),0x19,locked);if(!locked){RuneDraw volatile fn=Draw;WriteField(option.data(),0x14,fn(row.data(),method));}}
    return true;
}
__declspec(noinline) bool __fastcall Refresh(void* object,int index,void* exclude,int* reason,const void* method) {
    assert(object==rune.data() && index==1 && exclude==&material && method==expectedMethod);++refreshes;*reason=0;
    RuneDraw volatile fn=Draw;WriteField(options[index].data(),0x14,fn(row.data(),method));return true;
}
bool PaidRefine(int* reason,const void* method) {if(material<10)return false;RuneRefine volatile fn=Refine;if(!fn(rune.data(),reason,method))return false;material-=10;return true;}
std::vector<Bytes> generated(30000);int generatedCount=0,roll=0,randomCalls=0,rarityCalls=0,registrations=0;
std::vector<PendingRune> pending;int codeValue=1,service=1;
uintptr_t rollCaller=0;bool nativeFailure=false,registrationFailure=false;
RandomNext observingOriginal=nullptr;
__declspec(noinline) int __fastcall Random(void* self,int minimum,int maximum,const void*) {
    assert(self==&service && minimum==0);++randomCalls;
    if(maximum==100){++rarityCalls;return 83;}
    assert(maximum==10000);return roll;
}
__declspec(noinline) int __fastcall RecordRandom(void* self,int minimum,int maximum,const void* method) {
    if(maximum==10000)rollCaller=reinterpret_cast<uintptr_t>(_ReturnAddress());return observingOriginal(self,minimum,maximum,method);
}
bool __fastcall Chance(void* self,int rank,void* code,int* chance,bool* boss,const void*) {
    assert(self==&service);if(code!=&codeValue || (rank!=1 && rank!=2))return false;*boss=rank==2;*chance=*boss?100:2;return true;
}
__declspec(noinline) bool __fastcall Build(void* self,int rank,void* code,PendingRune* output,const void* method) {
    assert(self==&service && method==expectedMethod);*output={};int chance=0;bool boss=false;
    if(!Chance(self,rank,code,&chance,&boss,method) || nativeFailure)return false;
    RandomNext volatile draw=Random;
    if(draw(self,0,10000,method)>=chance)return false;
    const int rarity=draw(self,0,100,method);assert(rarity==83); // unmodified second RNG draw
    auto& made=generated.at(generatedCount++);Put(made.data(),0x18,2);Put(made.data(),0x10,generatedCount);
    output->rewardType=1;output->rune=made.data();output->sourceMode=3;output->code=code;output->rank=rank;return true;
}
__declspec(noinline) bool __fastcall Register(void* self,PendingRune* output,const void* method) {
    assert(self==&service && method==expectedMethod);++registrations;
    if(registrationFailure || output->sourceMode!=3 || !output->rune || !output->code || output->rewardType<=0)return false;
    pending.push_back(*output);return true;
}
__declspec(noinline) int __fastcall Display(void* self,bool boss,const void* method){assert(self==&service && method==expectedMethod);return boss?100:2;}
int __fastcall PendingCount(void* self,const void*){assert(self==&service);return int(pending.size());}
uintptr_t __cdecl Root(void* object,bool){return reinterpret_cast<uintptr_t>(object);}
void __cdecl Free(uintptr_t){}
void* __cdecl RootTarget(uintptr_t handle){return reinterpret_cast<void*>(handle);}
void Tests(ds::Shared& state) {
    const uintptr_t previousBase=gameBase;const auto previousApi=artifactApi;
    state.enabled=ds::RuneMaximum;state.clientHeartbeat=LONG(GetTickCount());expectedMethod=nullptr;
    void* targets[]={reinterpret_cast<void*>(&Refine),reinterpret_cast<void*>(&Refresh),reinterpret_cast<void*>(&Draw),reinterpret_cast<void*>(&Build),reinterpret_cast<void*>(&Register),reinterpret_cast<void*>(&Display)};
    void* hooks[]={reinterpret_cast<void*>(&OnRuneRefine),reinterpret_cast<void*>(&OnRuneRefresh),reinterpret_cast<void*>(&OnRuneDraw),reinterpret_cast<void*>(&OnChallengeBuild),reinterpret_cast<void*>(&OnChallengeRegister),reinterpret_cast<void*>(&OnChallengeDisplay)};
    void** originals[]={reinterpret_cast<void**>(&originalRuneRefine),reinterpret_cast<void**>(&originalRuneRefresh),reinterpret_cast<void**>(&originalRuneDraw),reinterpret_cast<void**>(&originalChallengeBuild),reinterpret_cast<void**>(&originalChallengeRegister),reinterpret_cast<void**>(&originalChallengeDisplay)};
    for(size_t i=0;i<std::size(targets);++i){assert(MH_CreateHook(targets[i],hooks[i],originals[i])==MH_OK);assert(MH_EnableHook(targets[i])==MH_OK);}
    for(int rank=1;rank<=4;++rank) {
        Put(rune.data(),0x18,rank);Put(rune.data(),0x1c,2);Put(row.data(),0x30,rank);
        for(int i=0;i<11;++i)Put(row.data(),0xa0+i*4,i<(rank==1?9:11)?1.0f:0.0f);
        for(auto& option:options){Put(option.data(),0x14,2);Put(option.data(),0x19,false);}
        Put(options[1].data(),0x19,true);int reason=-1;
        assert(PaidRefine(&reason,nullptr));assert(material==100-rank*10 && !reason);
        for(int i:{0,2}){int step=0;Field(options[i].data(),0x14,step);assert(step==(rank==1?9:11));}
        int locked=0,quality=0,color=0;Field(options[1].data(),0x14,locked);Field(rune.data(),0x18,quality);Field(rune.data(),0x1c,color);
        assert(locked==2 && quality==rank && color==2 && maximizingRuneRank==0);
        RuneRefresh volatile refresh=Refresh;assert(refresh(rune.data(),1,&material,&reason,nullptr));Field(options[1].data(),0x14,locked);assert(locked==(rank==1?9:11));
    }
    RuneDraw volatile draw=Draw;assert(draw(row.data(),nullptr)==6); // creation / drop draws outside reforge stay native
    for(int i=0;i<11;++i)Put(row.data(),0xa0+i*4,0.0f);
    {RuneMaximumScope scope(rune.data());assert(draw(row.data(),nullptr)==6);Put(row.data(),0xc8,std::numeric_limits<float>::quiet_NaN());assert(draw(row.data(),nullptr)==6);Put(row.data(),0xc4,1.0f);assert(draw(row.data(),nullptr)==10);Put(row.data(),0x30,1);assert(draw(row.data(),nullptr)==6);}
    int reason=0;refuse=true;const int fees=material;assert(!PaidRefine(&reason,nullptr) && material==fees && reason==3);refuse=false;
    state.clientHeartbeat=LONG(GetTickCount()-4000);RuneRefine volatile refine=Refine;assert(refine(rune.data(),&reason,nullptr));int staleStep=0;Field(options[0].data(),0x14,staleStep);assert(staleStep==6 && maximizingRuneRank==0);
    state.clientHeartbeat=LONG(GetTickCount());state.enabled=ds::ChallengeRate;
    getChallengeChance=Chance;getPendingChallengeCount=PendingCount;artifactApi.rootNew=Root;artifactApi.rootFree=Free;artifactApi.rootTarget=RootTarget;
    // Observe the actual indirect Win64 RNG call site, then exercise the real
    // hook using exactly that return address. Other random calls stay native.
    assert(MH_CreateHook(reinterpret_cast<void*>(&Random),reinterpret_cast<void*>(&RecordRandom),reinterpret_cast<void**>(&observingOriginal))==MH_OK);assert(MH_EnableHook(reinterpret_cast<void*>(&Random))==MH_OK);
    state.challengeNormalChance=state.challengeBossChance=10000;PendingRune drop{};ChallengeBuild volatile build=Build;assert(build(&service,1,&codeValue,&drop,nullptr));assert(rollCaller);
    gameBase=rollCaller-ds::ChallengeRollReturnRva;
    assert(MH_DisableHook(reinterpret_cast<void*>(&Random))==MH_OK && MH_RemoveHook(reinterpret_cast<void*>(&Random))==MH_OK);
    assert(MH_CreateHook(reinterpret_cast<void*>(&Random),reinterpret_cast<void*>(&OnRandomNext),reinterpret_cast<void**>(&originalRandomNext))==MH_OK);assert(MH_EnableHook(reinterpret_cast<void*>(&Random))==MH_OK);
    state.challengeNormalChance=3700;state.challengeBossChance=731;
    for(int rank:{1,2}) {
        int accepted=0;for(roll=0;roll<10000;++roll)if(build(&service,rank,&codeValue,&drop,nullptr))++accepted;
        assert(accepted==(rank==1?3700:731));assert(!challengeRandom);
    }
    state.challengeNormalChance=0;roll=0;assert(!build(&service,1,&codeValue,&drop,nullptr));
    state.challengeNormalChance=10000;roll=9999;assert(build(&service,1,&codeValue,&drop,nullptr));
    assert(!build(&service,3,&codeValue,&drop,nullptr));nativeFailure=true;assert(!build(&service,1,&codeValue,&drop,nullptr));nativeFailure=false;
    {ChallengeRandomScope scope(10000);assert(ChallengeRoll(9999,0,10000,gameBase+ds::ChallengeRollReturnRva)==0);assert(ChallengeRoll(83,0,100,gameBase+ds::ChallengeRollReturnRva)==83);assert(ChallengeRoll(9999,0,10000,gameBase+ds::ChallengeRollReturnRva+1)==9999);}
    RandomNext volatile rng=Random;assert(rng(&service,0,10000,nullptr)==9999);
    ChallengeDisplay volatile display=Display;assert(display(&service,false,nullptr)==10000 && display(&service,true,nullptr)==731);
    state.enabled=0;assert(!build(&service,1,&codeValue,&drop,nullptr) && display(&service,false,nullptr)==2);
    state.enabled=ds::ChallengeRate|ds::ChallengeQuantity;state.challengeNormalChance=10000;state.challengeQuantity=5;
    assert(build(&service,1,&codeValue,&drop,nullptr));ChallengeRegister volatile reg=Register;assert(reg(&service,&drop,nullptr));assert(pending.size()==5 && state.challengeExtraRunes==4);
    std::set<void*> unique;for(auto& entry:pending)unique.insert(entry.rune);assert(unique.size()==5 && !extraChallengeRune && !challengeRandom);
    const auto extra=state.challengeExtraRunes;registrationFailure=true;assert(!reg(&service,&drop,nullptr) && state.challengeExtraRunes==extra);registrationFailure=false;
    pending.resize(ds::MaxPendingChallengeRunes-1,drop);assert(reg(&service,&drop,nullptr) && pending.size()==ds::MaxPendingChallengeRunes && state.challengeExtraRunes==extra);
    pending.clear();state.challengeQuantity=1;assert(reg(&service,&drop,nullptr) && pending.size()==1 && state.challengeExtraRunes==extra);
    pending.clear();state.challengeQuantity=5;state.enabled=ds::ChallengeQuantity;roll=0;assert(build(&service,2,&codeValue,&drop,nullptr));roll=9999;assert(reg(&service,&drop,nullptr) && pending.size()==5); // quantity works without custom probability
    state.clientHeartbeat=LONG(GetTickCount()-4000);pending.clear();assert(reg(&service,&drop,nullptr) && pending.size()==1);state.clientHeartbeat=LONG(GetTickCount());
    assert(MH_DisableHook(reinterpret_cast<void*>(&Random))==MH_OK && MH_RemoveHook(reinterpret_cast<void*>(&Random))==MH_OK);
    for(auto* target:targets){assert(MH_DisableHook(target)==MH_OK && MH_RemoveHook(target)==MH_OK);}
    gameBase=previousBase;artifactApi=previousApi;getChallengeChance=nullptr;getPendingChallengeCount=nullptr;
    std::cout<<"PASS: 7 real x64 rune detours, rank 1..4 maxima/zero weights/NaN/mismatch/locks/fees, unscoped draws unchanged; all 10,000 probability rolls per monster group, 0/100 percent, native eligibility/rarity, exact RNG caller isolation, display parity, fresh bonus IDs, 1/5 quantity and 2000 pending guard, native rejection, independent switches and heartbeat\n";
}
}
