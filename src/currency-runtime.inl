// Native manual preview/claim, legacy single-rune decomposition, and overflow
// decomposition. Scale each operation's total, retaining native material checks.
using RuneDecomposeCheck = bool(__fastcall*)(void*,void*,const void*);
using RuneOverflow = int(__fastcall*)(void*,void*,const void*);
using RuneSingle = bool(__fastcall*)(void*,void*,int*,bool,const void*);
using RuneStateSave = void(__fastcall*)(void*,bool,const void*);
RuneDecomposeCheck originalRuneCheck=nullptr;
RuneOverflow originalRuneOverflow=nullptr;
RuneSingle originalRuneSingle=nullptr;
RuneStateSave originalRuneStateSave=nullptr;
RewardGain loadRuneDust=nullptr;
using RuneStateLoad=void*(__fastcall*)(void*,const void*);
RuneStateLoad loadRuneState=nullptr;

struct RuneCheckResult {
    bool canDecompose; unsigned char padding[3];
    int reason, targetCount, totalDustGain;
    bool containsEquipped; unsigned char tail[3];
};
static_assert(sizeof(RuneCheckResult)==20 && offsetof(RuneCheckResult,totalDustGain)==0xc);
int RuneFactor() { return ds::Active(shared,ds::RunePowder) ? ds::NormalizeMultiplier(ds::Read(shared->runePowderMultiplier)) : 1000; }
bool __fastcall OnRuneCheck(void* ids,void* output,const void* method) {
    const bool active=ds::Active(shared,ds::RunePowder);const int factor=RuneFactor();
    bool valid=originalRuneCheck(ids,output,method);
    RuneCheckResult result{};
    if(!valid || !active || !Field(output,0,result) || !result.canDecompose || result.totalDustGain<=0)return valid;
    void* controller=CurrentDataController();
    const int room=controller && loadRuneDust ? ds::RewardRoom(loadRuneDust(controller,nullptr)) : INT_MAX;
    result.totalDustGain=ds::ScaleReward(result.totalDustGain,factor,room);
    if(!result.totalDustGain) { result.canDecompose=false;result.reason=8;valid=false; }
    WriteField(output,0,result);
    return valid;
}
int __fastcall OnRuneOverflow(void* self,void* data,const void* method) {
    const bool active=ds::Active(shared,ds::RunePowder);const int factor=RuneFactor();int before=0;
    const bool readable=Field(data,0x20,before);
    int gain=originalRuneOverflow(self,data,method);
    if(active && readable && gain>0) {
        const int scaled=ds::ScaleReward(gain,factor,ds::RewardRoom(before));
        // Native overflow decomposition returns its total and modifies Dust
        // before AddRunes saves it. Replace that total before the native save.
        if(WriteField(data,0x20,std::max(0,before)+scaled))gain=scaled;
    }
    return gain;
}
struct RuneSingleScope { void* self;int* output;int before,factor;bool applied=false; };
thread_local RuneSingleScope* runeSingleScope=nullptr;
bool __fastcall OnRuneSingle(void* self,void* id,int* gain,bool upload,const void* method) {
    if(!ds::Active(shared,ds::RunePowder) || !loadRuneDust || runeSingleScope)return originalRuneSingle(self,id,gain,upload,method);
    RuneSingleScope scope{self,gain,loadRuneDust(self,nullptr),RuneFactor()};
    if(ds::RewardRoom(scope.before)==0) { WriteField(gain,0,0);return false; }
    runeSingleScope=&scope;
    const bool success=originalRuneSingle(self,id,gain,upload,method);
    runeSingleScope=nullptr;
    return success;
}
void __fastcall OnRuneStateSave(void* self,bool upload,const void* method) {
    auto* scope=runeSingleScope;int gain=0;
    if(scope && scope->self==self && !scope->applied && loadRuneState && Field(scope->output,0,gain) && gain>0) {
        void* data=loadRuneState(self,nullptr);
        const int scaled=ds::ScaleReward(gain,scope->factor,ds::RewardRoom(scope->before));
        if(data && WriteField(data,0x20,std::max(0,scope->before)+scaled)) {
            WriteField(scope->output,0,scaled);scope->applied=true;
        }
    }
    originalRuneStateSave(self,upload,method);
}
