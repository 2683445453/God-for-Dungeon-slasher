// The shared constant-return methods are folded together by IL2CPP. Scope
// overrides to the exact live manager object, leaving every other alias alone.
RewardGain originalCapacitySix=nullptr,originalCapacityEight=nullptr,originalArtifactHaveCount=nullptr;
using CheckCapacity=bool(__fastcall*)(void*,const void*);
CheckCapacity originalArtifactBackFull=nullptr,originalArtifactAllFull=nullptr;
Instance getSkillManager=nullptr;
ArtifactCtor constructSkill=nullptr;
GameAction skillChanged=nullptr,originalSkillUpdate=nullptr,originalSkillInit=nullptr,originalSkillDestroy=nullptr;
GameAction originalSkillEquipInit=nullptr,originalViewerSkillEquipInit=nullptr;
GameAction originalSkillBuffEquipInit=nullptr,originalViewerSkillBuffEquipInit=nullptr;
ArtifactLoad originalSkillLoad=nullptr;
void* skillOwner=nullptr;
bool skillLoading=false;int skillLoadSlots=6,skillLoadBackpack=6,skillLoadBuffSlots=2;
GameAction originalArtifactAllListInit=nullptr,originalArtifactSellListInit=nullptr,originalSkillAllListInit=nullptr,originalSkillSellListInit=nullptr,originalSkillLevelListInit=nullptr;
GameAction originalSkillInventoryInit=nullptr,originalViewerSkillInventoryInit=nullptr;
InventoryPool checkArtifactAllPool=nullptr,checkSkillAllPool=nullptr,checkSkillInventoryPool=nullptr;

int ArtifactBackpack(void* self) {return artifactTracking.owner==self?artifactTracking.backpack:6;}
int CapacitySixValue(void* self,uintptr_t caller,const void* method) {
    if(self && artifactTracking.owner==self) {
        for(const auto& site:ds::ArtifactInventoryCalls)if(caller==site.rva+5)return artifactTracking.backpack;
    }
    if(self && skillOwner==self) {
        for(const auto& site:ds::SkillNormalCalls)if(caller==site.rva+5)return skillCurrentSlots;
        for(const auto& site:ds::SkillInventoryCalls)if(caller==site.rva+5)return skillCurrentBackpack;
    }
    return originalCapacitySix(self,method);
}
int __fastcall OnCapacitySix(void* self,const void* method){return CapacitySixValue(self,reinterpret_cast<uintptr_t>(_ReturnAddress())-gameBase,method);}
int CapacityEightValue(void* self,uintptr_t caller,const void* method) {
    if(self&&skillOwner==self)for(const auto& site:ds::SkillEquipmentCalls)if(caller==site.rva+5)return skillCurrentSlots+skillCurrentBuffSlots;
    return originalCapacityEight(self,method);
}
int __fastcall OnCapacityEight(void* self,const void* method){return CapacityEightValue(self,reinterpret_cast<uintptr_t>(_ReturnAddress())-gameBase,method);}
int __fastcall OnArtifactHaveCount(void* self,const void* method) {
    int allowed=0;return self&&artifactTracking.owner==self&&Field(self,0xb0,allowed)?allowed+artifactTracking.backpack:originalArtifactHaveCount(self,method);
}
bool ArtifactFull(void* self,bool total) {
    void* list=nullptr;int physical=0,allowed=0;std::vector<void*> items;std::vector<bool> occupied;
    if(!ArtifactSnapshot(self,list,physical,allowed,items,occupied))return true;
    int count=0;for(int i=total?0:physical;i<int(items.size());++i)count+=occupied[i]?1:0;
    return count>=(total?allowed:0)+ArtifactBackpack(self);
}
bool __fastcall OnArtifactBackFull(void* self,const void* method){return artifactTracking.owner==self?ArtifactFull(self,false):originalArtifactBackFull(self,method);}
bool __fastcall OnArtifactAllFull(void* self,const void* method){return artifactTracking.owner==self?ArtifactFull(self,true):originalArtifactAllFull(self,method);}

bool SetSkillNativeLimit(int count,int backpack=6,int buffs=2) {
    count=std::clamp(count,6,ds::MaxSkillSlots);backpack=std::clamp(backpack,6,ds::MaxInventorySlots);
    if(buffs!=ds::NativeBuffSlots&&buffs!=ds::MaxBuffSlots)return false;
    if(!PatchCapacityOperands(ds::SkillCapacityPatches,skillCurrentSlots-6,skillCurrentBackpack-6,count-6,backpack-6,skillCurrentBuffSlots-2,buffs-2))return false;
    skillCurrentSlots=count;skillCurrentBackpack=backpack;skillCurrentBuffSlots=buffs;return true;
}
bool SkillSnapshot(void* owner,void*& list,std::vector<void*>& items,std::vector<bool>& occupied) {
    if(!Field(owner,0x40,list)||!ManagedItems(list,items)||int(items.size())<skillCurrentSlots+skillCurrentBuffSlots+skillCurrentBackpack)return false;
    occupied.clear();for(void* item:items){int id=-1;if(!Field(item,0x14,id))return false;occupied.push_back(id>=0);}return true;
}
bool ResizeSkills(void* owner,int count,int backpack=6,int buffs=2) {
    void* list=nullptr;std::vector<void*> items;std::vector<bool> occupied;
    if(!SkillSnapshot(owner,list,items,occupied)||count<6||count>ds::MaxSkillSlots||backpack<6||backpack>ds::MaxInventorySlots||(buffs!=2&&buffs!=6))return false;
    if(count==skillCurrentSlots&&backpack==skillCurrentBackpack&&buffs==skillCurrentBuffSlots)return true;
    if(count<skillCurrentSlots)for(int i=count;i<skillCurrentSlots;++i)if(occupied[i])return false;
    if(buffs<skillCurrentBuffSlots)for(int i=buffs;i<skillCurrentBuffSlots;++i)if(occupied[skillCurrentSlots+i])return false;
    if(backpack<skillCurrentBackpack)for(int i=skillCurrentSlots+skillCurrentBuffSlots+backpack;i<int(items.size());++i)if(occupied[i])return false;
    ManagedRoot next(artifactApi,artifactApi.newObject(artifactApi.objectClass(list)));
    if(!next.Get()||!artifactApi.Call(next.Get(),".ctor",0))return false;
    int size=count+buffs+backpack+6;void* capacity[]={&size};
    if(size>ds::artifacts::MaxItems||!artifactApi.Call(next.Get(),"set_Capacity",1,capacity))return false;
    std::vector<void*> ordered;std::vector<int> sources,remapped(items.size(),-1);
    for(int i=0;i<size;++i) {
        int source=-1;
        if(i<count)source=i<skillCurrentSlots?i:-1;
        else if(i<count+buffs)source=i-count<skillCurrentBuffSlots?skillCurrentSlots+i-count:-1;
        else source=i-count-buffs+skillCurrentSlots+skillCurrentBuffSlots;
        if(source>=int(items.size()))source=-1;
        ManagedRoot root(artifactApi,source>=0?items[source]:artifactApi.newObject(artifactApi.objectClass(items.front())));
        if(!root.Get())return false;
        if(source<0)constructSkill(root.Get(),i,nullptr);
        void* add[]={root.Get()};if(!artifactApi.Call(next.Get(),"Add",1,add))return false;
        ordered.push_back(root.Get());
        sources.push_back(source);
        if(source>=0)remapped[source]=i;
    }
    if(!SetSkillNativeLimit(count,backpack,buffs))return false;
    if(!WriteField(owner,0x40,next.Get()))return false;
    artifactApi.barrier(owner,reinterpret_cast<void**>(static_cast<unsigned char*>(owner)+0x40),next.Get());
    for(int i=0;i<size;++i){int newIndex=-1;if(sources[i]>=0&&Field(ordered[i],0x3c,newIndex)&&newIndex>=0&&newIndex<int(remapped.size())&&remapped[newIndex]>=0)WriteField(ordered[i],0x3c,remapped[newIndex]);WriteField(ordered[i],0x10,i);}
    return true;
}
void SkillStatus(int value){if(shared)ds::Set(shared->skillStatus,value);}
bool ApplySkillCapacity(void* owner) {
    if(skillLoading)return false;
    if(skillLoadFailed){SkillStatus(-2);return false;}
    bool valid=false;if(!Field(owner,0x6c,valid)||!valid)return false;
    void* list=nullptr;std::vector<void*> items;std::vector<bool> occupied;
    if(!SkillSnapshot(owner,list,items,occupied)){SkillStatus(-1);return false;}
    skillOwner=owner;
    int requested=ds::Active(shared,ds::SkillCapacity)?std::clamp(int(ds::Read(shared->skillSlots)),6,ds::MaxSkillSlots):6;
    int backpack=ds::Active(shared,ds::SkillInventoryCapacity)?std::clamp(int(ds::Read(shared->skillInventorySlots)),6,ds::MaxInventorySlots):6;
    int buffs=ds::Active(shared,ds::BuffCapacity)?ds::MaxBuffSlots:ds::NativeBuffSlots;
    int equipped=0;for(int i=0;i<skillCurrentSlots;++i)equipped+=occupied[i]?1:0;
    int buffEquipped=0;for(int i=0;i<skillCurrentBuffSlots;++i)buffEquipped+=occupied[skillCurrentSlots+i]?1:0;
    int stored=0;for(int i=skillCurrentSlots+skillCurrentBuffSlots;i<int(items.size());++i)stored+=occupied[i]?1:0;
    bool equipBlocked=false,backBlocked=false,buffBlocked=false;
    if(requested<skillCurrentSlots)for(int i=requested;i<skillCurrentSlots;++i)equipBlocked|=occupied[i];
    if(buffs<skillCurrentBuffSlots)for(int i=buffs;i<skillCurrentBuffSlots;++i)buffBlocked|=occupied[skillCurrentSlots+i];
    if(backpack<skillCurrentBackpack)for(int i=skillCurrentSlots+skillCurrentBuffSlots+backpack;i<int(items.size());++i)backBlocked|=occupied[i];
    if(equipBlocked)requested=skillCurrentSlots;if(backBlocked)backpack=skillCurrentBackpack;if(buffBlocked)buffs=skillCurrentBuffSlots;
    if(shared){ds::Set(shared->skillAllowed,skillCurrentSlots);ds::Set(shared->skillEquipped,equipped);ds::Set(shared->skillInventoryAllowed,skillCurrentBackpack);ds::Set(shared->skillInventoryOccupied,stored);ds::Set(shared->skillInventoryStatus,backBlocked?2:(skillUiFailed?-1:(skillCurrentBackpack>6?1:0)));ds::Set(shared->buffAllowed,skillCurrentBuffSlots);ds::Set(shared->buffEquipped,buffEquipped);ds::Set(shared->buffStatus,buffBlocked?2:(skillUiFailed?-1:(skillCurrentBuffSlots>2?1:0)));}
    if(requested==skillCurrentSlots&&backpack==skillCurrentBackpack&&buffs==skillCurrentBuffSlots){SkillStatus(equipBlocked?2:(skillUiFailed?-1:(requested>6?1:0)));return false;}
    if(artifactViewerOpen||artifactPopupOpen||ExistingArtifactUiOpen(true)){SkillStatus(3);if(shared){ds::Set(shared->skillInventoryStatus,3);ds::Set(shared->buffStatus,3);}return false;}
    if(!ResizeSkills(owner,requested,backpack,buffs)){SkillStatus(-1);if(shared){ds::Set(shared->skillInventoryStatus,-1);ds::Set(shared->buffStatus,-1);}return false;}
    if(shared){ds::Set(shared->skillAllowed,skillCurrentSlots);ds::Set(shared->skillInventoryAllowed,skillCurrentBackpack);ds::Set(shared->skillInventoryStatus,backBlocked?2:(backpack>6?1:0));ds::Set(shared->buffAllowed,skillCurrentBuffSlots);ds::Set(shared->buffStatus,buffBlocked?2:(buffs>2?1:0));}
    SkillStatus(equipBlocked?2:(requested>6?1:0));return true;
}
void ClearSkillCooldowns(void* self) {
    if(ds::Active(shared,ds::NoCooldown) && encodeCooldownFloat) {
        void* list=nullptr;std::vector<void*> items;ObscuredFloatBytes zero{};
        if(!Field(self,0x40,list) || !ManagedItems(list,items) ||
           encodeCooldownFloat(&zero,0.0f,nullptr)!=&zero)return;
        // Equipped ordinary + buff slots only. Keep animation/delay flags and
        // effect durations; the native trigger still checks character rules.
        const size_t count=std::min(items.size(),size_t(skillCurrentSlots+skillCurrentBuffSlots));
        for(size_t i=0;i<count;++i) {
            int id=-1;void* skill=nullptr;
            if(Field(items[i],0x14,id) && id>=0 && Field(items[i],0x20,skill) && skill)
                WriteField(skill,0x70,zero);
        }
    }
}
void __fastcall OnSkillUpdate(void* self,const void* method) {
    bool changed=ApplySkillCapacity(self);originalSkillUpdate(self,method);if(changed&&skillChanged)skillChanged(self,nullptr);
    ClearSkillCooldowns(self);
}
void __fastcall OnSkillInit(void* self,const void* method) {
    int count=skillLoading?skillLoadSlots:(ds::Active(shared,ds::SkillCapacity)?std::clamp(int(ds::Read(shared->skillSlots)),6,ds::MaxSkillSlots):6);
    int backpack=skillLoading?skillLoadBackpack:(ds::Active(shared,ds::SkillInventoryCapacity)?std::clamp(int(ds::Read(shared->skillInventorySlots)),6,ds::MaxInventorySlots):6);
    int buffs=skillLoading?skillLoadBuffSlots:(ds::Active(shared,ds::BuffCapacity)?ds::MaxBuffSlots:ds::NativeBuffSlots);
    // Native Init clears the old list and constructs all entries. Its count
    // operand grows by all three deltas, retaining the six reserve entries.
    if(!SetSkillNativeLimit(count,backpack,buffs)){skillLoadFailed=true;SkillStatus(-2);return;}
    skillOwner=self;originalSkillInit(self,method);
    if(!skillLoading){skillLoadFailed=false;skillUiFailed=false;SkillStatus(count>6?1:0);}
}
void __fastcall OnSkillDestroy(void* self,const void* method) {
    originalSkillDestroy(self,method);
    if(skillOwner==self){SetSkillNativeLimit(6);skillOwner=nullptr;skillLoadFailed=false;SkillStatus(0);}
}
bool SavedLayout(void* container,int& physical,int& native,int& backpack,int& skills,int& skillBackpack,int& buffs) {
    int saved=0;if(!Field(container,0x1cd8,saved))return false;
    if(ds::artifacts::LoadLayout(saved,physical,native,backpack,skills,skillBackpack,buffs))return true;
    if(artifactSaveHint.Get()==container && ds::artifacts::LoadLayout(artifactSaveHintValue,physical,native,backpack,skills,skillBackpack,buffs)&&physical==saved)return true;
    return false;
}
bool SavedLayout(void* container,int& physical,int& native,int& backpack,int& skills,int& skillBackpack){int buffs;return SavedLayout(container,physical,native,backpack,skills,skillBackpack,buffs);}
bool SavedLayout(void* container,int& physical,int& native,int& backpack,int& skills){int skillBackpack;return SavedLayout(container,physical,native,backpack,skills,skillBackpack);}
void __fastcall OnSkillLoad(void* self,void* container,const void* method) {
    int physical=7,native=6,backpack=6,skills=6,skillBackpack=6,buffs=2,saved=0;
    bool tagged=SavedLayout(container,physical,native,backpack,skills,skillBackpack,buffs);
    if(!Field(container,0x1cd8,saved)||(!tagged&&ds::artifacts::Tagged(saved))){skillLoadFailed=true;SkillStatus(-2);return;}
    void* records=nullptr;std::vector<void*> items;
    if(!Field(container,0x1cb0,records)||!ManagedItems(records,items)){skillLoadFailed=true;SkillStatus(-2);return;}
    for(void* item:items){int index=-1;if(!Field(item,0x14,index)||index<0||index>=skills+buffs+skillBackpack+6){skillLoadFailed=true;SkillStatus(-2);return;}}
    skillLoadSlots=skills;skillLoadBackpack=skillBackpack;skillLoadBuffSlots=buffs;skillLoading=true;skillLoadFailed=false;
    originalSkillLoad(self,container,method);skillLoading=false;
    if(skillLoadFailed)return;
    skillOwner=self;if(ApplySkillCapacity(self)&&skillChanged)skillChanged(self,nullptr);
}
bool EnsureSkillBoxes(void* self) {
    void* list=nullptr;void* parent=nullptr;
    if(!Field(self,0x28,list)||!Field(self,0x20,parent))return false;
    bool ok=ResizeArtifactBoxes(list,parent,skillCurrentSlots,6,true);if(!ok)skillUiFailed=true;SkillStatus(skillUiFailed?-1:(skillCurrentSlots>6?1:0));return ok;
}
void __fastcall OnSkillEquipInit(void* self,const void* method){if(EnsureSkillBoxes(self))originalSkillEquipInit(self,method);}
void __fastcall OnViewerSkillEquipInit(void* self,const void* method){if(EnsureSkillBoxes(self))originalViewerSkillEquipInit(self,method);}
bool EnsureBuffBoxes(void* self) {
    void* list=nullptr;void* parent=nullptr;
    bool ok=Field(self,0x28,list)&&Field(self,0x20,parent)&&ResizeArtifactBoxes(list,parent,skillCurrentBuffSlots,2,true);
    if(!ok)skillUiFailed=true;
    if(shared)ds::Set(shared->buffStatus,ok?(skillCurrentBuffSlots>2?1:0):-1);
    return ok;
}
void __fastcall OnSkillBuffEquipInit(void* self,const void* method){if(EnsureBuffBoxes(self))originalSkillBuffEquipInit(self,method);}
void __fastcall OnViewerSkillBuffEquipInit(void* self,const void* method){if(EnsureBuffBoxes(self))originalViewerSkillBuffEquipInit(self,method);}

// These lists are also refreshed synchronously by acquisition and upgrade
// events. Their prefab pools do not grow in Init. An indexed exception here
// aborts native code before pickup/shop callbacks or material Init executes.
bool EnsureAllBoxes(void* self,bool skills) {
    void* owner=skills?skillOwner:artifactTracking.owner;void* data=nullptr;
    std::vector<void*> items;
    if(!owner||!Field(owner,skills?0x40:0xa0,data)||!ManagedItems(data,items))return true;
    if((skills?skillCurrentSlots==6&&skillCurrentBackpack==6&&skillCurrentBuffSlots==2:!artifactTracking.expanded&&artifactTracking.backpack==6)) {
        void* list=nullptr;void* parent=nullptr;
        return Field(self,0x28,list)&&Field(self,0x20,parent)&&ResizeArtifactBoxes(list,parent,20,20,skills);
    }
    auto pool=skills?checkSkillAllPool:checkArtifactAllPool;if(!pool)return false;
    pool(self,int(items.size()),nullptr);
    void* list=nullptr;void* parent=nullptr;
    return Field(self,0x28,list)&&Field(self,0x20,parent)&&ResizeArtifactBoxes(list,parent,int(items.size()),20,skills);
}
void __fastcall OnArtifactAllListInit(void* self,const void* method){if(EnsureAllBoxes(self,false))originalArtifactAllListInit(self,method);else ArtifactUiResult(false);}
void __fastcall OnArtifactSellListInit(void* self,const void* method){if(EnsureAllBoxes(self,false))originalArtifactSellListInit(self,method);else ArtifactUiResult(false);}
void __fastcall OnSkillAllListInit(void* self,const void* method){if(EnsureAllBoxes(self,true))originalSkillAllListInit(self,method);else {skillUiFailed=true;SkillStatus(-1);}}
void __fastcall OnSkillSellListInit(void* self,const void* method){if(EnsureAllBoxes(self,true))originalSkillSellListInit(self,method);else {skillUiFailed=true;SkillStatus(-1);}}
void __fastcall OnSkillLevelListInit(void* self,const void* method) {
    {
        void* list=nullptr;void* parent=nullptr;
        if(!Field(self,0xe0,list)||!Field(self,0xd8,parent)||!ResizeArtifactBoxes(list,parent,skillCurrentSlots+skillCurrentBuffSlots+skillCurrentBackpack+6,20,true)){skillUiFailed=true;SkillStatus(-1);return;}
    }
    originalSkillLevelListInit(self,method);
}
bool EnsureSkillInventoryBoxes(void* self) {
    if(!checkSkillInventoryPool)return false;
    checkSkillInventoryPool(self,skillCurrentBackpack,nullptr);
    void* list=nullptr;void* parent=nullptr;
    return Field(self,0x30,list)&&Field(self,0x20,parent)&&ResizeArtifactBoxes(list,parent,skillCurrentBackpack,6,true);
}
void FitSkillInventoryBoxes(void* self) {
    void* list=nullptr;void* parent=nullptr;std::vector<void*> boxes;
    if(Field(self,0x30,list)&&Field(self,0x20,parent)&&ManagedItems(list,boxes)&&FitArtifactLayout(parent,int(boxes.size()),6))ApplyArtifactVisuals(boxes,parent,int(boxes.size()),6);
}
void __fastcall OnSkillInventoryInit(void* self,const void* method){if(EnsureSkillInventoryBoxes(self)){originalSkillInventoryInit(self,method);FitSkillInventoryBoxes(self);}else {skillUiFailed=true;SkillStatus(-1);}}
void __fastcall OnViewerSkillInventoryInit(void* self,const void* method){if(EnsureSkillInventoryBoxes(self)){originalViewerSkillInventoryInit(self,method);FitSkillInventoryBoxes(self);}else {skillUiFailed=true;SkillStatus(-1);}}
