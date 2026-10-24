// These scopes alter native random draws only inside paid, validated reforge /
// refine operations. Rank, option locks, costs and the native UI remain native.
using RuneRefine=bool(__fastcall*)(void*,int*,const void*);
using RuneRefresh=bool(__fastcall*)(void*,int,void*,int*,const void*);
using RuneDraw=int(__fastcall*)(void*,const void*);
RuneRefine originalRuneRefine=nullptr;
RuneRefresh originalRuneRefresh=nullptr;
RuneDraw originalRuneDraw=nullptr;
thread_local int maximizingRuneRank=0;
struct RuneMaximumScope {
    int previous;
    explicit RuneMaximumScope(void* rune):previous(maximizingRuneRank) {
        int rank=0;
        if(ds::Active(shared,ds::RuneMaximum) && Field(rune,0x18,rank) && rank>=1 && rank<=4)maximizingRuneRank=rank;
    }
    ~RuneMaximumScope(){maximizingRuneRank=previous;}
};
bool __fastcall OnRuneRefine(void* rune,int* reason,const void* method) {
    RuneMaximumScope scope(rune);return originalRuneRefine(rune,reason,method);
}
bool __fastcall OnRuneRefresh(void* rune,int index,void* excluded,int* reason,const void* method) {
    RuneMaximumScope scope(rune);return originalRuneRefresh(rune,index,excluded,reason,method);
}
int __fastcall OnRuneDraw(void* row,const void* method) {
    const int native=originalRuneDraw(row,method);int rank=0;
    if(!maximizingRuneRank || !Field(row,0x30,rank) || rank!=maximizingRuneRank)return native;
    for(int step=11;step>=1;--step) {
        float rate=0;
        if(!Field(row,0xa0+(step-1)*sizeof(float),rate))return native;
        if(std::isfinite(rate) && rate>0) {if(shared)InterlockedIncrement64(&shared->runeMaxDraws);return step;}
    }
    return native;
}

// RunePendingDrop is a 56-byte value type. Win64 passes it through a pointer
// to caller storage; it is not an IL2CPP object with a 16-byte header.
struct PendingRune {
    int rewardType;int pad0;void* rune;
    int sourceMode;int pad1;void* code;
    int rank;int pad2;void* rewardKey;
    int difficulty;int pad3;
};
static_assert(sizeof(PendingRune)==0x38 && offsetof(PendingRune,rank)==0x20);
using ChallengeBuild=bool(__fastcall*)(void*,int,void*,PendingRune*,const void*);
using ChallengeRegister=bool(__fastcall*)(void*,PendingRune*,const void*);
using ChallengeChance=bool(__fastcall*)(void*,int,void*,int*,bool*,const void*);
using ChallengeDisplay=int(__fastcall*)(void*,bool,const void*);
using RandomNext=int(__fastcall*)(void*,int,int,const void*);
ChallengeBuild originalChallengeBuild=nullptr;
ChallengeRegister originalChallengeRegister=nullptr;
ChallengeChance getChallengeChance=nullptr;
ChallengeDisplay originalChallengeDisplay=nullptr;
RewardGain getPendingChallengeCount=nullptr;
RandomNext originalRandomNext=nullptr;
struct ChallengeRandomScope {
    int chance;
    ChallengeRandomScope* previous;
    explicit ChallengeRandomScope(int probability);
    ~ChallengeRandomScope();
};
thread_local ChallengeRandomScope* challengeRandom=nullptr;
thread_local bool extraChallengeRune=false;
ChallengeRandomScope::ChallengeRandomScope(int probability):chance(std::clamp(probability,0,10000)),previous(challengeRandom){challengeRandom=this;}
ChallengeRandomScope::~ChallengeRandomScope(){challengeRandom=previous;}
int ChallengeRoll(int roll,int minimum,int maximum,uintptr_t caller) {
    if(!challengeRandom || minimum!=0 || maximum!=10000 || !gameBase || caller!=gameBase+ds::ChallengeRollReturnRva)return roll;
    // The native inlined compare still uses 2 / 100 (or debug 1000). Return
    // values on either side of those positive thresholds for the desired rate.
    return roll>=0 && roll<challengeRandom->chance?0:9999;
}
__declspec(noinline) int __fastcall OnRandomNext(void* self,int minimum,int maximum,const void* method) {
    const uintptr_t caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    return ChallengeRoll(originalRandomNext(self,minimum,maximum,method),minimum,maximum,caller);
}
int ChallengeRate(bool boss) {return std::clamp(int(ds::Read(boss?shared->challengeBossChance:shared->challengeNormalChance)),0,10000);}
bool __fastcall OnChallengeBuild(void* self,int rank,void* code,PendingRune* drop,const void* method) {
    if(!getChallengeChance || (!extraChallengeRune && !ds::Active(shared,ds::ChallengeRate)))return originalChallengeBuild(self,rank,code,drop,method);
    int native=0;bool boss=false;
    if(!self || !code || !getChallengeChance(self,rank,code,&native,&boss,nullptr))return originalChallengeBuild(self,rank,code,drop,method);
    ChallengeRandomScope scope(extraChallengeRune?10000:ChallengeRate(boss));
    return originalChallengeBuild(self,rank,code,drop,method);
}
int __fastcall OnChallengeDisplay(void* self,bool boss,const void* method) {
    const int native=originalChallengeDisplay(self,boss,method);
    return ds::Active(shared,ds::ChallengeRate)?ChallengeRate(boss):native;
}
bool __fastcall OnChallengeRegister(void* self,PendingRune* drop,const void* method) {
    const bool added=originalChallengeRegister(self,drop,method);
    PendingRune source{};
    if(!added || extraChallengeRune || !ds::Active(shared,ds::ChallengeQuantity) || !Field(drop,0,source) || !source.rune || !source.code)return added;
    const int count=std::clamp(int(ds::Read(shared->challengeQuantity)),1,ds::MaxChallengeQuantity);
    // Every bonus rune goes through the native factory for a fresh GUID. Keep
    // deduplication, pending rewards, autosave, grant and overflow decomposition.
    ManagedRoot sourceRoot(artifactApi,source.rune),codeRoot(artifactApi,source.code);
    if(!sourceRoot.handle || !codeRoot.handle)return added;
    struct ExtraScope {ExtraScope(){extraChallengeRune=true;}~ExtraScope(){extraChallengeRune=false;}} scope;
    for(int i=1;i<count;++i) {
        if(!ds::Active(shared,ds::ChallengeQuantity))break;
        if(getPendingChallengeCount && getPendingChallengeCount(self,nullptr)>=ds::MaxPendingChallengeRunes)break;
        PendingRune bonus{};
        if(!OnChallengeBuild(self,source.rank,codeRoot.Get(),&bonus,nullptr) || !bonus.rune)break;
        ManagedRoot bonusRoot(artifactApi,bonus.rune);if(!bonusRoot.handle)break;
        if(!originalChallengeRegister(self,&bonus,method))break;
        InterlockedIncrement64(&shared->challengeExtraRunes);
    }
    return added;
}
