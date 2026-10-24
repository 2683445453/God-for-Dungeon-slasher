namespace {
alignas(8) unsigned char runeFixtureData[64]{};
int runeFixtureGain=15,runeFixtureSave=0,runeFixtureRemoved=0,runeCheckCalls=0;
bool runeFixtureValid=true,runeFixtureUpload=false,runeChangeFactor=false;
void* runeFixtureIds=nullptr;const void* runeFixtureMethod=nullptr;
int __fastcall RuneBalanceFixture(void* self,const void*) { assert(self==&bank);int n=0;Field(runeFixtureData,0x20,n);return n; }
void* __fastcall RuneDataFixture(void* self,const void*) { assert(self==&bank);return runeFixtureData; }
__declspec(noinline) bool __fastcall RuneCheckFixture(void* ids,void* output,const void* method) {
    ++runeCheckCalls;runeFixtureIds=ids;runeFixtureMethod=method;
    RuneCheckResult value{runeFixtureValid,{0,0,0},runeFixtureValid?0:5,3,runeFixtureGain,true,{0,0,0}};
    WriteField(output,0,value);
    if(runeChangeFactor)ds::Set(shared->runePowderMultiplier,1000000);
    return runeFixtureValid;
}
bool RuneClaimFixture(void* ids,const void* method) {
    RuneCheckResult check{};RuneDecomposeCheck volatile query=RuneCheckFixture;
    if(!query(ids,&check,method))return false;
    assert(check.canDecompose);runeFixtureRemoved+=check.targetCount;
    WriteField(runeFixtureData,0x20,RuneBalanceFixture(&bank,nullptr)+check.totalDustGain);
    runeFixtureSave=check.totalDustGain;return true;
}
__declspec(noinline) int __fastcall RuneOverflowFixture(void* self,void* data,const void* method) {
    assert(self==&bank&&data==runeFixtureData);runeFixtureMethod=method;
    int before=0;Field(data,0x20,before);
    // Native adds an int32 and then clamps a wrapped negative balance to zero.
    WriteField(data,0x20,std::max(0,int(uint32_t(before)+uint32_t(runeFixtureGain))));
    ++runeFixtureRemoved;return runeFixtureGain;
}
__declspec(noinline) void __fastcall RuneSaveFixture(void* self,bool upload,const void* method) {
    assert(self==&bank);runeFixtureSave=RuneBalanceFixture(self,nullptr);runeFixtureUpload=upload;runeFixtureMethod=method;
}
__declspec(noinline) bool __fastcall RuneSingleFixture(void* self,void* id,int* gain,bool upload,const void* method) {
    assert(self==&bank);runeFixtureIds=id;
    if(!runeFixtureValid){*gain=0;return false;}
    *gain=runeFixtureGain;++runeFixtureRemoved;
    WriteField(runeFixtureData,0x20,std::max(0,int(uint32_t(RuneBalanceFixture(self,nullptr))+uint32_t(*gain))));
    RuneStateSave volatile save=RuneSaveFixture;save(self,upload,method);return true;
}
uintptr_t gemFixtureCaller=0;
__declspec(noinline) void __fastcall RecordGemFixture(void*,int,bool,const void*) { gemFixtureCaller=reinterpret_cast<uintptr_t>(_ReturnAddress()); }
CurrencySave gemFixtureInvoke=RecordGemFixture;
__declspec(noinline) void InvokeRunGemFixture(int amount,bool option,const void* method) {
    CurrencySave volatile invoke=gemFixtureInvoke;invoke(&bank,amount,option,method);
    InterlockedIncrement(&fixtureInvocationCount);
}
void CurrencyMultiplierTests(ds::Shared& data,void* self,const void* method) {
    const uintptr_t previousBase=gameBase;
    initializeMetadata=MetadataStub;getDataController=ControllerStub;dataControllerMethodUsage=&singletonUsage;
    loadCurrency={BalanceStub<Currency::Prayer>,BalanceStub<Currency::Soul>,BalanceStub<Currency::Breach>,BalanceStub<Currency::Gem>};
    loadRuneDust=RuneBalanceFixture;loadRuneState=RuneDataFixture;
    struct Hook {void* target;void* replacement;void** original;};
    Hook hooks[]={
        {reinterpret_cast<void*>(&RuneCheckFixture),reinterpret_cast<void*>(&OnRuneCheck),reinterpret_cast<void**>(&originalRuneCheck)},
        {reinterpret_cast<void*>(&RuneOverflowFixture),reinterpret_cast<void*>(&OnRuneOverflow),reinterpret_cast<void**>(&originalRuneOverflow)},
        {reinterpret_cast<void*>(&RuneSingleFixture),reinterpret_cast<void*>(&OnRuneSingle),reinterpret_cast<void**>(&originalRuneSingle)},
        {reinterpret_cast<void*>(&RuneSaveFixture),reinterpret_cast<void*>(&OnRuneStateSave),reinterpret_cast<void**>(&originalRuneStateSave)},
        {reinterpret_cast<void*>(&SaveFixture<Currency::Gem>),reinterpret_cast<void*>(&OnSaveGem),reinterpret_cast<void**>(&originalCurrencySave[3])}
    };
    InvokeRunGemFixture(0,false,method);assert(gemFixtureCaller);
    gameBase=gemFixtureCaller-ds::RunGemSaveCallRva-5;gemFixtureInvoke=SaveFixture<Currency::Gem>;
    for(const auto& hook:hooks){assert(MH_CreateHook(hook.target,hook.replacement,hook.original)==MH_OK);assert(MH_EnableHook(hook.target)==MH_OK);}
    RuneDecomposeCheck volatile query=RuneCheckFixture;RuneOverflow volatile overflow=RuneOverflowFixture;RuneSingle volatile single=RuneSingleFixture;
    data.clientHeartbeat=LONG(GetTickCount());data.enabled=ds::RunePowder;data.runePowderMultiplier=2500;
    WriteField(runeFixtureData,0x20,10);runeFixtureGain=15;runeFixtureValid=true;RuneCheckResult result{};
    assert(query(self,&result,method)&&result.totalDustGain==38&&result.targetCount==3&&result.containsEquipped&&result.reason==0);
    assert(runeFixtureIds==self&&runeFixtureMethod==method);
    for(int i=0;i<3;++i)assert(query(self,&result,method)&&result.totalDustGain==38&&RuneBalanceFixture(&bank,nullptr)==10);
    assert(RuneClaimFixture(self,method)&&runeFixtureSave==38&&RuneBalanceFixture(&bank,nullptr)==48&&runeFixtureRemoved==3);
    data.runePowderMultiplier=100;assert(query(self,&result,method)&&result.totalDustGain==2);
    data.runePowderMultiplier=1000000;assert(query(self,&result,method)&&result.totalDustGain==15000);
    WriteField(runeFixtureData,0x20,INT_MAX-9);assert(query(self,&result,method)&&result.totalDustGain==9);
    assert(RuneClaimFixture(self,method)&&RuneBalanceFixture(&bank,nullptr)==INT_MAX);
    int removed=runeFixtureRemoved;
    assert(!query(self,&result,method)&&!result.canDecompose&&result.reason==8&&result.totalDustGain==0);
    assert(!RuneClaimFixture(self,method)&&runeFixtureRemoved==removed);
    runeFixtureValid=false;assert(!query(self,&result,method)&&result.reason==5&&result.totalDustGain==15);runeFixtureValid=true;
    WriteField(runeFixtureData,0x20,0);data.runePowderMultiplier=2500;runeChangeFactor=true;
    assert(query(self,&result,method)&&result.totalDustGain==38&&data.runePowderMultiplier==1000000);runeChangeFactor=false;
    data.runePowderMultiplier=2500;data.enabled=0;assert(query(self,&result,method)&&result.totalDustGain==15);
    data.enabled=ds::RunePowder;data.clientHeartbeat=LONG(GetTickCount()-4000);assert(query(self,&result,method)&&result.totalDustGain==15);
    data.clientHeartbeat=LONG(GetTickCount());
    WriteField(runeFixtureData,0x20,10);assert(overflow(&bank,runeFixtureData,method)==38&&RuneBalanceFixture(&bank,nullptr)==48);
    WriteField(runeFixtureData,0x20,INT_MAX-4);assert(overflow(&bank,runeFixtureData,method)==4&&RuneBalanceFixture(&bank,nullptr)==INT_MAX);
    WriteField(runeFixtureData,0x20,10);int gain=0;assert(single(&bank,self,&gain,false,method)&&gain==38&&runeFixtureSave==48&&!runeFixtureUpload&&runeFixtureMethod==method);
    WriteField(runeFixtureData,0x20,INT_MAX-3);assert(single(&bank,self,&gain,true,method)&&gain==3&&runeFixtureSave==INT_MAX&&runeFixtureUpload);
    removed=runeFixtureRemoved;assert(!single(&bank,self,&gain,true,method)&&gain==0&&runeFixtureRemoved==removed);
    // A normal rune-data save, including synthesis costs, passes through.
    WriteField(runeFixtureData,0x20,12);RuneStateSave volatile save=RuneSaveFixture;save(&bank,false,method);assert(runeFixtureSave==12&&!runeSingleScope);
    data.enabled=ds::Diamond;data.diamondMultiplier=3000;bank.fill(0);
    CurrencySave volatile gem=SaveFixture<Currency::Gem>;gem(&bank,7,false,method);assert(savedAmount==21&&bank[3]==21&&!savedOption&&methodSeen==method);
    gem(&bank,-4,true,method);assert(savedAmount==-4&&bank[3]==17&&savedOption);
    gem(&bank,0,false,method);assert(savedAmount==0&&bank[3]==17);
    data.diamondMultiplier=100;gem(&bank,15,false,method);assert(savedAmount==2&&bank[3]==19);
    data.diamondMultiplier=1000000;bank[3]=INT_MAX-4;gem(&bank,7,false,method);assert(savedAmount==4&&bank[3]==INT_MAX);
    gem(&bank,7,false,method);assert(savedAmount==0&&bank[3]==INT_MAX);
    data.enabled=0;bank[3]=0;gem(&bank,7,false,method);assert(savedAmount==7);
    data.enabled=ds::Diamond;data.clientHeartbeat=LONG(GetTickCount()-4000);gem(&bank,7,false,method);assert(savedAmount==7);data.clientHeartbeat=LONG(GetTickCount());
    // Native main-run caller consumes a scaled buffer; quest callers multiply
    // once. Independent diamond x3 takes precedence over settlement x10.
    data.enabled=ds::Diamond|ds::Reward;data.diamondMultiplier=3000;data.rewardMultiplier=10000;bank.fill(0);ResetBuffers();
    OnSetReward(self,method);ExpectBuffers();assert(displayedFinal.soul==100&&displayedFinal.gem==15);
    int previews=previewCalls;OnSinUpdate(self,method);assert(previewCalls==previews);
    data.diamondMultiplier=4000;OnSinUpdate(self,method);assert(previewCalls==previews+1&&displayedFinal.gem==20);ExpectBuffers();
    data.diamondMultiplier=3000;
    {SettlementScope scope(10000);RewardBuffers shown{};Field(playerBuffer,0x218,shown);assert(shown.gem==15&&scope.gemFactor==3000);
        data.diamondMultiplier=5000;data.enabled=0;
        InvokeRunGemFixture(shown.gem,true,method);assert(savedAmount==15&&bank[3]==15);
        gem(&bank,7,false,method);assert(savedAmount==21&&bank[3]==36);
        gem(&bank,-4,true,method);assert(savedAmount==-4&&bank[3]==32);
    }ExpectBuffers();
    data.enabled=ds::Reward;data.rewardMultiplier=10000;OnSinUpdate(self,method);assert(displayedFinal.gem==50);ExpectBuffers();
    data.enabled=ds::Diamond;data.diamondMultiplier=1000;OnSetReward(self,method);assert(displayedFinal.gem==5&&displayedFinal.soul==10);
    data.enabled=ds::Reward|ds::Diamond;OnSetReward(self,method);assert(displayedFinal.gem==5&&displayedFinal.soul==100);
    for(const auto& hook:hooks){assert(MH_DisableHook(hook.target)==MH_OK);assert(MH_RemoveHook(hook.target)==MH_OK);}
    gameBase=previousBase;initializeMetadata=nullptr;getDataController=nullptr;dataControllerMethodUsage=nullptr;loadCurrency.fill(nullptr);loadRuneDust=nullptr;loadRuneState=nullptr;
    rewardScreen=nullptr;data.enabled=ds::Reward;data.rewardMultiplier=2500;ResetBuffers();
    std::cout<<"PASS: powder manual preview/claim, single/overflow decomposition, positive diamond receipts with spend passthrough, independent settlement display/factor snapshots, no double scaling of main-run gems, quest credits, int32 headroom, toggles/stale controller and 5 real x64 detours; isolated fixtures only\n";
}
}
