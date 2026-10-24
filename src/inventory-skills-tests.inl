namespace {
void* capacitySkillOwner=nullptr;int capacityFrames=0,capacityRefreshes=0,capacityInitCalls=0,capacityUiCalls=0,capacityLoadCalls=0;
const void* capacityMethod=nullptr;
std::vector<void*> CapacitySkills(void* owner) {void* list=nullptr;std::vector<void*> items;assert(Field(owner,0x40,list)&&ManagedItems(list,items));return items;}
void __fastcall CapacitySkillCtor(void* self,int index,const void*){Put(self,0x10,index);Put(self,0x3c,index);Put(self,0x14,-1);Put(self,0x30,0);}
__declspec(noinline) void __fastcall CapacityInitFixture(void* self,const void* method) {
    ++capacityInitCalls;capacityMethod=method;
    void* list=ArtiList();for(int i=0;i<skillCurrentSlots+skillCurrentBuffSlots+skillCurrentBackpack+6;++i){void* item=ArtiObject(ArtiItemClass);CapacitySkillCtor(item,i,nullptr);ArtiAdd(list,item);}Put(self,0x40,list);Put(self,0x6c,true);
}
__declspec(noinline) void __fastcall CapacityUpdateFixture(void* self,const void* method){assert(self==capacitySkillOwner);++capacityFrames;capacityMethod=method;}
void __fastcall CapacityRefresh(void* self,const void*){assert(self==capacitySkillOwner);++capacityRefreshes;}
int __fastcall CapacitySixFixture(void*,const void*){return 6;}
int __fastcall CapacityEightFixture(void*,const void*){return 8;}
void __fastcall CapacityPool(void* self,int count,const void*) {
    void* list=nullptr;void* parent=nullptr;std::vector<void*> boxes;assert(Field(self,0x30,list)&&Field(self,0x20,parent)&&ManagedItems(list,boxes));
    while(int(boxes.size())<count){void* box=ArtiClone(boxes.empty()?parent:boxes.front(),parent,false,nullptr);assert(box);ArtiAdd(list,box);boxes.push_back(box);}
}
void CapacityInventoryFixture(void* self,const void*) {
    ++capacityUiCalls;void* list=nullptr;std::vector<void*> boxes;assert(Field(self,0x30,list)&&ManagedItems(list,boxes));
    int physical=ArtiInt(artiOwner,0xac);auto items=ArtiItems(artiOwner);assert(int(boxes.size())>=artifactTracking.backpack);
    for(int i=0;i<artifactTracking.backpack;++i){int index=physical+i;assert(ArtiInt(items[index],0x18)==index);Put(boxes[i],0x90,index);Put(boxes[i],0x8c,ArtiInt(items[index],0x14));}
}
__declspec(noinline) void __fastcall CapacitySkillUiFixture(void* self,const void* method) {
    ++capacityUiCalls;capacityMethod=method;void* list=nullptr;std::vector<void*> boxes;assert(Field(self,0x28,list)&&ManagedItems(list,boxes));
    assert(int(boxes.size())>=skillCurrentSlots);auto skills=CapacitySkills(capacitySkillOwner);
    for(int i=0;i<skillCurrentSlots;++i){assert(ArtiInt(skills[i],0x10)==i);Put(boxes[i],0xa8,i);Put(boxes[i],0xac,ArtiInt(skills[i],0x14));}
}
__declspec(noinline) void __fastcall CapacitySkillLoadFixture(void* self,void* container,const void* method) {
    ++capacityLoadCalls;GameAction volatile init=CapacityInitFixture;init(self,method);
    void* list=nullptr;std::vector<void*> records;assert(Field(container,0x1cb0,list)&&ManagedItems(list,records));auto skills=CapacitySkills(self);
    for(void* record:records){int at=ArtiInt(record,0x14);assert(at>=0&&at<int(skills.size()));Put(skills[at],0x14,ArtiInt(record,0x10));}
}
#include "acquisition-tests.inl"
#include "expanded-capacity-tests.inl"
void InventorySkillTests(ds::Shared& data,const void* method) {
    uintptr_t oldBase=gameBase;auto oldApi=artifactApi;const auto oldMetadata=initializeMetadata;const auto oldController=getDataController;void* oldUsage=dataControllerMethodUsage;
    auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));assert(code);gameBase=reinterpret_cast<uintptr_t>(code);
    for(const auto& patch:ds::SkillCapacityPatches)memcpy(code+patch.rva,patch.prefix,16);
    for(const auto& patch:ds::ArtifactCapacityPatches)memcpy(code+patch.rva,patch.prefix,16);
    skillCurrentSlots=skillCurrentBackpack=artifactCurrentBackpack=6;skillCurrentBuffSlots=2;skillOwner=nullptr;skillLoading=skillLoadFailed=skillUiFailed=false;
    artifactApi.objectClass=ArtiClass;artifactApi.findMethod=ArtiFindMethod;artifactApi.invoke=ArtiInvoke;artifactApi.newObject=ArtiNew;
    artifactApi.rootNew=ArtiRoot;artifactApi.rootFree=ArtiFree;artifactApi.rootTarget=ArtiTarget;artifactApi.barrier=ArtiBarrier;
    artifactApi.domain=ArtiDomain;artifactApi.assemblies=ArtiAssemblies;artifactApi.image=ArtiImage;artifactApi.findClass=ArtiFindClass;artifactApi.classType=ArtiType;artifactApi.typeObject=ArtiTypeObject;
    constructArtifact=ArtiConstructor;constructSkill=CapacitySkillCtor;cloneArtifactBox=ArtiClone;componentTransform=ArtiTransform;componentGameObject=ArtiGame;
    getTypeComponent=ArtiGetComponent;addTypeComponent=ArtiAddComponent;destroyArtifactObject=ArtiDestroy;getArtifactManager=ArtiInstance;skillChanged=CapacityRefresh;
    originalCapacitySix=CapacitySixFixture;originalCapacityEight=CapacityEightFixture;checkArtifactInventoryPool=CapacityPool;originalInventoryInit=originalViewerInventoryInit=CapacityInventoryFixture;
    data.state=ds::Ready;data.available=ds::All;data.clientHeartbeat=LONG(GetTickCount());data.enabled=0;data.artifactSlots=12;data.inventorySlots=12;data.skillSlots=12;data.skillInventorySlots=12;
    // Every native operand is reversible at every supported capacity; the
    // shared constant aliases are untouched unless self is the live manager.
    for(int n=6;n<=50;++n)for(int b=6;b<=50;++b)for(int buffs:{2,6}){assert(SetSkillNativeLimit(n,b,buffs));for(const auto& patch:ds::SkillCapacityPatches){int value=0;if(patch.width==1){signed char byte=0;assert(Field(code+patch.rva+patch.offset,0,byte));value=byte;}else assert(Field(code+patch.rva+patch.offset,0,value));int delta=patch.basis==1?b-6:patch.basis==2?n+b-12:n-6;assert(value==patch.value+patch.sign*delta+patch.buffSign*(buffs-2));}}
    data.clientHeartbeat=LONG(GetTickCount());
    assert(SetSkillNativeLimit(6));for(const auto& patch:ds::SkillCapacityPatches)assert(memcmp(code+patch.rva,patch.prefix,16)==0);
    const auto& first=ds::SkillCapacityPatches[0];code[first.rva+first.offset]^=1;assert(!SetSkillNativeLimit(12)&&skillCurrentSlots==6);code[first.rva+first.offset]^=1;
    void* owner=ArtiSetup();auto old=ArtiItems(owner);Put(old[7],0x14,701);Put(old[7],0x1c,4);
    assert(!ApplyArtifactCapacity(owner));assert(CapacitySixValue(owner,0,method)==6);
    data.enabled=ds::ArtifactInventoryCapacity;assert(ApplyArtifactCapacity(owner));auto back=ArtiItems(owner);
    assert(ArtiInt(owner,0xac)==7&&ArtiInt(owner,0xb0)==6&&artifactTracking.backpack==12&&back.size()==26&&back[7]==old[7]&&back[19]==old[19]);
    assert(CapacitySixValue(owner,0x103c45f,method)==12&&CapacitySixValue(nullptr,0,method)==6);
    for(int i=0;i<int(back.size());++i)assert(ArtiInt(back[i],0x18)==i);
    assert(!ArtifactFull(owner,false));for(int i=0;i<12;++i)Put(back[7+i],0x14,700+i);assert(ArtifactFull(owner,false));
    data.inventorySlots=30;assert(ApplyArtifactCapacity(owner));back=ArtiItems(owner);assert(back.size()==44&&artifactTracking.backpack==30);
    Put(back[36],0x14,999);data.enabled=0;assert(!ApplyArtifactCapacity(owner)&&data.inventoryStatus==2&&artifactTracking.backpack==30&&ArtiInt(back[36],0x14)==999);
    for(int i=6;i<30;++i)Put(back[7+i],0x14,-1);assert(ApplyArtifactCapacity(owner)&&artifactTracking.backpack==6&&ArtiItems(owner).size()==20);
    data.enabled=ds::ArtifactInventoryCapacity;data.inventorySlots=30;artifactViewerOpen=true;assert(!ApplyArtifactCapacity(owner)&&data.inventoryStatus==3);artifactViewerOpen=false;
    void* beforeList=nullptr;assert(Field(owner,0xa0,beforeList));artiAllocationLimit=1;assert(!ApplyArtifactCapacity(owner));void* afterList=nullptr;assert(Field(owner,0xa0,afterList)&&beforeList==afterList);artiAllocationLimit=-1;
    assert(ApplyArtifactCapacity(owner));void* parent=ArtiObject(ArtiRectClass);float rect[]={0,0,520,240};memcpy(static_cast<unsigned char*>(parent)+0x100,rect,16);void* game=ArtiObject(0x94);Put(parent,0x40,game);
    void* panel=ArtiObject(0x97);void* pool=ArtiList();for(int i=0;i<6;++i)ArtiAdd(pool,ArtiClone(parent,parent,false,nullptr));Put(panel,0x20,parent);Put(panel,0x30,pool);
    OnViewerInventoryInit(panel,method);std::vector<void*> boxes;assert(ManagedItems(pool,boxes)&&boxes.size()==30);for(int i=0;i<30;++i)assert(ArtiInt(boxes[i],0x90)==7+i);
    assert(data.artifactStatus>=0);data.enabled=0;assert(ApplyArtifactCapacity(owner));OnInventoryInit(panel,method);assert(ManagedItems(pool,boxes)&&boxes.size()==6);
    // Occupied equipped slots must not prevent an independent backpack resize.
    data.enabled=ds::ArtifactCapacity|ds::ArtifactInventoryCapacity;data.artifactSlots=12;data.inventorySlots=12;
    assert(ApplyArtifactCapacity(owner));Put(ArtiItems(owner)[11],0x14,444);
    data.enabled=ds::ArtifactInventoryCapacity;data.inventorySlots=30;
    assert(ApplyArtifactCapacity(owner)&&ArtiInt(owner,0xac)==12&&ArtiInt(owner,0xb0)==12&&artifactTracking.backpack==30&&data.artifactStatus==2);
    Put(ArtiItems(owner)[11],0x14,-1);assert(ApplyArtifactCapacity(owner)&&ArtiInt(owner,0xac)==7&&artifactTracking.backpack==30);
    data.enabled=0;assert(ApplyArtifactCapacity(owner)&&artifactTracking.backpack==6);
    // Actual x64 detours exercise Update, native list initialization, indexed
    // resume, and UI callback population on self-owned fixture functions.
    for(auto entry:{std::pair<void*,void*>{reinterpret_cast<void*>(&CapacityInitFixture),reinterpret_cast<void*>(&OnSkillInit)},
                   {reinterpret_cast<void*>(&CapacityUpdateFixture),reinterpret_cast<void*>(&OnSkillUpdate)},
                   {reinterpret_cast<void*>(&CapacitySkillLoadFixture),reinterpret_cast<void*>(&OnSkillLoad)},
                   {reinterpret_cast<void*>(&CapacitySkillUiFixture),reinterpret_cast<void*>(&OnViewerSkillEquipInit)}}) {
        void** original=entry.first==reinterpret_cast<void*>(&CapacityInitFixture)?reinterpret_cast<void**>(&originalSkillInit):entry.first==reinterpret_cast<void*>(&CapacityUpdateFixture)?reinterpret_cast<void**>(&originalSkillUpdate):entry.first==reinterpret_cast<void*>(&CapacitySkillLoadFixture)?reinterpret_cast<void**>(&originalSkillLoad):reinterpret_cast<void**>(&originalViewerSkillEquipInit);
        assert(MH_CreateHook(entry.first,entry.second,original)==MH_OK);assert(MH_EnableHook(entry.first)==MH_OK);
    }
    GameAction volatile init=CapacityInitFixture,frame=CapacityUpdateFixture,ui=CapacitySkillUiFixture;ArtifactLoad volatile load=CapacitySkillLoadFixture;
    capacitySkillOwner=ArtiObject(0xa1);init(capacitySkillOwner,method);auto skills=CapacitySkills(capacitySkillOwner);assert(skills.size()==20&&skillCurrentSlots==6&&capacityMethod==method);
    Put(skills[0],0x14,100);Put(skills[0],0x30,8);Put(skills[6],0x14,600);Put(skills[7],0x14,601);Put(skills[8],0x14,800);void* buff=skills[6];void* backpackItem=skills[8];
    data.enabled=ds::SkillCapacity;frame(capacitySkillOwner,method);auto expanded=CapacitySkills(capacitySkillOwner);
    assert(skillCurrentSlots==12&&expanded.size()==26&&capacityRefreshes==1&&expanded[12]==buff&&expanded[14]==backpackItem&&expanded[0]==skills[0]&&ArtiInt(expanded[0],0x30)==8);
    for(int i=0;i<26;++i)assert(ArtiInt(expanded[i],0x10)==i);for(int i=6;i<12;++i)assert(ArtiInt(expanded[i],0x14)==-1);
    assert(CapacitySixValue(capacitySkillOwner,0x106c788,method)==12&&CapacitySixValue(capacitySkillOwner,0x106cbd1,method)==6);
    assert(CapacityEightValue(capacitySkillOwner,0xfb1e60,method)==14&&CapacityEightValue(owner,0xfb1e60,method)==8&&CapacityEightValue(capacitySkillOwner,0x111111,method)==8);
    Put(expanded[11],0x14,111);data.skillSlots=6;frame(capacitySkillOwner,method);assert(data.skillStatus==2&&skillCurrentSlots==12&&CapacitySkills(capacitySkillOwner)[12]==buff);
    Put(expanded[11],0x14,-1);frame(capacitySkillOwner,method);assert(skillCurrentSlots==6&&CapacitySkills(capacitySkillOwner)[6]==buff&&CapacitySkills(capacitySkillOwner)[8]==backpackItem);
    data.skillSlots=30;artifactViewerOpen=true;frame(capacitySkillOwner,method);assert(data.skillStatus==3&&skillCurrentSlots==6);artifactViewerOpen=false;
    artiAllocationLimit=1;frame(capacitySkillOwner,method);assert(data.skillStatus==-1&&skillCurrentSlots==6&&CapacitySkills(capacitySkillOwner)[6]==buff);artiAllocationLimit=-1;
    frame(capacitySkillOwner,method);assert(skillCurrentSlots==30&&CapacitySkills(capacitySkillOwner).size()==44&&CapacitySkills(capacitySkillOwner)[30]==buff&&CapacitySkills(capacitySkillOwner)[32]==backpackItem);
    void* skillPanel=ArtiObject(0xa2);void* skillParent=ArtiObject(ArtiRectClass);void* skillGame=ArtiObject(0x94);Put(skillParent,0x40,skillGame);memcpy(static_cast<unsigned char*>(skillParent)+0x100,rect,16);
    void* skillPool=ArtiList();for(int i=0;i<6;++i)ArtiAdd(skillPool,ArtiClone(skillParent,skillParent,false,nullptr));Put(skillPanel,0x20,skillParent);Put(skillPanel,0x28,skillPool);
    ui(skillPanel,method);assert(ManagedItems(skillPool,boxes)&&boxes.size()==30);for(int i=0;i<30;++i)assert(ArtiInt(boxes[i],0xa8)==i);assert(capacityMethod==method);
    data.clientHeartbeat=LONG(GetTickCount()-4000);frame(capacitySkillOwner,method);assert(skillCurrentSlots==6&&CapacitySkills(capacitySkillOwner)[6]==buff&&CapacitySkills(capacitySkillOwner)[8]==backpackItem);data.clientHeartbeat=LONG(GetTickCount());
    ui(skillPanel,method);assert(ManagedItems(skillPool,boxes)&&boxes.size()==6);
    // All capacities share one versioned layout marker. Keep the two native
    // buff slots and inventory indices distinct during save and resume.
    for(int physical:{7,12,30})for(int backpack:{6,12,30})for(int n:{6,12,30})for(int native:{6,7}){
        int p=0,b=0,s=0,a=0;int encoded=ds::artifacts::SaveLayout(physical,native,backpack,n);assert(ds::artifacts::LoadLayout(encoded,p,a,b,s)&&p==physical&&a==native&&b==backpack&&s==n);}
    void* container=ArtiObject(0xa3);void* records=ArtiList();for(auto value:{std::pair<int,int>{120,29},{600,30},{800,32}}){void* item=ArtiObject(ArtiItemClass);Put(item,0x10,value.first);Put(item,0x14,value.second);ArtiAdd(records,item);}Put(container,0x1cb0,records);Put(container,0x1cd0,ArtiList());
    const int encoded=ds::artifacts::SaveLayout(7,6,30,30);Put(container,0x1cd8,encoded);data.enabled=0;load(capacitySkillOwner,container,method);
    assert(!skillLoadFailed&&skillCurrentSlots==30&&data.skillStatus==2&&ArtiInt(CapacitySkills(capacitySkillOwner)[29],0x14)==120&&ArtiInt(CapacitySkills(capacitySkillOwner)[30],0x14)==600&&ArtiInt(CapacitySkills(capacitySkillOwner)[32],0x14)==800);
    Put(CapacitySkills(capacitySkillOwner)[29],0x14,-1);frame(capacitySkillOwner,method);assert(skillCurrentSlots==6&&ArtiInt(CapacitySkills(capacitySkillOwner)[6],0x14)==600&&ArtiInt(CapacitySkills(capacitySkillOwner)[8],0x14)==800);
    std::vector<void*> savedRecords;assert(ManagedItems(records,savedRecords));Put(savedRecords[0],0x14,99);int calls=capacityLoadCalls;load(capacitySkillOwner,container,method);assert(skillLoadFailed&&data.skillStatus==-2&&capacityLoadCalls==calls);
    originalAutoSave=ArtiAutoSaveFixture;int fileBefore=artiFileMetadata;OnAutoSave(nullptr,nullptr,method);assert(artiFileMetadata==fileBefore);
    Put(savedRecords[0],0x14,29);load(capacitySkillOwner,container,method);assert(!skillLoadFailed&&capacityLoadCalls==calls+1);
    // The artifact half restores the independent backpack capacity too.
    originalArtifactLoad=ArtiLoadFixture;artiOwner=ArtiSetup();OnArtifactLoad(artiOwner,container,method);assert(!artifactLoadFailed&&artifactTracking.backpack==6);
    data.enabled=ds::ArtifactInventoryCapacity;data.inventorySlots=30;assert(ApplyArtifactCapacity(artiOwner));auto artifactItems=ArtiItems(artiOwner);Put(artifactItems[36],0x14,900);
    Put(container,0x1cd8,ArtiInt(artiOwner,0xb0));StampArtifactSave(artiOwner,container);int p=0,a=0,b=0,s=0;assert(ds::artifacts::LoadLayout(ArtiInt(container,0x1cd8),p,a,b,s)&&b==30&&s==30);
    RestoreArtifactSavePreview(container);assert(ArtiInt(container,0x1cd8)==p);assert(SavedLayout(container,p,a,b,s)&&b==30&&s==30);
    void* backSave=ArtiObject(0xa3);void* backRecords=ArtiList();void* backRecord=ArtiObject(ArtiItemClass);
    Put(backRecord,0x14,900);Put(backRecord,0x18,36);Put(backRecord,0x1c,3);ArtiAdd(backRecords,backRecord);
    Put(backSave,0x1cd0,backRecords);Put(backSave,0x1cd8,ds::artifacts::SaveLayout(7,6,30,30));
    artiOwner=ArtiSetup();data.enabled=0;OnArtifactLoad(artiOwner,backSave,method);
    assert(!artifactLoadFailed&&artifactTracking.backpack==30&&data.inventoryStatus==2&&ArtiInt(ArtiItems(artiOwner)[36],0x14)==900&&ArtiInt(ArtiItems(artiOwner)[36],0x1c)==3);
    Put(ArtiItems(artiOwner)[36],0x14,-1);assert(ApplyArtifactCapacity(artiOwner)&&artifactTracking.backpack==6);
    // Native and r9 markers remain readable; invalid new tags are rejected.
    Put(container,0x1cb0,ArtiList());artifactSaveHint.Reset();Put(container,0x1cd8,ds::artifacts::SaveCapacity(12,7));load(capacitySkillOwner,container,method);assert(skillCurrentSlots==6&&!skillLoadFailed);
    Put(container,0x1cd8,0x44540000);load(capacitySkillOwner,container,method);assert(skillLoadFailed&&data.skillStatus==-2);
    data.enabled=0;init(capacitySkillOwner,method);assert(!skillLoadFailed&&skillCurrentSlots==6);
    R11AcquisitionTests(data,method);
    ExpandedCapacityTests(data,method);
    for(void* target:{reinterpret_cast<void*>(&CapacityInitFixture),reinterpret_cast<void*>(&CapacityUpdateFixture),reinterpret_cast<void*>(&CapacitySkillLoadFixture),reinterpret_cast<void*>(&CapacitySkillUiFixture)}){assert(MH_DisableHook(target)==MH_OK);assert(MH_RemoveHook(target)==MH_OK);}
    artifactSaveHint.Reset();artifactLayouts.clear();artifactVisuals.clear();assert(artiRoots.empty());assert(SetSkillNativeLimit(6));assert(SetArtifactNativeBackpack(6));assert(VirtualFree(code,0,MEM_RELEASE));
    artifactApi=oldApi;gameBase=oldBase;initializeMetadata=oldMetadata;getDataController=oldController;dataControllerMethodUsage=oldUsage;
    artiMemory.clear();artiMethods.clear();artifactTracking={};artifactPlayUiClass=nullptr;artiPlayUi=nullptr;skillOwner=nullptr;skillLoading=skillLoadFailed=skillUiFailed=false;artifactLoadFailed=false;data.enabled=0;data.clientHeartbeat=LONG(GetTickCount());
    std::cout<<"PASS: 67 reversible native boundary operands for all 4050 normal/backpack/buff combinations with alias scoping, preserved buff/inventory objects and indices, full checks, safe shrink/stale/open-popup waits, allocation failure, inventory/skill UI native callback indices/grid restore, combined save metadata/r9 compatibility, indexed resume and invalid-load autosave guard, 4 real x64 detours; isolated fixtures only\n";
}
}
