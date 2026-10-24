namespace syncCooldownFixture {
int prompts=0,timeouts=0,invocations=0,refills=0;
bool invokeFails=false,reenter=false;
void* seenOwner=nullptr;void* seenCallback=nullptr;int seenCode=0;const void* seenMethod=nullptr;
__declspec(noinline) void ConnectFixture(void* owner,void* callback,int code,const void* method) {
    ++prompts;seenOwner=owner;seenCallback=callback;seenCode=code;seenMethod=method;
}
__declspec(noinline) void TimeoutFixture(void* owner,void* callback,const void* method) {
    ++timeouts;seenOwner=owner;seenCallback=callback;seenMethod=method;
}
void* Invoke(const void* method,void* object,void** args,void** exception) {
    if(*static_cast<const std::string*>(method)!="Invoke")return ArtiInvoke(method,object,args,exception);
    assert(!args);++invocations;*exception=invokeFails?object:nullptr;
    if(reenter) {
        const LONG saved=ds::Read(shared->clientHeartbeat);ds::Set(shared->clientHeartbeat,LONG(GetTickCount()));
        OnTimeOut(seenOwner,object,nullptr);ds::Set(shared->clientHeartbeat,saved);
    }
    return nullptr;
}
float cooldown=12.5f;
void* cooldownOther=nullptr;void* cooldownData=nullptr;int cooldownIndex=0;
__declspec(noinline) float SkillFixture(void* owner,int index,void* other,void* data,const void* method) {
    seenOwner=owner;cooldownIndex=index;cooldownOther=other;cooldownData=data;seenMethod=method;return cooldown;
}
__declspec(noinline) float WeaponFixture(void* owner,const void* method) {seenOwner=owner;seenMethod=method;return cooldown;}
ObscuredFloatBytes* Encode(ObscuredFloatBytes* out,float value,const void* method) {
    assert(!method);memset(out->bytes,0xa3,24);memcpy(out->bytes,&value,4);return out;
}
void Refill(void* self,const void* method) {
    assert(!method);++refills;Put(self,0x628,4);Put(self,0x620,OnWeaponCooldown(self,nullptr));
}
void Tests(ds::Shared& state) {
    const auto api=artifactApi;
    artifactApi.objectClass=ArtiClass;artifactApi.findMethod=ArtiFindMethod;artifactApi.invoke=Invoke;
    artifactApi.rootNew=ArtiRoot;artifactApi.rootFree=ArtiFree;artifactApi.rootTarget=ArtiTarget;
    artifactApi.newObject=ArtiNew;
    state.state=ds::Ready;state.available=ds::All;state.clientHeartbeat=LONG(GetTickCount());state.enabled=ds::AutoRetry;
    void* owner=ArtiObject(0xd1);void* callback=ArtiObject(0xd2);void* clone=ArtiObject(0xd2);
    Put(callback,0x10,uintptr_t(0x1234));Put(callback,0x20,owner);
    Put(clone,0x10,uintptr_t(0x1234));Put(clone,0x20,owner);
    assert(MH_CreateHook(reinterpret_cast<void*>(&ConnectFixture),reinterpret_cast<void*>(&OnConnectFail),reinterpret_cast<void**>(&originalConnectFail))==MH_OK);
    assert(MH_CreateHook(reinterpret_cast<void*>(&TimeoutFixture),reinterpret_cast<void*>(&OnTimeOut),reinterpret_cast<void**>(&originalTimeOut))==MH_OK);
    assert(MH_EnableHook(reinterpret_cast<void*>(&ConnectFixture))==MH_OK && MH_EnableHook(reinterpret_cast<void*>(&TimeoutFixture))==MH_OK);
    ConnectFail volatile connect=ConnectFixture;TimeOut volatile timeout=TimeoutFixture;
    const DWORD now=GetTickCount();int method=91;
    connect(owner,callback,2,&method);assert(prompts==0 && state.syncPending==1 && artiRoots.size()==2);
    DWORD due=syncRetries[0].due;
    connect(owner,clone,2,&method);assert(syncRetries.size()==1 && syncRetries[0].due==due);
    ProcessSyncRetries(now);assert(invocations==0);
    state.clientHeartbeat=LONG(due);ProcessSyncRetries(due);assert(invocations==1 && state.syncPending==0 && state.syncRetries==1);
    // Async callbacks recreated by the game retain backoff via method+target.
    assert(QueueSyncRetry(owner,clone,2,false,due+20));assert(state.syncPending==1 && syncRetries[0].attempt==1 && syncRetries[0].due==due+2020);
    for(unsigned attempt=2;attempt<=8;++attempt) {
        due=syncRetries[0].due;state.clientHeartbeat=LONG(due);ProcessSyncRetries(due);
        assert(QueueSyncRetry(owner,clone,2,false,due));
        assert(syncRetries[0].due-due==SyncRetryDelay(std::min(attempt,5u)));
    }
    ds::SetFeature(&state,ds::AutoRetry,false);ProcessSyncRetries(due);
    assert(syncRetries.empty() && state.syncPending==0 && prompts==1 && seenCallback==callback && seenCode==2 && artiRoots.empty());
    state.clientHeartbeat=LONG(GetTickCount());state.enabled=ds::AutoRetry;
    for(int code:{1000,1002,1010,1003,0})connect(owner,callback,code,&method);
    assert(prompts==6 && seenMethod==&method); // Account/validation errors retain native handling.
    timeout(owner,callback,&method);assert(timeouts==0 && state.syncPending==1);
    due=syncRetries[0].due;invokeFails=true;state.clientHeartbeat=LONG(due);ProcessSyncRetries(due);
    assert(state.syncPending==1 && !syncRetries[0].inFlight && syncRetries[0].attempt==1);
    invokeFails=false;due=syncRetries[0].due;state.clientHeartbeat=LONG(due);reenter=true;seenOwner=owner;ProcessSyncRetries(due);reenter=false;
    assert(state.syncPending==1 && syncRetries.size()==1 && syncRetries[0].attempt==2 && timeouts==0);
    // Stop on heartbeat loss and restore a manual retry without sending again.
    state.clientHeartbeat=LONG(due-4000);int calls=invocations;ProcessSyncRetries(due);
    assert(syncRetries.empty() && invocations==calls && timeouts==1 && artiRoots.empty());
    // Tick rollover and completed request identity cleanup.
    state.clientHeartbeat=LONG(0xffffff00);assert(QueueSyncRetry(owner,callback,2,false,0xffffff00));
    due=syncRetries[0].due;state.clientHeartbeat=LONG(0xffffff00);ProcessSyncRetries(0xffffff10);assert(state.syncPending==1);
    state.clientHeartbeat=LONG(due);ProcessSyncRetries(due);assert(state.syncPending==0);
    state.clientHeartbeat=LONG(due+120000);ProcessSyncRetries(due+120000);assert(syncRetries.empty() && artiRoots.empty());
    assert(MH_CreateHook(reinterpret_cast<void*>(&SkillFixture),reinterpret_cast<void*>(&OnSkillCooldown),reinterpret_cast<void**>(&originalSkillCooldown))==MH_OK);
    assert(MH_CreateHook(reinterpret_cast<void*>(&WeaponFixture),reinterpret_cast<void*>(&OnWeaponCooldown),reinterpret_cast<void**>(&originalWeaponCooldown))==MH_OK);
    assert(MH_EnableHook(reinterpret_cast<void*>(&SkillFixture))==MH_OK && MH_EnableHook(reinterpret_cast<void*>(&WeaponFixture))==MH_OK);
    SkillCooldown volatile skillQuery=SkillFixture;FloatQuery volatile weaponQuery=WeaponFixture;
    state.clientHeartbeat=LONG(GetTickCount());state.enabled=0;
    assert(skillQuery(owner,27,clone,callback,&method)==12.5f && weaponQuery(owner,&method)==12.5f);
    state.enabled=ds::NoCooldown;
    assert(skillQuery(owner,27,clone,callback,&method)==0 && cooldownOther==clone && cooldownData==callback && cooldownIndex==27 && seenMethod==&method);
    assert(weaponQuery(owner,&method)==0 && seenOwner==owner && seenMethod==&method);
    cooldown=-1;assert(weaponQuery(owner,&method)==-1);cooldown=12.5f;
    // Existing encrypted equipped cooldowns clear; backpack and delay/duration remain intact.
    encodeCooldownFloat=Encode;skillCurrentSlots=12;skillCurrentBuffSlots=6;void* list=ArtiList();Put(owner,0x40,list);
    std::vector<void*> skills;
    for(int i=0;i<20;++i) {
        void* data=ArtiObject(0xd3);void* info=ArtiObject(0xd4);Put(data,0x14,i);Put(data,0x20,info);Put(data,0x38,true);
        memset(static_cast<unsigned char*>(info)+0x70,0x5a,24);Put(info,0x58,7.5f);ArtiAdd(list,data);skills.push_back(info);
    }
    ClearSkillCooldowns(owner);
    for(size_t i=0;i<skills.size();++i) {
        ObscuredFloatBytes value{};assert(Field(skills[i],0x70,value));
        if(i<18){float v=1;memcpy(&v,value.bytes,4);assert(v==0);for(int j=4;j<24;++j)assert(value.bytes[j]==0xa3);}
        else for(auto b:value.bytes)assert(b==0x5a);
        float duration=0;assert(Field(skills[i],0x58,duration) && duration==7.5f);
    }
    refillWeaponCooldown=Refill;Put(owner,0x620,5.0f);Put(owner,0x628,0);RefillMovement(owner);
    assert(refills==1 && ArtiInt(owner,0x628)==4);float timer=1;assert(Field(owner,0x620,timer)&&timer==0);
    state.enabled=0;Put(owner,0x620,5.0f);RefillMovement(owner);assert(refills==1);
    state.enabled=ds::NoCooldown;state.clientHeartbeat=LONG(GetTickCount()-4000);
    assert(weaponQuery(owner,&method)==12.5f);RefillMovement(owner);assert(refills==1);
    for(void* target:{reinterpret_cast<void*>(&ConnectFixture),reinterpret_cast<void*>(&TimeoutFixture),reinterpret_cast<void*>(&SkillFixture),reinterpret_cast<void*>(&WeaponFixture)}) {
        assert(MH_DisableHook(target)==MH_OK && MH_RemoveHook(target)==MH_OK);
    }
    originalSkillCooldown=nullptr;originalWeaponCooldown=nullptr;originalConnectFail=nullptr;originalTimeOut=nullptr;
    refillWeaponCooldown=nullptr;encodeCooldownFloat=nullptr;skillCurrentSlots=6;skillCurrentBuffSlots=2;
    artifactApi=api;artiMemory.clear();artiMethods.clear();state.enabled=0;state.clientHeartbeat=LONG(GetTickCount());
    std::cout<<"PASS: 4 native cooldown/retry detours, forwarding/off/heartbeat, encrypted equipped cooldowns and weapon charge recovery, no backpack/duration changes; quiet retries, delegate dedupe, async/sync backoff, managed exception, rollover, native account-error handling, pending manual recovery, GC roots released\n";
}
}
