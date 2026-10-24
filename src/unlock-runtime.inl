GameAction originalDataUpdate=nullptr;
bool unlockSavePending=false;

bool UnlockFailed(ds::UnlockFailure error) {if(shared)ds::Set(shared->unlockError,error);return false;}
bool LoadUnlockTable(const ds::UnlockCatalogProfile& profile,bool skills,void** list) {
    void* klass=artifactApi.Class(profile.space,profile.type);
    if(!klass)return UnlockFailed(skills?ds::UnlockSkillType:ds::UnlockArtifactType);
    if(!artifactApi.findMethod || !artifactApi.invoke)return UnlockFailed(skills?ds::UnlockSkillLoader:ds::UnlockArtifactLoader);
    const void* method=artifactApi.findMethod(klass,"Load",1);
    if(!method)return UnlockFailed(skills?ds::UnlockSkillLoader:ds::UnlockArtifactLoader);
    bool force=false;void* args[]={&force};void* exception=nullptr;
    artifactApi.invoke(method,nullptr,args,&exception);
    if(exception)return UnlockFailed(skills?ds::UnlockSkillLoad:ds::UnlockArtifactLoad);
    if(!artifactApi.StaticField(klass,profile.list,list))return UnlockFailed(skills?ds::UnlockSkillField:ds::UnlockArtifactField);
    return true;
}
bool UnlockListShape(void* list,void*& array,int& count,size_t width) {
    uintptr_t length=0;
    return Field(list,0x10,array) && Field(list,0x18,count) && count>=0 && count<=8192 &&
        Field(array,0x18,length) && uintptr_t(count)<=length && length<=8192 && width<=sizeof(void*);
}
bool CatalogUnlockIds(void* list,bool skills,std::vector<int>& ids) {
    void* array=nullptr;int count=0;
    if(!UnlockListShape(list,array,count,sizeof(void*)) || !count)return false;
    for(int i=0;i<count;++i) {
        void* data=nullptr;int id=0;bool excluded=true;
        if(!Field(array,0x20+sizeof(void*)*i,data) || !Field(data,0x10,id) ||
           !Field(data,skills?0x14:0xc0,excluded))return false;
        if(excluded || id<=0 || id>1000000)continue;
        // Table skill IDs are baseID * 10 + modID; unlock keys use baseID.
        // Only the base row represents an independently unlockable skill.
        if(skills) {if(id%10)continue;id/=10;}
        ids.push_back(id);
    }
    std::sort(ids.begin(),ids.end());ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
    return !ids.empty();
}
bool HasUnlockId(void* list,int id,bool& found) {
    void* array=nullptr;int count=0;found=false;
    if(!UnlockListShape(list,array,count,sizeof(int)))return false;
    for(int i=0;i<count;++i) {int value=0;if(!Field(array,0x20+sizeof(int)*i,value))return false;if(value==id)found=true;}
    return true;
}
bool CompleteUnlockIds(void* controller,void* list,const std::vector<int>& ids,bool skills,volatile LONG& completed,bool& changed) {
    const char* load=skills?"LoadSkillUnlock":"LoadArtifactUnlock";
    const char* save=skills?"SaveSkillUnlockClear":"SaveArtifactUnlockClear";
    for(int id:ids) {
        void* args[]={&id};void* result=nullptr;int status=-1;bool found=false;
        if(!HasUnlockId(list,id,found))return UnlockFailed(skills?ds::UnlockSkillList:ds::UnlockArtifactList);
        if(!artifactApi.Call(controller,load,1,args,&result) || !Field(result,0x10,status) || status<0 || status>3)
            return UnlockFailed(skills?ds::UnlockSkillQuery:ds::UnlockArtifactQuery);
        if(status!=2) {
            if(!artifactApi.Call(controller,save,1,args))return UnlockFailed(skills?ds::UnlockSkillSave:ds::UnlockArtifactSave);
            changed=true;
        }
        if(!found) {if(!artifactApi.Call(list,"Add",1,args))return UnlockFailed(skills?ds::UnlockSkillAppend:ds::UnlockArtifactAppend);changed=true;}
        InterlockedIncrement(&completed);
    }
    return true;
}
void ProcessUnlockAll(void* controller) {
    if(!shared || !ds::Read(shared->unlockCommand))return;
    if(ds::Read(shared->state)!=ds::Ready || !ds::Fresh(GetTickCount(),DWORD(ds::Read(shared->clientHeartbeat)))) {
        ds::Set(shared->unlockCommand,0);ds::Set(shared->unlockStatus,0);return;
    }
    if(DWORD(GetTickCount()-DWORD(ds::Read(shared->unlockRequestedAt)))>15000) {
        if(!ds::Read(shared->unlockError))ds::Set(shared->unlockError,ds::UnlockTimeout);
        ds::Set(shared->unlockCommand,0);ds::Set(shared->unlockStatus,-1);return;
    }
    void* manager=nullptr;void* skillList=nullptr;void* artifactList=nullptr;void* skillTable=nullptr;void* artifactTable=nullptr;
    void* klass=artifactApi.Class("","UnlockManager");
    // This persistent manager may not exist yet on the loading screen. Leave
    // the command queued until native initialization has populated its lists.
    if(!klass){UnlockFailed(ds::UnlockManagerType);return;}
    if(!artifactApi.Static(klass,"get_Instance",&manager) || !manager){UnlockFailed(ds::UnlockManagerInstance);return;}
    if(!Field(manager,0x28,skillList) || !skillList){UnlockFailed(ds::UnlockManagerSkills);return;}
    if(!Field(manager,0x30,artifactList) || !artifactList){UnlockFailed(ds::UnlockManagerArtifacts);return;}
    if(InterlockedExchange(&shared->unlockCommand,0)!=1 || InterlockedCompareExchange(&shared->unlockStatus,2,1)!=1)return;
    ManagedRoot managerRoot(artifactApi,manager),controllerRoot(artifactApi,controller);
    ds::Set(shared->unlockError,ds::UnlockNone);
    if(!managerRoot.handle || !controllerRoot.handle){UnlockFailed(ds::UnlockRoot);ds::Set(shared->unlockStatus,-1);return;}
    std::vector<int> skills,artifacts;
    bool ready=LoadUnlockTable(ds::SkillUnlockCatalog,true,&skillTable) &&
        LoadUnlockTable(ds::ArtifactUnlockCatalog,false,&artifactTable);
    if(ready && !CatalogUnlockIds(skillTable,true,skills))ready=UnlockFailed(ds::UnlockSkillCatalog);
    if(ready && !CatalogUnlockIds(artifactTable,false,artifacts))ready=UnlockFailed(ds::UnlockArtifactCatalog);
    bool changed=false;
    bool complete=ready && CompleteUnlockIds(controller,skillList,skills,true,shared->unlockedSkills,changed) &&
        CompleteUnlockIds(controller,artifactList,artifacts,false,shared->unlockedArtifacts,changed);
    // SaveSkill/ArtifactUnlockClear update the native ES3 dictionary. Sync the
    // local ES3 file once, including partial progress if a later item failed.
    // No PurchaseManager, cloud grants or currency consumption are invoked.
    unlockSavePending=unlockSavePending || changed;
    if(unlockSavePending) {
        void* file=nullptr;
        if(!artifactApi.Call(controller,"get_ES3File",0,nullptr,&file) || !file)complete=UnlockFailed(ds::UnlockSaveFile);
        else if(!artifactApi.Call(file,"Sync",0))complete=UnlockFailed(ds::UnlockFileSync);
        else unlockSavePending=false;
    }
    ds::Set(shared->unlockStatus,complete?3:-1);
}
void __fastcall OnDataUpdate(void* self,const void* method) {
    originalDataUpdate(self,method);
    ProcessSyncRetries(GetTickCount());
    try {ProcessWallet(self);}catch(...) {ds::Set(shared->walletCommand,0);WalletFailure(8);}
    // Only Unity's main thread executes managed game APIs. Never propagate an
    // allocation failure in the adapter into the native Update callback.
    try {ProcessUnlockAll(self);}catch(...) {UnlockFailed(ds::UnlockAdapterException);ds::Set(shared->unlockCommand,0);ds::Set(shared->unlockStatus,-1);}
}
