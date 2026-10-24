namespace unlockFixture {
using Bytes=std::array<unsigned char,0xe0>;
Bytes manager{},controller{},file{},skillTable{},artifactTable{},skillArray{},artifactArray{},skillList{},artifactList{},skillIds{},artifactIds{},boxed{};
std::array<Bytes,6> skillRows{},artifactRows{};
int skillClass,artifactClass,managerClass,imageToken,domainToken;
const void* assemblyList[]={&imageToken};
std::array<int,10> skillStates{},artifactStates{};
bool managerReady=true,failSave=false,failSync=false;
int missingCatalog=0,missingLoader=0,missingField=0;
bool tableLoadException=false;
int saves=0,syncs=0,updates=0,adds=0;DWORD expectedThread=0;
void* __cdecl ObjectClass(void* object){return object;}
const void* __cdecl Method(void* klass,const char* name,int count) {
    if(!strcmp(name,"Load") && ((missingLoader==1 && klass==&skillClass) || (missingLoader==2 && klass==&artifactClass)))return nullptr;
    const std::pair<const char*,int> allowed[]={{"Load",1},{"get_Instance",0},{"LoadSkillUnlock",1},{"LoadArtifactUnlock",1},
        {"SaveSkillUnlockClear",1},{"SaveArtifactUnlockClear",1},{"Add",1},{"get_ES3File",0},{"Sync",0}};
    for(const auto& entry:allowed)if(!strcmp(entry.first,name) && entry.second==count)return name;
    assert(false && "Unexpected game method (must not grant currency or purchase)");return nullptr;
}
void* __cdecl Invoke(const void* method,void* object,void** args,void** exception) {
    assert(GetCurrentThreadId()==expectedThread);*exception=nullptr;
    const char* name=static_cast<const char*>(method);
    if(!strcmp(name,"get_Instance"))return managerReady?manager.data():nullptr;
    if(!strcmp(name,"Load")){assert(!*static_cast<bool*>(args[0]));if(tableLoadException)*exception=&skillClass;return nullptr;}
    if(!strcmp(name,"get_ES3File")){assert(object==controller.data());return file.data();}
    if(!strcmp(name,"Sync")){assert(object==file.data());++syncs;if(failSync)*exception=&file;return nullptr;}
    const int id=*static_cast<int*>(args[0]);assert(id>0 && id<10);
    if(!strcmp(name,"Add")) {
        assert(object==skillList.data() || object==artifactList.data());void* array=nullptr;int count=0;
        assert(Field(object,0x10,array) && Field(object,0x18,count));Put(static_cast<unsigned char*>(array),0x20+count*4,id);
        WriteField(object,0x18,count+1);++adds;return nullptr;
    }
    assert(object==controller.data());
    if(!strcmp(name,"LoadSkillUnlock") || !strcmp(name,"LoadArtifactUnlock")) {
        Put(boxed.data(),0x10,!strcmp(name,"LoadSkillUnlock")?skillStates[id]:artifactStates[id]);return boxed.data();
    }
    if(failSave && id==3){*exception=&controller;return nullptr;}
    assert(!strcmp(name,"SaveSkillUnlockClear") || !strcmp(name,"SaveArtifactUnlockClear"));
    (!strcmp(name,"SaveSkillUnlockClear")?skillStates:artifactStates)[id]=2;++saves;return nullptr;
}
void* __cdecl Domain(){return &domainToken;}
const void** __cdecl Assemblies(void*,size_t* count){*count=1;return assemblyList;}
const void* __cdecl Image(const void*){return &imageToken;}
void* __cdecl Class(const void*,const char* space,const char* name) {
    // Match the actual game metadata independently of the runtime profile.
    // An empty namespace for either table must fail, as it does in IL2CPP.
    if(!strcmp(space,"SkillTable") && !strcmp(name,"SkillData") && missingCatalog!=1)return &skillClass;
    if(!strcmp(space,"ArtifactTable") && !strcmp(name,"ArtifactData") && missingCatalog!=2)return &artifactClass;
    if(!*space && !strcmp(name,"UnlockManager"))return &managerClass;
    return nullptr;
}
void* __cdecl FieldInfo(void* klass,const char* name) {
    if(klass==&skillClass && !strcmp(name,"SkillDataList") && missingField!=1)return &skillClass;
    if(klass==&artifactClass && !strcmp(name,"ArtifactDataList") && missingField!=2)return &artifactClass;
    return nullptr;
}
void __cdecl StaticValue(void* field,void* result){*static_cast<void**>(result)=field==&skillClass?skillTable.data():artifactTable.data();}
uintptr_t __cdecl Root(void* object,bool){return reinterpret_cast<uintptr_t>(object);}
void __cdecl Free(uintptr_t){}
template<size_t N>void RefList(Bytes& list,Bytes& array,std::array<Bytes,N>& rows) {
    Put(list.data(),0x10,static_cast<void*>(array.data()));Put(list.data(),0x18,int(N));Put(array.data(),0x18,uintptr_t(N));
    for(size_t i=0;i<N;++i)Put(array.data(),0x20+i*sizeof(void*),static_cast<void*>(rows[i].data()));
}
void IntList(Bytes& list,Bytes& array) {
    Put(list.data(),0x10,static_cast<void*>(array.data()));Put(list.data(),0x18,1);Put(array.data(),0x18,uintptr_t(10));Put(array.data(),0x20,1);
}
__declspec(noinline) void __fastcall Update(void* object,const void*){assert(object==controller.data());InterlockedIncrement(reinterpret_cast<volatile LONG*>(&updates));}
__declspec(noinline) void RunUpdate(){GameAction volatile fn=Update;fn(controller.data(),nullptr);InterlockedIncrement(&fixtureInvocationCount);}
void Tests(ds::Shared& state) {
    const auto previousApi=artifactApi;auto previousUpdate=originalDataUpdate;expectedThread=GetCurrentThreadId();
    artifactApi={};artifactApi.objectClass=ObjectClass;artifactApi.findMethod=Method;artifactApi.invoke=Invoke;
    artifactApi.domain=Domain;artifactApi.assemblies=Assemblies;artifactApi.image=Image;artifactApi.findClass=Class;
    artifactApi.findField=FieldInfo;artifactApi.staticFieldValue=StaticValue;artifactApi.rootNew=Root;artifactApi.rootFree=Free;
    assert(!artifactApi.Class("","SkillData") && !artifactApi.Class("","ArtifactData"));
    assert(artifactApi.Class("SkillTable","SkillData")==&skillClass && artifactApi.Class("ArtifactTable","ArtifactData")==&artifactClass);
    void* wrongTable=nullptr;const ds::UnlockCatalogProfile incorrect{"","SkillData","SkillDataList"};
    assert(!LoadUnlockTable(incorrect,true,&wrongTable) && state.unlockError==ds::UnlockSkillType);
    RefList(skillTable,skillArray,skillRows);RefList(artifactTable,artifactArray,artifactRows);
    for(int i=0;i<6;++i){Put(skillRows[i].data(),0x10,(i+1)*10);Put(artifactRows[i].data(),0x10,i+1);}
    Put(skillRows[3].data(),0x10,31);Put(skillRows[4].data(),0x14,true);Put(skillRows[5].data(),0x10,30);
    Put(artifactRows[3].data(),0xc0,true);Put(artifactRows[4].data(),0x10,0);Put(artifactRows[5].data(),0x10,3);
    IntList(skillList,skillIds);IntList(artifactList,artifactIds);Put(manager.data(),0x28,static_cast<void*>(skillList.data()));Put(manager.data(),0x30,static_cast<void*>(artifactList.data()));
    skillStates.fill(3);artifactStates.fill(0);skillStates[1]=artifactStates[1]=2;
    state.state=ds::Ready;state.available=ds::All;state.enabled=0;state.clientHeartbeat=LONG(GetTickCount());state.unlockStatus=0;
    assert(MH_CreateHook(reinterpret_cast<void*>(&Update),reinterpret_cast<void*>(&OnDataUpdate),reinterpret_cast<void**>(&originalDataUpdate))==MH_OK);
    assert(MH_EnableHook(reinterpret_cast<void*>(&Update))==MH_OK);
    assert(ds::RequestUnlockAll(&state));assert(!ds::RequestUnlockAll(&state));managerReady=false;RunUpdate();
    assert(state.unlockStatus==1 && state.unlockCommand==1 && saves==0 && syncs==0 && updates==1);
    managerReady=true;RunUpdate();assert(state.unlockStatus==3 && state.unlockCommand==0 && state.unlockedSkills==3 && state.unlockedArtifacts==3);
    assert(saves==4 && syncs==1 && adds==4 && skillStates[5]==3 && artifactStates[4]==0);
    RunUpdate();assert(saves==4 && syncs==1);
    assert(ds::RequestUnlockAll(&state));RunUpdate();assert(saves==4 && syncs==1 && adds==4 && state.unlockStatus==3);
    for(int kind=1;kind<=2;++kind) {
        missingCatalog=kind;assert(ds::RequestUnlockAll(&state));RunUpdate();
        assert(state.unlockStatus==-1 && state.unlockedSkills==0 && state.unlockedArtifacts==0 &&
            state.unlockError==(kind==1?ds::UnlockSkillType:ds::UnlockArtifactType));missingCatalog=0;
        missingLoader=kind;assert(ds::RequestUnlockAll(&state));RunUpdate();
        assert(state.unlockStatus==-1 && state.unlockError==(kind==1?ds::UnlockSkillLoader:ds::UnlockArtifactLoader));missingLoader=0;
        missingField=kind;assert(ds::RequestUnlockAll(&state));RunUpdate();
        assert(state.unlockStatus==-1 && state.unlockError==(kind==1?ds::UnlockSkillField:ds::UnlockArtifactField));missingField=0;
    }
    tableLoadException=true;assert(ds::RequestUnlockAll(&state));RunUpdate();assert(state.unlockError==ds::UnlockSkillLoad);tableLoadException=false;
    Put(skillTable.data(),0x18,0);assert(ds::RequestUnlockAll(&state));RunUpdate();assert(state.unlockError==ds::UnlockSkillCatalog);Put(skillTable.data(),0x18,6);
    assert(saves==4 && syncs==1 && adds==4);
    assert(ds::RequestUnlockAll(&state));RunUpdate();assert(state.unlockStatus==3 && state.unlockError==ds::UnlockNone);
    // Failed item leaves a retryable command, with prior changes flushed once.
    skillStates[2]=skillStates[3]=0;failSave=true;assert(ds::RequestUnlockAll(&state));RunUpdate();
    assert(state.unlockStatus==-1 && state.unlockError==ds::UnlockSkillSave && state.unlockedSkills==2 && state.unlockedArtifacts==0 && syncs==2);
    failSave=false;assert(ds::RequestUnlockAll(&state));RunUpdate();assert(state.unlockStatus==3 && skillStates[3]==2 && adds==4 && syncs==3);
    skillStates[2]=0;failSync=true;assert(ds::RequestUnlockAll(&state));RunUpdate();assert(state.unlockStatus==-1 && state.unlockError==ds::UnlockFileSync);
    // A retry must flush a previously failed file write even when all statuses
    // are now clear; test this separately from idempotent, completed requests.
    failSync=false;
    const int beforeRetry=syncs;assert(ds::RequestUnlockAll(&state));RunUpdate();assert(state.unlockStatus==3 && syncs==beforeRetry+1 && !unlockSavePending);
    assert(ds::RequestUnlockAll(&state));ds::Disable(&state);RunUpdate();assert(state.unlockCommand==0 && state.unlockStatus==0);
    assert(ds::RequestUnlockAll(&state));state.clientHeartbeat=LONG(GetTickCount()-4000);RunUpdate();assert(state.unlockStatus==0);
    state.clientHeartbeat=LONG(GetTickCount());assert(ds::RequestUnlockAll(&state));state.unlockRequestedAt=LONG(GetTickCount()-16000);RunUpdate();assert(state.unlockStatus==-1);
    assert(MH_DisableHook(reinterpret_cast<void*>(&Update))==MH_OK && MH_RemoveHook(reinterpret_cast<void*>(&Update))==MH_OK);
    artifactApi=previousApi;originalDataUpdate=previousUpdate;state.unlockStatus=0;state.enabled=0;
    std::cout<<"PASS: exact SkillTable/ArtifactTable namespaces (empty rejected), native unlock command detour, distinct missing-type/method/field/load/catalog/save diagnostics, clear statuses, ID filtering/deduplication, original lists, local flush/failure retry, timeout, cancellation and heartbeat\n";
}
}
