// Execute the actual capacity instruction bytes in small, self-owned x64
// fixtures. They test the acquisition route independently of ResizeSkills.
int CapacityNativePick(const std::vector<int>& ids,bool buff) {
    std::vector<unsigned char> bytes{0x53};
    auto native=[&](uintptr_t r,size_t count){auto* p=reinterpret_cast<unsigned char*>(gameBase+r);bytes.insert(bytes.end(),p,p+count);};
    if(buff)native(0xfa91e3,5);else {bytes.push_back(0x33);bytes.push_back(0xdb);}
    size_t loop=bytes.size();for(auto b:{0x83,0x3c,0x99,0xff,0x74,0})bytes.push_back(static_cast<unsigned char>(b));size_t found=bytes.size()-1;
    bytes.insert(bytes.end(),{0xff,0xc3});native(buff?0xfa9238:0xfa9178,3);
    bytes.push_back(0x7c);bytes.push_back(static_cast<unsigned char>(int(loop)-int(bytes.size()+1)));
    native(buff?0xfa9241:0xfa9181,5);size_t done=bytes.size();bytes[found]=static_cast<unsigned char>(done-(found+1));
    bytes.insert(bytes.end(),{0x8b,0xc3,0x5b,0xc3});
    void* code=VirtualAlloc(nullptr,bytes.size(),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(code);memcpy(code,bytes.data(),bytes.size());
    DWORD old=0;assert(VirtualProtect(code,bytes.size(),PAGE_EXECUTE_READ,&old));FlushInstructionCache(GetCurrentProcess(),code,bytes.size());
    int result=reinterpret_cast<int(__fastcall*)(const int*)>(code)(ids.data());assert(VirtualFree(code,0,MEM_RELEASE));return result;
}
int CapacityNativeFull(uintptr_t compare,int count,bool over=false) {
    unsigned char bytes[]={0x8b,0xc2,0,0,0,0x0f,0x9d,0xc0,0x0f,0xb6,0xc0,0xc3};
    memcpy(bytes+2,reinterpret_cast<void*>(gameBase+compare),3);if(over)bytes[6]=0x9f;
    void* code=VirtualAlloc(nullptr,sizeof(bytes),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(code);memcpy(code,bytes,sizeof(bytes));DWORD old=0;
    assert(VirtualProtect(code,sizeof(bytes),PAGE_EXECUTE_READ,&old));FlushInstructionCache(GetCurrentProcess(),code,sizeof(bytes));
    int result=reinterpret_cast<int(__fastcall*)(void*,int)>(code)(nullptr,count);assert(VirtualFree(code,0,MEM_RELEASE));return result;
}
void CapacityAllPool(void* self,int count,const void*) {
    void* list=nullptr;void* parent=nullptr;std::vector<void*> boxes;assert(Field(self,0x28,list)&&Field(self,0x20,parent)&&ManagedItems(list,boxes));
    while(int(boxes.size())<count){void* box=ArtiClone(boxes.empty()?parent:boxes.front(),parent,false,nullptr);assert(box);ArtiAdd(list,box);boxes.push_back(box);}
}
void* CapacityPanel(size_t listOffset=0x28,size_t parentOffset=0x20,int count=14) {
    void* self=ArtiObject(0xb1);void* parent=ArtiObject(ArtiRectClass);void* game=ArtiObject(0xb2);Put(parent,0x40,game);
    float rect[]={0,0,520,320};memcpy(static_cast<unsigned char*>(parent)+0x100,rect,16);
    void* list=ArtiList();for(int i=0;i<count;++i)ArtiAdd(list,ArtiClone(parent,parent,false,nullptr));Put(self,listOffset,list);Put(self,parentOffset,parent);return self;
}
int capacityTransactions=0;const void* capacityTransactionMethod=nullptr;
__declspec(noinline) void __fastcall CapacityAllUiFixture(void* self,const void* method) {
    capacityTransactionMethod=method;void* list=nullptr;std::vector<void*> boxes;assert(Field(self,0x28,list)&&ManagedItems(list,boxes));
    auto items=ArtiItems(artiOwner);int used=0;for(int i=0;i<ArtiInt(artiOwner,0xac)+artifactTracking.backpack;++i)if(ArtiInt(items[i],0x14)>=0){assert(used<int(boxes.size()));Put(boxes[used++],0x90,i);}
    ++capacityTransactions;
}
__declspec(noinline) void __fastcall CapacityAllSkillUiFixture(void* self,const void* method) {
    capacityTransactionMethod=method;void* list=nullptr;std::vector<void*> boxes;assert(Field(self,0x28,list)&&ManagedItems(list,boxes));
    auto items=CapacitySkills(capacitySkillOwner);int used=0;for(int i=0;i<skillCurrentSlots+skillCurrentBuffSlots+skillCurrentBackpack;++i)if(ArtiInt(items[i],0x14)>=0){assert(used<int(boxes.size()));Put(boxes[used++],0xa8,i);}
    ++capacityTransactions;
}
void CapacityLevelUiFixture(void* self,const void* method) {
    capacityTransactionMethod=method;void* list=nullptr;std::vector<void*> boxes;assert(Field(self,0xe0,list)&&ManagedItems(list,boxes));
    auto items=CapacitySkills(capacitySkillOwner);int used=0;for(int i=0;i<int(items.size());++i)if(ArtiInt(items[i],0x14)>=0){assert(used<int(boxes.size()));Put(boxes[used++],0xa8,i);}++capacityTransactions;
}
void CapacityBackUiFixture(void* self,const void* method) {
    capacityTransactionMethod=method;void* list=nullptr;std::vector<void*> boxes;assert(Field(self,0x30,list)&&ManagedItems(list,boxes));assert(int(boxes.size())>=skillCurrentBackpack);
    for(int i=0;i<skillCurrentBackpack;++i)Put(boxes[i],0xa8,skillCurrentSlots+skillCurrentBuffSlots+i);
}
void R11AcquisitionTests(ds::Shared& data,const void* method) {
    data.clientHeartbeat=LONG(GetTickCount());data.enabled=ds::ArtifactCapacity|ds::ArtifactInventoryCapacity|ds::SkillCapacity|ds::SkillInventoryCapacity;
    data.artifactSlots=data.inventorySlots=data.skillSlots=data.skillInventorySlots=30;
    capacitySkillOwner=ArtiObject(0xb3);OnSkillInit(capacitySkillOwner,method);
    assert(skillCurrentSlots==30&&skillCurrentBackpack==30&&CapacitySkills(capacitySkillOwner).size()==68);
    auto nativeSkills=CapacitySkills(capacitySkillOwner);Put(nativeSkills[30],0x14,600);Put(nativeSkills[31],0x14,601);Put(nativeSkills[61],0x14,900);
    assert(ArtiInt(nativeSkills[30],0x3c)==30);
    data.skillSlots=12;data.skillInventorySlots=12;assert(ApplySkillCapacity(capacitySkillOwner));
    assert(skillCurrentSlots==12&&skillCurrentBackpack==30&&data.skillInventoryStatus==2);auto small=CapacitySkills(capacitySkillOwner);
    assert(small[12]==nativeSkills[30]&&small[43]==nativeSkills[61]&&ArtiInt(small[12],0x3c)==12&&ArtiInt(small[43],0x3c)==43);
    Put(small[43],0x14,-1);assert(ApplySkillCapacity(capacitySkillOwner)&&skillCurrentBackpack==12);
    // Blocking one capacity must not block a change to the independent one.
    Put(CapacitySkills(capacitySkillOwner)[11],0x14,111);data.skillSlots=6;data.skillInventorySlots=30;
    assert(ApplySkillCapacity(capacitySkillOwner)&&skillCurrentSlots==12&&skillCurrentBackpack==30&&data.skillStatus==2);
    Put(CapacitySkills(capacitySkillOwner)[11],0x14,-1);data.skillSlots=30;assert(ApplySkillCapacity(capacitySkillOwner));
    for(int n:{6,12,30,31,32,50})for(int b:{6,12,30}) {
        assert(SetSkillNativeLimit(n,b));std::vector<int> ids(n+b+8,-1);
        assert(CapacityNativePick(ids,false)==0&&CapacityNativePick(ids,true)==n);
        for(int i=0;i<n;++i)ids[i]=100+i;assert(CapacityNativePick(ids,false)==n+2&&CapacityNativePick(ids,true)==n);
        ids[n]=600;assert(CapacityNativePick(ids,true)==n+1);ids[n+1]=601;assert(CapacityNativePick(ids,true)==n+2);
        ids[n-1]=-1;assert(CapacityNativePick(ids,false)==n-1);assert(CapacityNativePick(ids,true)==n+2);
        assert(!CapacityNativeFull(0xf8208b,b-1)&&CapacityNativeFull(0xf8208b,b));
        assert(!CapacityNativeFull(0xf820ab,b,true)&&CapacityNativeFull(0xf820ab,b+1,true));
    }
    assert(SetSkillNativeLimit(30,30));
    assert(CapacitySixValue(capacitySkillOwner,0x106cbd1,method)==30&&CapacitySixValue(capacitySkillOwner,0x11434fc,method)==30);
    assert(CapacitySixValue(capacitySkillOwner,0x123456,method)==6&&CapacitySixValue(artiOwner,0x106cbd1,method)==6);
    checkArtifactAllPool=checkSkillAllPool=CapacityAllPool;checkSkillInventoryPool=CapacityPool;
    originalSkillLevelListInit=CapacityLevelUiFixture;originalSkillInventoryInit=originalViewerSkillInventoryInit=CapacityBackUiFixture;
    for(auto entry:{std::pair<void*,void*>{reinterpret_cast<void*>(&CapacityAllUiFixture),reinterpret_cast<void*>(&OnArtifactAllListInit)},
                   {reinterpret_cast<void*>(&CapacityAllSkillUiFixture),reinterpret_cast<void*>(&OnSkillAllListInit)}}) {
        void** original=entry.first==reinterpret_cast<void*>(&CapacityAllUiFixture)?reinterpret_cast<void**>(&originalArtifactAllListInit):reinterpret_cast<void**>(&originalSkillAllListInit);
        assert(MH_CreateHook(entry.first,entry.second,original)==MH_OK&&MH_EnableHook(entry.first)==MH_OK);
    }
    GameAction volatile artifactEvent=CapacityAllUiFixture,skillEvent=CapacityAllSkillUiFixture;
    artiOwner=ArtiSetup();assert(ApplyArtifactCapacity(artiOwner));auto artifacts=ArtiItems(artiOwner);
    for(int i=0;i<59;++i)Put(artifacts[i],0x14,100+i);
    auto skills=CapacitySkills(capacitySkillOwner);for(int i=0;i<61;++i)Put(skills[i],0x14,200+i);
    void* artifactPanel=CapacityPanel(),*skillPanel=CapacityPanel();
    // Match the native order: currency is deducted, acquisition refreshes
    // subscribed UI lists, then the completion callback marks sold/picked.
    int gold=1000;bool sold=false,picked=false;gold-=100;Put(artifacts[59],0x14,777);
    artifactEvent(artifactPanel,method);sold=true;assert(gold==900&&sold&&capacityTransactionMethod==method);
    int purchases=1;if(!sold){gold-=100;++purchases;}assert(purchases==1&&gold==900);
    Put(skills[61],0x14,888);skillEvent(skillPanel,method);picked=true;assert(picked&&capacityTransactionMethod==method);
    void* levelPanel=CapacityPanel(0xe0,0xd8);OnSkillLevelListInit(levelPanel,method);assert(capacityTransactionMethod==method);
    // Native LevelUp displays its result before Init consumes the selected
    // SkillDataNew entries. Expanded event-list refreshes must return first.
    skillEvent(skillPanel,method);for(int index:{40,60}){Put(skills[index],0x14,-1);}OnSkillLevelListInit(levelPanel,method);
    assert(ArtiInt(skills[40],0x14)==-1&&ArtiInt(skills[60],0x14)==-1&&ArtiInt(skills[30],0x14)==230);
    void* inventoryPanel=CapacityPanel(0x30,0x20,6);OnViewerSkillInventoryInit(inventoryPanel,method);
    void* list=nullptr;std::vector<void*> boxes;assert(Field(inventoryPanel,0x30,list)&&ManagedItems(list,boxes)&&boxes.size()==30);
    for(int i=0;i<30;++i)assert(ArtiInt(boxes[i],0xa8)==32+i);
    for(int physical:{7,12,30,31,32,50})for(int native:{6,7})for(int b:{6,12,30})for(int n:{6,12,30,31,32,50})for(int sb:{6,12,30}) {
        int p=0,a=0,back=0,equip=0,skillBack=0;int tag=ds::artifacts::SaveLayout(physical,native,b,n,sb);
        assert(ds::artifacts::Tagged(tag)&&ds::artifacts::LoadLayout(tag,p,a,back,equip,skillBack)&&p==physical&&a==native&&back==b&&equip==n&&skillBack==sb);
    }
    void* save=ArtiObject(0xb4);void* records=ArtiList();void* record=ArtiObject(ArtiItemClass);Put(record,0x10,999);Put(record,0x14,61);ArtiAdd(records,record);
    Put(save,0x1cb0,records);Put(save,0x1cd0,ArtiList());Put(save,0x1cd8,ds::artifacts::SaveLayout(30,6,30,30,30));
    data.enabled=0;OnSkillLoad(capacitySkillOwner,save,method);
    assert(!skillLoadFailed&&skillCurrentBackpack==30&&data.skillInventoryStatus==2&&ArtiInt(CapacitySkills(capacitySkillOwner)[37],0x14)==999);
    assert(skillCurrentSlots==6&&ArtiInt(CapacitySkills(capacitySkillOwner)[37],0x14)==999);
    // The occupied backpack item moves with the normal boundary on shrink.
    Put(CapacitySkills(capacitySkillOwner)[37],0x14,-1);assert(ApplySkillCapacity(capacitySkillOwner)&&skillCurrentBackpack==6);
    Put(save,0x1cd8,ds::artifacts::InventoryLayoutTag);int loads=capacityLoadCalls;OnSkillLoad(capacitySkillOwner,save,method);assert(skillLoadFailed&&capacityLoadCalls==loads);
    data.enabled=0;OnSkillInit(capacitySkillOwner,method);assert(!skillLoadFailed);
    // Maximum equipment plus maximum independent backpacks: 87 artifact and
    // 88 skill records including native reserves. Event callbacks must finish.
    data.enabled=ds::ArtifactCapacity|ds::ArtifactInventoryCapacity|ds::SkillCapacity|ds::SkillInventoryCapacity;
    data.artifactSlots=data.skillSlots=50;data.inventorySlots=data.skillInventorySlots=30;
    OnSkillInit(capacitySkillOwner,method);artiOwner=ArtiSetup();assert(ApplyArtifactCapacity(artiOwner));
    assert(ArtiItems(artiOwner).size()==87 && CapacitySkills(capacitySkillOwner).size()==88);
    for(int i=0;i<80;++i)Put(ArtiItems(artiOwner)[i],0x14,100+i);
    for(int i=0;i<82;++i)Put(CapacitySkills(capacitySkillOwner)[i],0x14,200+i);
    artifactEvent(artifactPanel,method);skillEvent(skillPanel,method);OnSkillLevelListInit(levelPanel,method);
    assert(Field(artifactPanel,0x28,list) && ManagedItems(list,boxes) && boxes.size()>=80 && ArtiInt(boxes[79],0x90)==79);
    assert(Field(skillPanel,0x28,list) && ManagedItems(list,boxes) && boxes.size()>=82 && ArtiInt(boxes[81],0xa8)==81);
    OnViewerSkillInventoryInit(inventoryPanel,method);assert(Field(inventoryPanel,0x30,list) && ManagedItems(list,boxes) && ArtiInt(boxes[29],0xa8)==81);
    // A fresh high-capacity save keeps both buff entries and both last
    // backpack/equipment entries, even when all expansion switches are off.
    void* highSave=ArtiObject(0xb4);void* highSkills=ArtiList();void* highArtifacts=ArtiList();
    for(int index:{49,50,51,81}) {void* item=ArtiObject(ArtiItemClass);Put(item,0x10,200+index);Put(item,0x14,index);ArtiAdd(highSkills,item);}
    for(int index:{49,79}) {void* item=ArtiObject(ArtiItemClass);Put(item,0x18,index);Put(item,0x14,100+index);Put(item,0x1c,4);ArtiAdd(highArtifacts,item);}
    Put(highSave,0x1cb0,highSkills);Put(highSave,0x1cd0,highArtifacts);Put(highSave,0x1cd8,ds::artifacts::SaveLayout(50,6,30,50,30));
    artifactSaveHint.Reset();data.enabled=0;OnSkillLoad(capacitySkillOwner,highSave,method);
    assert(!skillLoadFailed && skillCurrentSlots==50 && skillCurrentBackpack==30 && data.skillStatus==2);
    auto highLoaded=CapacitySkills(capacitySkillOwner);for(int index:{49,50,51,81})assert(ArtiInt(highLoaded[index],0x14)==200+index);
    artiOwner=ArtiSetup();OnArtifactLoad(artiOwner,highSave,method);
    assert(!artifactLoadFailed && ArtiInt(artiOwner,0xac)==50 && artifactTracking.backpack==30 && data.artifactStatus==2);
    assert(ArtiInt(ArtiItems(artiOwner)[49],0x14)==149 && ArtiInt(ArtiItems(artiOwner)[79],0x1c)==4);
    Put(highSave,0x1cd8,50);StampArtifactSave(artiOwner,highSave);
    int highPhysical=0,highNative=0,highBack=0,highNormal=0,highSkillBack=0;
    assert(ds::artifacts::LoadLayout(ArtiInt(highSave,0x1cd8),highPhysical,highNative,highBack,highNormal,highSkillBack) && highPhysical==50 && highNormal==50 && highBack==30 && highSkillBack==30);
    RestoreArtifactSavePreview(highSave);assert(ArtiInt(highSave,0x1cd8)==50 && SavedLayout(highSave,highPhysical,highNative,highBack,highNormal,highSkillBack) && highNormal==50);
    Put(highLoaded[49],0x14,-1);Put(highLoaded[81],0x14,-1);assert(ApplySkillCapacity(capacitySkillOwner));
    assert(skillCurrentSlots==6 && skillCurrentBackpack==6 && ArtiInt(CapacitySkills(capacitySkillOwner)[6],0x14)==250 && ArtiInt(CapacitySkills(capacitySkillOwner)[7],0x14)==251);
    Put(ArtiItems(artiOwner)[49],0x14,-1);Put(ArtiItems(artiOwner)[79],0x14,-1);assert(ApplyArtifactCapacity(artiOwner) && ArtiInt(artiOwner,0xac)==7 && artifactTracking.backpack==6);
    for(int tag:{ds::artifacts::SaveLayout(51,6,30,50,30),ds::artifacts::SaveLayout(50,6,51,50,30),ds::artifacts::SaveLayout(50,6,30,51,30),ds::artifacts::SaveLayout(50,6,30,50,51),ds::artifacts::LayoutTag|31|(6<<5)|(6<<10)})
        assert(!ds::artifacts::LoadLayout(tag,highPhysical,highNative,highBack,highNormal,highSkillBack));
    for(void* target:{reinterpret_cast<void*>(&CapacityAllUiFixture),reinterpret_cast<void*>(&CapacityAllSkillUiFixture)}){assert(MH_DisableHook(target)==MH_OK&&MH_RemoveHook(target)==MH_OK);}
    std::cout<<"PASS: executed native SkillGet normal/buff/backpack boundary bytes and full/overflow thresholds, independent skill backpack 6..30, index/newIndex remap, 6-bit equipment save tags + r9/r10 compatibility, occupied-tail resume, up to 88-entry pools / 50 equipment resume / buff preservation, callback completion/shop sold-state/material consumption after UI events; 2 real UI x64 detours, isolated fixtures only\n";
}
