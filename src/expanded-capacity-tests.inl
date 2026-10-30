// Verify the newly expanded regions against actual patched machine operands,
// then exercise object migration, UI callbacks and indexed save/load.
bool CapacityNativeBuffFull(int count) {
    unsigned char bytes[]={0x8b,0xca,0,0,0,0x0f,0x9d,0xc0,0x0f,0xb6,0xc0,0xc3};
    memcpy(bytes+2,reinterpret_cast<void*>(gameBase+0xf81a38),3);
    void* code=VirtualAlloc(nullptr,sizeof(bytes),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(code);
    memcpy(code,bytes,sizeof(bytes));DWORD old=0;assert(VirtualProtect(code,sizeof(bytes),PAGE_EXECUTE_READ,&old));
    FlushInstructionCache(GetCurrentProcess(),code,sizeof(bytes));
    bool result=reinterpret_cast<int(__fastcall*)(void*,int)>(code)(nullptr,count)!=0;
    assert(VirtualFree(code,0,MEM_RELEASE));return result;
}
bool CapacityNativeBuffSwap(int index,bool second) {
    // Preserve the native lea eax,[rdi/rbx-normal] and cmp eax,buffs-1.
    std::vector<unsigned char> bytes{static_cast<unsigned char>(second?0x53:0x57),0x8b,static_cast<unsigned char>(second?0xd9:0xf9)};
    auto* native=reinterpret_cast<unsigned char*>(gameBase+(second?0xfacb19:0xfaca8f));
    bytes.insert(bytes.end(),native,native+6);
    bytes.insert(bytes.end(),{0x0f,0x96,0xc0,0x0f,0xb6,0xc0,static_cast<unsigned char>(second?0x5b:0x5f),0xc3});
    void* code=VirtualAlloc(nullptr,bytes.size(),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(code);
    memcpy(code,bytes.data(),bytes.size());DWORD old=0;assert(VirtualProtect(code,bytes.size(),PAGE_EXECUTE_READ,&old));
    FlushInstructionCache(GetCurrentProcess(),code,bytes.size());
    bool result=reinterpret_cast<int(__fastcall*)(int)>(code)(index)!=0;
    assert(VirtualFree(code,0,MEM_RELEASE));return result;
}
__declspec(noinline) void CapacityBuffUiFixture(void* self,const void* method) {
    capacityTransactionMethod=method;void* list=nullptr;std::vector<void*> boxes;
    assert(Field(self,0x28,list)&&ManagedItems(list,boxes)&&int(boxes.size())>=skillCurrentBuffSlots);
    auto skills=CapacitySkills(capacitySkillOwner);
    for(int i=0;i<skillCurrentBuffSlots;++i) {
        int at=skillCurrentSlots+i;assert(ArtiInt(skills[at],0x10)==at);
        Put(boxes[i],0xa8,at);Put(boxes[i],0xac,ArtiInt(skills[at],0x14));
    }
}
void ExpandedCapacityTests(ds::Shared& data,const void* method) {
    for(int n:{6,12,30,31,50})for(int b:{6,30,31,50})for(int buffs:{2,6}) {
        assert(SetSkillNativeLimit(n,b,buffs));std::vector<int> ids(n+buffs+b+6,-1);
        for(int i=0;i<n;++i)ids[i]=i;
        assert(CapacityNativePick(ids,false)==n+buffs);
        for(int i=0;i<buffs;++i){assert(CapacityNativePick(ids,true)==n+i);ids[n+i]=600+i;}
        assert(CapacityNativePick(ids,true)==n+buffs);
        assert(!CapacityNativeBuffFull(buffs-1)&&CapacityNativeBuffFull(buffs));
        for(bool second:{false,true})for(int at:{n-1,n,n+1,n+buffs-1,n+buffs}) {
            assert(CapacityNativeBuffSwap(at,second)==(at>=n&&at<n+buffs));
        }
    }
    for(int p:{7,30,31,50})for(int a:{6,7})for(int b:{6,30,31,50})for(int n:{6,30,31,50})for(int sb:{6,30,31,50})for(int buffs:{2,6}) {
        int rp=0,ra=0,rb=0,rn=0,rsb=0,rf=0;int tag=ds::artifacts::SaveLayout(p,a,b,n,sb,buffs);
        assert(ds::artifacts::Tagged(tag)&&ds::artifacts::LoadLayout(tag,rp,ra,rb,rn,rsb,rf));
        assert(rp==p&&ra==a&&rb==b&&rn==n&&rsb==sb&&rf==buffs);
    }
    data.clientHeartbeat=LONG(GetTickCount());data.enabled=0;
    // R11 removed its detours and freed their trampolines. The next fixture
    // invokes the original UI bodies directly.
    originalArtifactAllListInit=CapacityAllUiFixture;originalSkillAllListInit=CapacityAllSkillUiFixture;
    capacitySkillOwner=ArtiObject(0xb3);OnSkillInit(capacitySkillOwner,method);
    auto native=CapacitySkills(capacitySkillOwner);
    Put(native[6],0x14,600);Put(native[7],0x14,601);Put(native[8],0x14,800);Put(native[8],0x3c,7);
    data.enabled=ds::BuffCapacity;assert(ApplySkillCapacity(capacitySkillOwner));auto grown=CapacitySkills(capacitySkillOwner);
    assert(skillCurrentSlots==6&&skillCurrentBuffSlots==6&&skillCurrentBackpack==6&&grown.size()==24);
    assert(grown[6]==native[6]&&grown[7]==native[7]&&grown[12]==native[8]&&ArtiInt(grown[12],0x3c)==7);
    for(int i=8;i<12;++i)assert(ArtiInt(grown[i],0x14)==-1);
    assert(CapacityEightValue(capacitySkillOwner,0x1089658,method)==12&&CapacityEightValue(artiOwner,0x1089658,method)==8);
    assert(MH_CreateHook(reinterpret_cast<void*>(&CapacityBuffUiFixture),reinterpret_cast<void*>(&OnViewerSkillBuffEquipInit),reinterpret_cast<void**>(&originalViewerSkillBuffEquipInit))==MH_OK);
    assert(MH_EnableHook(reinterpret_cast<void*>(&CapacityBuffUiFixture))==MH_OK);
    originalSkillBuffEquipInit=originalViewerSkillBuffEquipInit;
    GameAction volatile buffUi=CapacityBuffUiFixture;void* buffPanel=CapacityPanel(0x28,0x20,2);
    buffUi(buffPanel,method);void* list=nullptr;std::vector<void*> boxes;
    assert(Field(buffPanel,0x28,list)&&ManagedItems(list,boxes)&&boxes.size()==6&&capacityTransactionMethod==method);
    for(int i=0;i<6;++i)assert(ArtiInt(boxes[i],0xa8)==6+i);
    // Buff shrink is blocked independently; normal/backpack can still resize.
    Put(grown[11],0x14,605);data.enabled=ds::SkillCapacity|ds::SkillInventoryCapacity;data.skillSlots=12;data.skillInventorySlots=50;
    assert(ApplySkillCapacity(capacitySkillOwner)&&skillCurrentBuffSlots==6&&data.buffStatus==2&&skillCurrentSlots==12&&skillCurrentBackpack==50);
    auto moved=CapacitySkills(capacitySkillOwner);assert(moved[17]==grown[11]&&moved[18]==native[8]);
    Put(moved[18],0x3c,18);Put(moved[17],0x14,-1);
    assert(ApplySkillCapacity(capacitySkillOwner)&&skillCurrentBuffSlots==2&&CapacitySkills(capacitySkillOwner)[14]==native[8]&&ArtiInt(native[8],0x3c)==14);
    OnSkillBuffEquipInit(buffPanel,method);assert(Field(buffPanel,0x28,list)&&ManagedItems(list,boxes)&&boxes.size()==2&&ArtiInt(boxes[1],0xa8)==13);
    data.enabled|=ds::BuffCapacity;artifactViewerOpen=true;assert(!ApplySkillCapacity(capacitySkillOwner)&&data.buffStatus==3);artifactViewerOpen=false;
    artiAllocationLimit=1;assert(!ApplySkillCapacity(capacitySkillOwner)&&skillCurrentBuffSlots==2);artiAllocationLimit=-1;
    assert(ApplySkillCapacity(capacitySkillOwner)&&skillCurrentBuffSlots==6&&ArtiInt(CapacitySkills(capacitySkillOwner)[18],0x3c)==18);
    // Both maximum backpacks and six buffs fit alongside fifty equipment.
    data.enabled=ds::ArtifactCapacity|ds::ArtifactInventoryCapacity|ds::SkillCapacity|ds::SkillInventoryCapacity|ds::BuffCapacity;
    data.artifactSlots=data.inventorySlots=data.skillSlots=data.skillInventorySlots=50;
    OnSkillInit(capacitySkillOwner,method);artiOwner=ArtiSetup();assert(ApplyArtifactCapacity(artiOwner));
    assert(CapacitySkills(capacitySkillOwner).size()==112&&ArtiItems(artiOwner).size()==107);
    auto skills=CapacitySkills(capacitySkillOwner);auto artifacts=ArtiItems(artiOwner);
    for(int i=0;i<106;++i)Put(skills[i],0x14,200+i);for(int i=0;i<100;++i)Put(artifacts[i],0x14,100+i);
    void* skillPanel=CapacityPanel(),*artifactPanel=CapacityPanel(),*levelPanel=CapacityPanel(0xe0,0xd8),*backPanel=CapacityPanel(0x30,0x20,6);
    OnSkillAllListInit(skillPanel,method);OnArtifactAllListInit(artifactPanel,method);OnSkillLevelListInit(levelPanel,method);
    assert(Field(skillPanel,0x28,list)&&ManagedItems(list,boxes)&&boxes.size()==112&&ArtiInt(boxes[105],0xa8)==105);
    assert(Field(artifactPanel,0x28,list)&&ManagedItems(list,boxes)&&boxes.size()==107&&ArtiInt(boxes[99],0x90)==99);
    OnViewerSkillInventoryInit(backPanel,method);assert(Field(backPanel,0x30,list)&&ManagedItems(list,boxes)&&boxes.size()==50&&ArtiInt(boxes[49],0xa8)==105);
    buffUi(buffPanel,method);assert(Field(buffPanel,0x28,list)&&ManagedItems(list,boxes)&&boxes.size()==6&&ArtiInt(boxes[5],0xa8)==55&&ArtiInt(boxes[5],0xac)==255);
    // Serialization retains the third/sixth buff and last backpack entry.
    void* save=ArtiObject(0xb4);void* skillRecords=ArtiList();void* artifactRecords=ArtiList();
    for(int index:{49,50,51,52,55,105}){void* item=ArtiObject(ArtiItemClass);Put(item,0x10,200+index);Put(item,0x14,index);ArtiAdd(skillRecords,item);}
    for(int index:{49,99}){void* item=ArtiObject(ArtiItemClass);Put(item,0x14,100+index);Put(item,0x18,index);ArtiAdd(artifactRecords,item);}
    // Build 26103048 moved these fields by eight bytes. The old skill-list
    // and capacity offsets now contain dictionary/state references.
    void* hasteDict=ArtiObject(0xb4);void* artifactStates=ArtiObject(0xb4);
    Put(save,0x1cb8,hasteDict);Put(save,0x1ce0,artifactStates);
    Put(save,0x1cb0,skillRecords);Put(save,0x1cd0,artifactRecords);Put(save,0x1cd8,50);StampArtifactSave(artiOwner,save);
    int p=0,a=0,b=0,n=0,sb=0,buffs=0;
    assert(ds::artifacts::LoadLayout(ArtiInt(save,0x1cd8),p,a,b,n,sb,buffs)&&p==50&&b==50&&n==50&&sb==50&&buffs==6);
    RestoreArtifactSavePreview(save);assert(ArtiInt(save,0x1cd8)==50&&SavedLayout(save,p,a,b,n,sb,buffs)&&buffs==6);
    data.enabled=0;OnSkillLoad(capacitySkillOwner,save,method);assert(!skillLoadFailed&&skillCurrentSlots==50&&skillCurrentBuffSlots==6&&skillCurrentBackpack==50&&data.buffStatus==2);
    auto loaded=CapacitySkills(capacitySkillOwner);for(int i:{49,50,51,52,55,105})assert(ArtiInt(loaded[i],0x14)==200+i);
    artiOwner=ArtiSetup();OnArtifactLoad(artiOwner,save,method);assert(!artifactLoadFailed&&artifactTracking.backpack==50&&ArtiInt(ArtiItems(artiOwner)[99],0x14)==199);
    void* preserved=nullptr;assert(Field(save,0x1cb8,preserved)&&preserved==hasteDict&&Field(save,0x1ce0,preserved)&&preserved==artifactStates);
    Put(loaded[49],0x14,-1);Put(loaded[105],0x14,-1);assert(ApplySkillCapacity(capacitySkillOwner));
    assert(skillCurrentSlots==6&&skillCurrentBuffSlots==6&&skillCurrentBackpack==6&&ArtiInt(CapacitySkills(capacitySkillOwner)[11],0x14)==255);
    loaded=CapacitySkills(capacitySkillOwner);Put(loaded[8],0x14,-1);Put(loaded[11],0x14,-1);assert(ApplySkillCapacity(capacitySkillOwner));
    assert(skillCurrentBuffSlots==2&&ArtiInt(CapacitySkills(capacitySkillOwner)[6],0x14)==250&&ArtiInt(CapacitySkills(capacitySkillOwner)[7],0x14)==251);
    Put(ArtiItems(artiOwner)[49],0x14,-1);Put(ArtiItems(artiOwner)[99],0x14,-1);assert(ApplyArtifactCapacity(artiOwner)&&artifactTracking.backpack==6);
    // Old two-buff saves expand after indexed restoration, moving their
    // backpack correctly; no reinterpretation of old backpack slots as buffs.
    Put(save,0x1cd8,ds::artifacts::SaveLayout(7,6,6,6,6));skillRecords=ArtiList();
    for(int at:{6,7,8}){void* item=ArtiObject(ArtiItemClass);Put(item,0x10,900+at);Put(item,0x14,at);ArtiAdd(skillRecords,item);}Put(save,0x1cb0,skillRecords);
    artifactSaveHint.Reset();data.enabled=ds::BuffCapacity;OnSkillLoad(capacitySkillOwner,save,method);
    assert(!skillLoadFailed&&skillCurrentBuffSlots==6&&ArtiInt(CapacitySkills(capacitySkillOwner)[6],0x14)==906&&ArtiInt(CapacitySkills(capacitySkillOwner)[12],0x14)==908);
    for(int tag:{ds::artifacts::FullLayoutTag,ds::artifacts::SaveLayout(51,6,50,50,50,6),ds::artifacts::SaveLayout(50,6,51,50,50,6),ds::artifacts::SaveLayout(50,6,50,51,50,6),ds::artifacts::SaveLayout(50,6,50,50,51,6),ds::artifacts::ExtendedLayoutTag|7|(31<<6)|(6<<11)|(6<<17)}) {
        Put(save,0x1cd8,tag);int calls=capacityLoadCalls;OnSkillLoad(capacitySkillOwner,save,method);assert(skillLoadFailed&&capacityLoadCalls==calls);
    }
    assert(MH_DisableHook(reinterpret_cast<void*>(&CapacityBuffUiFixture))==MH_OK&&MH_RemoveHook(reinterpret_cast<void*>(&CapacityBuffUiFixture))==MH_OK);
    data.enabled=0;OnSkillInit(capacitySkillOwner,method);
    std::cout<<"PASS: executed buff full/swap/acquisition operands at 2/6 buffs and 6..50 backpacks; buff-only and independent shrink, stable objects/index links, six-box HUD/viewer callbacks, 107 artifact/112 skill UI pools, four 6-bit save counts + buff metadata, occupied maximum resume and old two-buff migration, invalid tags; isolated fixtures only\n";
}
