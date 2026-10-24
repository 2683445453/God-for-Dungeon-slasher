namespace {
alignas(16) unsigned char movementBody[0x960]{};
int movementMaximum=3, movementLateCalls=0, movementDashCalls=0, movementCheckCalls=0, movementEncodes=0;
bool movementNativeReady=false, movementUseCount=false, movementEncodeFails=false;
void* movementSelf=nullptr; const void* movementMethod=nullptr;
ObscuredIntBytes* MovementEncode(ObscuredIntBytes* out,int value,const void* method) {
    assert(!method); ++movementEncodes;
    if(movementEncodeFails)return nullptr;
    memset(out->bytes,0xa7,sizeof(out->bytes));
    const int key=0x71234567, encrypted=value^key;
    memcpy(out->bytes,&key,4); memcpy(out->bytes+4,&encrypted,4); return out;
}
int MovementDecode(ObscuredIntBytes* value,const void* method) {
    assert(!method);
    for(size_t i=8;i<sizeof(value->bytes);++i)assert(value->bytes[i]==0xa7);
    int key=0, encrypted=0; memcpy(&key,value->bytes,4); memcpy(&encrypted,value->bytes+4,4); return encrypted^key;
}
int MovementMaximum(void* self,const void* method) { assert(self==movementBody && !method); return movementMaximum; }
void SetJumpBudget(int value) { ObscuredIntBytes raw{}; assert(MovementEncode(&raw,value,nullptr)==&raw); Put(movementBody,0x4c4,raw); }
int JumpBudget() { ObscuredIntBytes raw{}; assert(Field(movementBody,0x4c4,raw)); return MovementDecode(&raw,nullptr); }
int DashBudget() { int count=0; assert(Field(movementBody,0x630,count)); return count; }
__declspec(noinline) void MovementLateFixture(void* self,const void* method) {
    ++movementLateCalls; movementSelf=self; movementMethod=method;
    // Original frame work must still execute, without being undone by refill.
    int work=0; assert(Field(self,0x930,work)); assert(WriteField(self,0x930,work+1));
}
__declspec(noinline) void MovementDashFixture(void* self,bool useCount,const void* method) {
    ++movementDashCalls; movementSelf=self; movementMethod=method; movementUseCount=useCount;
    if(useCount) { int count=0; assert(Field(self,0x630,count)); assert(WriteField(self,0x630,count-1)); }
}
__declspec(noinline) bool MovementReadyFixture(void* self,const void* method) {
    ++movementCheckCalls; movementSelf=self; movementMethod=method; return movementNativeReady;
}
void ResetMovement(int dash,int jump) {
    memset(movementBody,0x6d,sizeof(movementBody)); SetJumpBudget(jump); Put(movementBody,0x630,dash); Put(movementBody,0x930,0);
    Put(movementBody,0x634,3.75f); Put(movementBody,0x8e8,7.5f);
    Put(movementBody,0x448,false); // Refill must not force the canDash state.
}
void MovementTests(ds::Shared& data,const void* method) {
    data.state=ds::Ready; data.available=ds::All; data.clientHeartbeat=LONG(GetTickCount());
    getMaxDashCount=MovementMaximum; encodeMovementInt=MovementEncode; decodeMovementInt=MovementDecode;
    originalMovementLateUpdate=MovementLateFixture; originalDashAfter=MovementDashFixture; originalDashReady=MovementReadyFixture;
    ResetMovement(0,0); data.enabled=0; unsigned char before[sizeof(movementBody)]; memcpy(before,movementBody,sizeof(before));
    RefillMovement(movementBody); assert(memcmp(before,movementBody,sizeof(before))==0);
    data.enabled=ds::InfiniteDash|ds::InfiniteJump;
    RefillMovement(movementBody); assert(DashBudget()==3 && JumpBudget()==1);
    // Every byte except the two budgets must remain untouched, including
    // encrypted state flags, gravity, velocity, animation timers and cooldown.
    for(size_t i=0;i<sizeof(movementBody);++i)
        if(!(i>=0x4c4 && i<0x4d8) && !(i>=0x630 && i<0x634))assert(movementBody[i]==before[i]);
    const auto dashRefills=data.dashRefills, jumpRefills=data.jumpRefills;
    const int encodes=movementEncodes;
    RefillMovement(movementBody); assert(data.dashRefills==dashRefills && data.jumpRefills==jumpRefills && movementEncodes==encodes);
    ResetMovement(8,INT_MAX); RefillMovement(movementBody); assert(DashBudget()==8 && JumpBudget()==INT_MAX);
    movementMaximum=0; ResetMovement(0,0); data.enabled=ds::InfiniteDash; RefillMovement(movementBody); assert(DashBudget()==0 && JumpBudget()==0);
    movementMaximum=3;
    data.enabled=ds::InfiniteJump; ResetMovement(0,-2); RefillMovement(movementBody); assert(DashBudget()==0 && JumpBudget()==1);
    OnDashAfter(movementBody,true,method); assert(movementUseCount && movementMethod==method && DashBudget()==-1);
    assert(!OnDashReady(movementBody,method) && movementSelf==movementBody && movementMethod==method);
    data.enabled=ds::InfiniteDash; ResetMovement(0,0); RefillMovement(movementBody); assert(DashBudget()==3 && JumpBudget()==0);
    OnDashAfter(movementBody,true,method); assert(!movementUseCount && movementSelf==movementBody && movementMethod==method && DashBudget()==3);
    const int checks=movementCheckCalls; assert(OnDashReady(movementBody,method) && movementCheckCalls==checks);
    data.enabled=ds::InfiniteDash|ds::InfiniteJump; data.clientHeartbeat=LONG(GetTickCount()-4000);
    ResetMovement(0,0); RefillMovement(movementBody); assert(DashBudget()==0 && JumpBudget()==0);
    OnDashAfter(movementBody,true,method); assert(movementUseCount && !OnDashReady(movementBody,method));
    data.clientHeartbeat=LONG(GetTickCount()); data.available=ds::All&~(ds::InfiniteDash|ds::InfiniteJump);
    ResetMovement(0,0); RefillMovement(movementBody); assert(DashBudget()==0 && JumpBudget()==0);
    data.available=ds::All; data.state=ds::Waiting; RefillMovement(movementBody); assert(DashBudget()==0 && JumpBudget()==0);
    data.state=ds::Ready;
    getMaxDashCount=nullptr; encodeMovementInt=nullptr; RefillMovement(movementBody); assert(DashBudget()==0 && JumpBudget()==0);
    getMaxDashCount=MovementMaximum; encodeMovementInt=MovementEncode; decodeMovementInt=nullptr;
    data.enabled=ds::InfiniteJump; RefillMovement(movementBody); assert(JumpBudget()==0);
    decodeMovementInt=MovementDecode; movementEncodeFails=true; RefillMovement(movementBody); assert(JumpBudget()==0); movementEncodeFails=false;
    const auto refills=data.jumpRefills; RefillMovement(nullptr); RefillMovement(reinterpret_cast<void*>(1)); assert(data.jumpRefills==refills);

    // Actual MinHook trampolines for all three new native entry signatures.
    assert(MH_CreateHook(reinterpret_cast<void*>(&MovementLateFixture),reinterpret_cast<void*>(&OnMovementLateUpdate),reinterpret_cast<void**>(&originalMovementLateUpdate))==MH_OK);
    assert(MH_CreateHook(reinterpret_cast<void*>(&MovementDashFixture),reinterpret_cast<void*>(&OnDashAfter),reinterpret_cast<void**>(&originalDashAfter))==MH_OK);
    assert(MH_CreateHook(reinterpret_cast<void*>(&MovementReadyFixture),reinterpret_cast<void*>(&OnDashReady),reinterpret_cast<void**>(&originalDashReady))==MH_OK);
    assert(MH_EnableHook(reinterpret_cast<void*>(&MovementLateFixture))==MH_OK);
    assert(MH_EnableHook(reinterpret_cast<void*>(&MovementDashFixture))==MH_OK);
    assert(MH_EnableHook(reinterpret_cast<void*>(&MovementReadyFixture))==MH_OK);
    GameAction volatile frame=MovementLateFixture; DashAfter volatile dash=MovementDashFixture; MovementCheck volatile ready=MovementReadyFixture;
    data.enabled=ds::InfiniteDash|ds::InfiniteJump; ResetMovement(0,0); const int lateBefore=movementLateCalls, dashBefore=movementDashCalls;
    for(int i=0;i<24;++i) {
        frame(movementBody,method); assert(movementSelf==movementBody && movementMethod==method && DashBudget()==3 && JumpBudget()==1);
        assert(ready(movementBody,method)); dash(movementBody,true,method);
        assert(!movementUseCount && movementSelf==movementBody && movementMethod==method && DashBudget()==3);
        // Model the original native jump's encrypted decrement.
        SetJumpBudget(JumpBudget()-1);
    }
    assert(movementLateCalls==lateBefore+24 && movementDashCalls==dashBefore+24);
    int work=0; assert(Field(movementBody,0x930,work) && work==24);
    ds::Disable(&data); frame(movementBody,method); assert(DashBudget()==3 && JumpBudget()==0);
    assert(!ready(movementBody,method) && movementSelf==movementBody && movementMethod==method);
    dash(movementBody,true,method); assert(movementUseCount && DashBudget()==2);
    dash(movementBody,false,method); assert(!movementUseCount && DashBudget()==2);
    data.enabled=ds::InfiniteDash|ds::InfiniteJump; data.clientHeartbeat=LONG(GetTickCount()-4000);
    ResetMovement(0,0); frame(movementBody,method); assert(DashBudget()==0 && JumpBudget()==0 && !ready(movementBody,method));
    dash(movementBody,true,method); assert(movementUseCount && DashBudget()==-1);
    for(void* target:{reinterpret_cast<void*>(&MovementLateFixture),reinterpret_cast<void*>(&MovementDashFixture),reinterpret_cast<void*>(&MovementReadyFixture)}) {
        assert(MH_DisableHook(target)==MH_OK); assert(MH_RemoveHook(target)==MH_OK);
    }
    data.clientHeartbeat=LONG(GetTickCount()); ds::Disable(&data); ResetMovement(2,0); dash(movementBody,true,method); assert(DashBudget()==1 && movementUseCount);
    getMaxDashCount=nullptr; encodeMovementInt=nullptr; decodeMovementInt=nullptr;
    std::cout<<"PASS: infinite dash/jump independent flags, exhausted-budget refill, valid 20-byte encrypted-int construction, higher budgets preserved, unrelated state/timers untouched, disabled/stale/unavailable pass-through, 24-frame reuse, 3 real x64 movement detours and restore\n";
}
}
