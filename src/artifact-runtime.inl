ManagedRuntime artifactApi;
int skillCurrentSlots=6,skillCurrentBackpack=6,artifactCurrentBackpack=6;
int skillCurrentBuffSlots=ds::NativeBuffSlots;
bool skillLoadFailed=false,skillUiFailed=false;
using ArtifactLoad=void(__fastcall*)(void*,void*,const void*);
using ArtifactList=void*(__fastcall*)(void*,const void*);
using ArtifactCtor=void(__fastcall*)(void*,int,const void*);
using UnityClone=void*(__fastcall*)(void*,void*,bool,const void*);
using UnityComponent=void*(__fastcall*)(void*,const void*);
using UnityTypeComponent=void*(__fastcall*)(void*,void*,const void*);
using UnityDestroy=void(__fastcall*)(void*,const void*);
using AutoSave=void(__fastcall*)(void*,void*,const void*);
GameAction originalArtifactUpdate=nullptr,originalArtifactSet=nullptr,originalArtifactReset=nullptr,originalArtifactDestroy=nullptr;
ArtifactLoad originalArtifactLoad=nullptr;
GameAction originalEquipValidate=nullptr,originalEquipInit=nullptr,originalViewerEquipInit=nullptr,originalArtifactPopupEnable=nullptr,originalArtifactPopupDisable=nullptr;
GameAction originalCharacterEnable=nullptr,originalCharacterDisable=nullptr,originalLoadPreview=nullptr;
GameAction originalInventoryInit=nullptr,originalViewerInventoryInit=nullptr;
using InventoryPool=void(__fastcall*)(void*,int,const void*);
InventoryPool checkArtifactInventoryPool=nullptr;
ArtifactList originalArtifactSaveStates=nullptr;
AutoSave originalAutoSave=nullptr;
Instance getArtifactManager=nullptr;
ArtifactCtor constructArtifact=nullptr;
UnityClone cloneArtifactBox=nullptr;
UnityComponent componentTransform=nullptr,componentGameObject=nullptr;
UnityTypeComponent getTypeComponent=nullptr,addTypeComponent=nullptr;
UnityDestroy destroyArtifactObject=nullptr;

struct ArtifactTracking {void* owner=nullptr;int nativeAllowed=6,lastAllowed=6;bool expanded=false;int backpack=6;};
ArtifactTracking artifactTracking;
bool artifactBusy=false,artifactLoading=false,artifactPopupOpen=false,artifactViewerOpen=false,artifactUiFailed=false,artifactLoadFailed=false;
ManagedRoot artifactSaveHint;int artifactSaveHintValue=0;
void* artifactPlayUiClass=nullptr;
bool Boxed(void* object,const char* property,void* output,size_t size);
template<size_t N> bool PatchCapacityOperands(const ds::CapacityPatch (&patches)[N],int oldNormal,int oldBack,int normal,int back,int oldBuff=0,int buff=0) {
    if(oldNormal==normal&&oldBack==back&&oldBuff==buff)return true;
    if(!gameBase)return false;
    struct Writable {void* address;size_t width;DWORD protection;};std::vector<Writable> pages;
    auto value=[](const ds::CapacityPatch& p,int n,int b,int f){return p.value+p.sign*(p.basis==1?b:p.basis==2?n+b:n)+p.buffSign*f;};
    for(const auto& p:patches) {
        auto* address=reinterpret_cast<unsigned char*>(gameBase+p.rva+p.offset);int actual=0;
        if(p.width==1){signed char byte=0;if(!Field(address,0,byte))return false;actual=byte;}
        else if(!Field(address,0,actual))return false;
        if(actual!=value(p,oldNormal,oldBack,oldBuff))return false;
        const int next=value(p,normal,back,buff);
        if(p.width==1&&(next<SCHAR_MIN||next>SCHAR_MAX))return false;
    }
    for(const auto& p:patches) {
        void* address=reinterpret_cast<void*>(gameBase+p.rva+p.offset);DWORD protection=0;
        if(!VirtualProtect(address,p.width,PAGE_EXECUTE_READWRITE,&protection)) {
            for(auto it=pages.rbegin();it!=pages.rend();++it){DWORD ignored;VirtualProtect(it->address,it->width,it->protection,&ignored);}return false;
        }
        pages.push_back({address,p.width,protection});
    }
    for(size_t i=0;i<N;++i){int next=value(patches[i],normal,back,buff);memcpy(pages[i].address,&next,pages[i].width);FlushInstructionCache(GetCurrentProcess(),pages[i].address,pages[i].width);}
    for(auto it=pages.rbegin();it!=pages.rend();++it){DWORD ignored;VirtualProtect(it->address,it->width,it->protection,&ignored);}return true;
}
bool SetArtifactNativeBackpack(int count) {
    if(!PatchCapacityOperands(ds::ArtifactCapacityPatches,0,artifactCurrentBackpack-6,0,count-6))return false;
    artifactCurrentBackpack=count;return true;
}
void ArtifactStatus(int status){if(shared)ds::Set(shared->artifactStatus,status);}
void ArtifactUiResult(bool ok) {
    artifactUiFailed=!ok;
    ArtifactStatus(artifactLoadFailed?-2:(!ok?-1:(artifactTracking.expanded?1:0)));
}
bool ManagedItems(void* list,std::vector<void*>& items) {
    void* array=nullptr;int size=0;uintptr_t length=0;
    if(!Field(list,0x10,array)||!Field(list,0x18,size)||size<0||size>ds::artifacts::MaxItems||!Field(array,0x18,length)||size>int(length))return false;
    items.clear();items.reserve(size);
    for(int i=0;i<size;++i) {void* object=nullptr;if(!Field(array,0x20+size_t(i)*8,object)||!object)return false;items.push_back(object);}
    return true;
}
bool ArtifactSnapshot(void* owner,void*& list,int& physical,int& allowed,std::vector<void*>& items,std::vector<bool>& occupied) {
    if(!Field(owner,0xa0,list)||!Field(owner,0xac,physical)||!Field(owner,0xb0,allowed)||!ManagedItems(list,items)||physical<7||physical>ds::MaxArtifactSlots||int(items.size())<physical+6)return false;
    occupied.clear();for(void* object:items) {int id=-1;if(!Field(object,0x14,id))return false;occupied.push_back(id>=0);}
    return true;
}
void ObserveArtifactManager(void* owner,int allowed) {
    if(artifactTracking.owner!=owner)artifactTracking={owner,std::clamp(allowed,6,7),std::clamp(allowed,6,7),false};
    else if(allowed!=artifactTracking.lastAllowed && (allowed==6||allowed==7))artifactTracking.nativeAllowed=allowed;
}
bool ExistingArtifactUiOpen(bool skills=false) {
    // Also catch windows that were already open when the trainer attached.
    // Play_UI retains the native character and artifact-selection popups.
    if(!artifactPlayUiClass)artifactPlayUiClass=artifactApi.Class("","Play_UI");
    void* ui=nullptr;if(!artifactApi.Static(artifactPlayUiClass,"get_Instance",&ui)||!ui||!componentGameObject)return false;
    for(size_t offset:{size_t(0x138),size_t(0x3d8),size_t(0x140),size_t(0x148),size_t(0x228)}) {
        if(!skills && offset!=0x138 && offset!=0x3d8)continue;
        void* popup=nullptr;uintptr_t native=0;bool active=false;
        if(Field(ui,offset,popup)&&popup&&Field(popup,0x10,native)&&native &&
           Boxed(componentGameObject(popup,nullptr),"get_activeInHierarchy",&active,1)&&active)return true;
    }
    return false;
}
bool ResizeArtifacts(void* owner,int requested,int backpack=0) {
    void* oldList=nullptr;int physical=0,allowed=0;std::vector<void*> items;std::vector<bool> occupied;
    if(!ArtifactSnapshot(owner,oldList,physical,allowed,items,occupied))return false;
    const auto plan=ds::artifacts::ResizePlan(occupied,physical,requested,backpack);
    if(!plan.target || plan.blocked)return false;
    if(plan.target==physical && plan.order.size()==items.size())return !backpack||SetArtifactNativeBackpack(backpack);
    if(!artifactApi.newObject || !constructArtifact || !artifactApi.barrier)return false;
    ManagedRoot next(artifactApi,artifactApi.newObject(artifactApi.objectClass(oldList)));
    if(!next.Get() || !artifactApi.Call(next.Get(),".ctor",0))return false;
    int count=int(plan.order.size());void* capacityArgs[]={&count};
    if(!artifactApi.Call(next.Get(),"set_Capacity",1,capacityArgs))return false;
    std::vector<void*> ordered;ordered.reserve(count);
    for(int source:plan.order) {
        void* object=source>=0?items[source]:artifactApi.newObject(artifactApi.objectClass(items.front()));
        ManagedRoot root(artifactApi,object);if(!root.Get())return false;object=root.Get();
        if(source<0) {
            constructArtifact(object,int(ordered.size()),nullptr);
            // Match ArtifactInven.Init(true)'s empty fields without its global
            // SetArtifact/ArtifactChanged callbacks during list construction.
            const int empty=-1;
            if(!WriteField(object,0x14,empty))return false;
        }
        void* addArgs[]={object};if(!artifactApi.Call(next.Get(),"Add",1,addArgs))return false;
        ordered.push_back(object);
    }
    // All allocation and validation completes before publishing. Preserve the
    // original ArtifactInven objects, including rolls, counters and callbacks.
    if(backpack&&!SetArtifactNativeBackpack(backpack))return false;
    if(!WriteField(owner,0xa0,next.Get()))return false;
    artifactApi.barrier(owner,reinterpret_cast<void**>(static_cast<unsigned char*>(owner)+0xa0),next.Get());
    for(int i=0;i<count;++i)WriteField(ordered[i],0x18,i);
    WriteField(owner,0xac,plan.target);return true;
}
bool ApplyArtifactCapacity(void* owner) {
    if(artifactBusy || artifactLoading)return false;
    if(artifactLoadFailed){ArtifactStatus(-2);return false;}
    bool valid=false;if(!Field(owner,0x20,valid)||!valid)return false;
    void* list=nullptr;int physical=0,allowed=0;std::vector<void*> items;std::vector<bool> occupied;
    if(!ArtifactSnapshot(owner,list,physical,allowed,items,occupied))return false;
    ObserveArtifactManager(owner,allowed);
    const bool active=ds::Active(shared,ds::ArtifactCapacity);
    int requested=active?std::clamp(int(ds::Read(shared->artifactSlots)),ds::MinArtifactSlots,ds::MaxArtifactSlots):artifactTracking.nativeAllowed;
    int target=std::max(7,requested);
    int wantedBack=ds::Active(shared,ds::ArtifactInventoryCapacity)?std::clamp(int(ds::Read(shared->inventorySlots)),6,ds::MaxInventorySlots):6;
    int backCount=0;for(int i=physical;i<int(items.size());++i)backCount+=occupied[i]?1:0;
    bool backBlocked=false;for(int i=physical+wantedBack;i<int(items.size());++i)backBlocked|=occupied[i];
    int backStatus=backBlocked && wantedBack<artifactTracking.backpack?2:0;
    if(backStatus==2)wantedBack=artifactTracking.backpack;
    int equipped=0;for(int i=0;i<physical;++i)equipped+=occupied[i]?1:0;
    if(shared){ds::Set(shared->artifactPhysicalSlots,physical);ds::Set(shared->artifactAllowed,allowed);ds::Set(shared->artifactEquipped,equipped);ds::Set(shared->inventoryAllowed,artifactTracking.backpack);ds::Set(shared->inventoryOccupied,backCount);ds::Set(shared->inventoryStatus,backStatus?backStatus:(artifactTracking.backpack>6?1:0));}
    const bool changed=target!=physical || requested!=allowed || wantedBack!=artifactTracking.backpack;
    if(!changed){ArtifactStatus(artifactUiFailed?-1:(artifactTracking.expanded?1:0));return false;}
    if(artifactPopupOpen || artifactViewerOpen || ExistingArtifactUiOpen()){ArtifactStatus(3);if(shared)ds::Set(shared->inventoryStatus,3);return false;}
    auto plan=ds::artifacts::ResizePlan(occupied,physical,target);
    if(!plan.target){ArtifactStatus(-1);return false;}
    // Shrinking the limit within the same physical seven-slot layout also
    // waits for the native bonus slot to become empty.
    bool tailOccupied=plan.blocked;
    for(int i=requested;i<physical;++i)tailOccupied=tailOccupied||occupied[i];
    const bool equipBlocked=tailOccupied && requested<allowed;
    if(equipBlocked){
        // An occupied equipment tail holds its own capacity, while an
        // independent backpack change can still proceed without moving it.
        requested=allowed;target=physical;
        if(wantedBack==artifactTracking.backpack){ArtifactStatus(2);return false;}
    }
    artifactBusy=true;
    bool ok=ResizeArtifacts(owner,target,wantedBack);
    if(ok) {
        WriteField(owner,0xb0,requested);artifactTracking.lastAllowed=requested;
        artifactTracking.expanded=target!=7 || requested!=artifactTracking.nativeAllowed;
        artifactTracking.backpack=wantedBack;
        if(shared){ds::Set(shared->artifactPhysicalSlots,target);ds::Set(shared->artifactAllowed,requested);ds::Set(shared->inventoryAllowed,wantedBack);ds::Set(shared->inventoryStatus,backStatus?backStatus:(wantedBack>6?1:0));}
    }
    artifactBusy=false;ArtifactStatus(ok?(equipBlocked?2:(artifactTracking.expanded?1:0)):-1);return ok;
}
void __fastcall OnArtifactUpdate(void* self,const void* method) {
    bool changed=ApplyArtifactCapacity(self);originalArtifactUpdate(self,method);
    if(changed)originalArtifactSet(self,nullptr);
}
void __fastcall OnArtifactSet(void* self,const void* method){ApplyArtifactCapacity(self);originalArtifactSet(self,method);}
void __fastcall OnArtifactReset(void* self,const void* method) {
    // A new run clears every artifact through the original reset first.
    artifactLoading=true;originalArtifactReset(self,method);artifactLoading=false;
    artifactLoadFailed=false;artifactUiFailed=false;
    artifactSaveHint.Reset();artifactSaveHintValue=0;
    SetArtifactNativeBackpack(6);artifactTracking={self,6,6,false};WriteField(self,0xb0,6);
    if(ApplyArtifactCapacity(self))originalArtifactSet(self,nullptr);
}
void __fastcall OnArtifactDestroy(void* self,const void* method) {
    if(artifactTracking.owner==self){SetArtifactNativeBackpack(6);artifactTracking={};ArtifactStatus(0);}
    originalArtifactDestroy(self,method);
}

struct ArtifactLayout {
    ManagedRoot parent,grid,horizontal;
    bool added=false,horizontalEnabled=false;float cell[2]{},spacing[2]{};int constraint=0,columns=0,axis=0,corner=0,alignment=0;
};
std::vector<ArtifactLayout> artifactLayouts;
bool Boxed(void* object,const char* property,void* output,size_t size) {
    void* value=nullptr;if(!artifactApi.Call(object,property,0,nullptr,&value))return false;
    __try {memcpy(output,static_cast<unsigned char*>(value)+0x10,size);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
template<class T> bool SetProperty(void* object,const char* name,T value) {void* args[]={&value};return artifactApi.Call(object,name,1,args);}
bool SetPair(void* object,const char* name,float x,float y) {float value[]={x,y};void* args[]={value};return artifactApi.Call(object,name,1,args);}
struct ArtifactVisualPart {
    ManagedRoot rect,text;float delta[2]{},position[2]{},font=0,fontMin=0,fontMax=0;
};
struct ArtifactVisual {
    ManagedRoot widget,root;float base=0,factor=1;
    float delta[2]{},position[2]{},anchorMin[2]{},anchorMax[2]{},pivot[2]{};
    std::vector<ArtifactVisualPart> parts;
};
std::vector<ArtifactVisual> artifactVisuals;
ArtifactVisual* FindArtifactVisual(void* widget){for(auto& v:artifactVisuals)if(v.widget.Get()==widget)return &v;return nullptr;}
void ForgetArtifactVisual(void* widget) {artifactVisuals.erase(std::remove_if(artifactVisuals.begin(),artifactVisuals.end(),[widget](const auto& v){return v.widget.Get()==widget;}),artifactVisuals.end());}
bool CaptureArtifactVisual(void* widget,void* root,bool preview=false,void* prototype=nullptr,bool skill=false) {
    if(FindArtifactVisual(widget))return true;
    ArtifactVisual fresh;fresh.widget=ManagedRoot(artifactApi,widget);fresh.root=ManagedRoot(artifactApi,root);
    float rect[4]{};if(!Boxed(root,"get_rect",rect,sizeof(rect)))return false;
    if(!Boxed(root,"get_sizeDelta",fresh.delta,8)||!Boxed(root,"get_anchoredPosition",fresh.position,8)||!Boxed(root,"get_anchorMin",fresh.anchorMin,8)||!Boxed(root,"get_anchorMax",fresh.anchorMax,8)||!Boxed(root,"get_pivot",fresh.pivot,8))return false;
    float normalize=1;
    if(auto* source=FindArtifactVisual(prototype)) {
        fresh.base=source->base;normalize=1/source->factor;
        memcpy(fresh.delta,source->delta,8);memcpy(fresh.position,source->position,8);memcpy(fresh.anchorMin,source->anchorMin,8);memcpy(fresh.anchorMax,source->anchorMax,8);memcpy(fresh.pivot,source->pivot,8);
    }
    else fresh.base=std::max(rect[2],rect[3]);
    std::vector<std::pair<void*,bool>> components;
    if(preview)components.push_back({widget,false});
    else if(skill)for(size_t offset:{size_t(0x28),size_t(0x30),size_t(0x38),size_t(0x40),size_t(0x48),size_t(0x50),size_t(0x58),size_t(0x60),size_t(0x68),size_t(0x70),size_t(0x78),size_t(0x80),size_t(0x88),size_t(0x90)}) {
        void* component=nullptr;if(Field(widget,offset,component)&&component)components.push_back({component,offset==0x88});
    }
    else for(size_t offset:{size_t(0x28),size_t(0x30),size_t(0x38),size_t(0x40),size_t(0x48),size_t(0x50),size_t(0x58),size_t(0x60),size_t(0x68),size_t(0x70),size_t(0x80)}) {
        void* component=nullptr;if(Field(widget,offset,component)&&component)components.push_back({component,offset==0x30||offset==0x68});
    }
    for(auto [component,isText]:components) {
        void* part=componentTransform(component,nullptr);if(!part||part==root)continue;
        // Optional synergy animators can use ordinary Transform objects;
        // keep their native effect transforms outside the UI margin adapter.
        if(!artifactApi.findMethod(artifactApi.objectClass(part),"get_sizeDelta",0))continue;
        bool duplicate=false;for(auto& p:fresh.parts)duplicate|=p.rect.Get()==part;if(duplicate)continue;
        ArtifactVisualPart next;next.rect=ManagedRoot(artifactApi,part);
        if(!Boxed(part,"get_sizeDelta",next.delta,8)||!Boxed(part,"get_anchoredPosition",next.position,8))return false;
        for(int i=0;i<2;++i){next.delta[i]*=normalize;next.position[i]*=normalize;}
        // A preview wrapper has zero serialized size before its native layout
        // runs. Its fixed 90px image provides the correct original baseline.
        if(preview && fresh.base<=0){float imageRect[4]{};if(Boxed(part,"get_rect",imageRect,16))fresh.base=std::max(imageRect[2],imageRect[3])*normalize;}
        if(isText && Boxed(component,"get_fontSize",&next.font,4)&&Boxed(component,"get_fontSizeMin",&next.fontMin,4)&&Boxed(component,"get_fontSizeMax",&next.fontMax,4)) {
            next.text=ManagedRoot(artifactApi,component);next.font*=normalize;next.fontMin*=normalize;next.fontMax*=normalize;
        }
        fresh.parts.push_back(std::move(next));
    }
    if(!std::isfinite(fresh.base)||fresh.base<=0)return false;
    artifactVisuals.push_back(std::move(fresh));return true;
}
bool ApplyArtifactVisuals(const std::vector<void*>& widgets,void* parent,int count,int nativeCount=7) {
    float cell[2]{};
    if(count>nativeCount) {
        void* grid=nullptr;for(auto& r:artifactLayouts)if(r.parent.Get()==parent){grid=r.grid.Get();break;}
        if(!grid||!Boxed(grid,"get_cellSize",cell,8)||cell[0]<=0)return false;
    }
    bool ok=true;
    for(void* widget:widgets)if(auto* record=FindArtifactVisual(widget)) {
        float factor=count>nativeCount?cell[0]/record->base:1;
        if(count<=nativeCount) {
            // Disabling an added grid does not undo its driven RectTransform
            // values. Restore manual prefab positions and original anchors.
            ok=SetPair(record->root.Get(),"set_anchorMin",record->anchorMin[0],record->anchorMin[1])&&ok;
            ok=SetPair(record->root.Get(),"set_anchorMax",record->anchorMax[0],record->anchorMax[1])&&ok;
            ok=SetPair(record->root.Get(),"set_pivot",record->pivot[0],record->pivot[1])&&ok;
            ok=SetPair(record->root.Get(),"set_sizeDelta",record->delta[0],record->delta[1])&&ok;
            ok=SetPair(record->root.Get(),"set_anchoredPosition",record->position[0],record->position[1])&&ok;
        }
        for(auto& p:record->parts) {
            ok=SetPair(p.rect.Get(),"set_sizeDelta",p.delta[0]*factor,p.delta[1]*factor)&&ok;
            ok=SetPair(p.rect.Get(),"set_anchoredPosition",p.position[0]*factor,p.position[1]*factor)&&ok;
            if(void* text=p.text.Get()) {
                ok=SetProperty(text,"set_fontSizeMin",p.fontMin*factor)&&ok;
                ok=SetProperty(text,"set_fontSizeMax",p.fontMax*factor)&&ok;
                ok=SetProperty(text,"set_fontSize",p.font*factor)&&ok;
            }
        }
        record->factor=factor;
    }
    return ok;
}
void* LayoutComponent(void* game,const char* name,bool add=false) {
    void* type=artifactApi.Type("UnityEngine.UI",name);
    return type?(add?addTypeComponent: getTypeComponent)(game,type,nullptr):nullptr;
}
bool FitArtifactLayout(void* parent,int count,int nativeCount=7) {
    if(!parent || !componentGameObject || !getTypeComponent || !addTypeComponent)return false;
    ArtifactLayout* record=nullptr;
    for(auto& existing:artifactLayouts)if(existing.parent.Get()==parent){record=&existing;break;}
    if(count<=nativeCount && !record)return true;
    if(!record) {
        void* game=componentGameObject(parent,nullptr);if(!game)return false;
        ArtifactLayout fresh;fresh.parent=ManagedRoot(artifactApi,parent);
        void* grid=LayoutComponent(game,"GridLayoutGroup");fresh.added=grid==nullptr;
        if(!grid)grid=LayoutComponent(game,"GridLayoutGroup",true);
        if(!grid)return false;fresh.grid=ManagedRoot(artifactApi,grid);
        if(!fresh.added) {
            if(!Boxed(grid,"get_cellSize",fresh.cell,sizeof(fresh.cell))||!Boxed(grid,"get_spacing",fresh.spacing,sizeof(fresh.spacing))||
               !Boxed(grid,"get_constraint",&fresh.constraint,4)||!Boxed(grid,"get_constraintCount",&fresh.columns,4)||
               !Boxed(grid,"get_startAxis",&fresh.axis,4)||!Boxed(grid,"get_startCorner",&fresh.corner,4)||!Boxed(grid,"get_childAlignment",&fresh.alignment,4))return false;
        }
        if(void* horizontal=LayoutComponent(game,"HorizontalLayoutGroup")) {
            fresh.horizontal=ManagedRoot(artifactApi,horizontal);
            if(!Boxed(horizontal,"get_enabled",&fresh.horizontalEnabled,1))return false;
        }
        artifactLayouts.push_back(std::move(fresh));record=&artifactLayouts.back();
    }
    void* grid=record->grid.Get();if(!grid)return false;
    if(count<=nativeCount) {
        if(record->horizontal.Get())SetProperty(record->horizontal.Get(),"set_enabled",record->horizontalEnabled);
        if(record->added)SetProperty(grid,"set_enabled",false);
        else {SetPair(grid,"set_cellSize",record->cell[0],record->cell[1]);SetPair(grid,"set_spacing",record->spacing[0],record->spacing[1]);SetProperty(grid,"set_constraint",record->constraint);SetProperty(grid,"set_constraintCount",record->columns);SetProperty(grid,"set_startAxis",record->axis);SetProperty(grid,"set_startCorner",record->corner);SetProperty(grid,"set_childAlignment",record->alignment);}
        return true;
    }
    float rect[4]{};if(!Boxed(parent,"get_rect",rect,sizeof(rect)))return false;
    void* padding=nullptr;int left=0,right=0,top=0,bottom=0;
    if(artifactApi.Call(grid,"get_padding",0,nullptr,&padding) && padding) {
        Boxed(padding,"get_left",&left,4);Boxed(padding,"get_right",&right,4);Boxed(padding,"get_top",&top,4);Boxed(padding,"get_bottom",&bottom,4);
    }
    auto fit=ds::artifacts::FitGrid(rect[2]-left-right,rect[3]-top-bottom,count);if(fit.cell<=0)return false;
    if(record->horizontal.Get())SetProperty(record->horizontal.Get(),"set_enabled",false);
    return SetProperty(grid,"set_enabled",true) && SetProperty(grid,"set_constraint",1) && SetProperty(grid,"set_constraintCount",fit.columns) &&
        SetProperty(grid,"set_startAxis",0) && SetProperty(grid,"set_startCorner",0) && SetProperty(grid,"set_childAlignment",4) &&
        SetPair(grid,"set_spacing",fit.spacing,fit.spacing) && SetPair(grid,"set_cellSize",fit.cell,fit.cell);
}
bool ResizeArtifactBoxes(void* list,void* parent,int count,int nativeCount=7,bool skill=false) {
    bool adapted=false;for(auto& layout:artifactLayouts)adapted|=layout.parent.Get()==parent;
    // Some native prefabs include spare boxes. Preserve their complete pool
    // when no expansion has ever been applied to this particular view.
    if(count<=nativeCount && !adapted){if(!skill)ArtifactUiResult(true);return true;}
    std::vector<void*> boxes;if(!ManagedItems(list,boxes)||boxes.empty()||!cloneArtifactBox||!componentGameObject||!destroyArtifactObject)return false;
    bool visual=true;
    // Capture original margins before adding a grid or cloning an already
    // scaled prototype. All cloned values are normalized to that baseline.
    if(count>nativeCount)for(void* box:boxes)visual=CaptureArtifactVisual(box,componentTransform(box,nullptr),false,nullptr,skill)&&visual;
    while(int(boxes.size())<count) {
        ManagedRoot box(artifactApi,cloneArtifactBox(boxes.front(),parent,false,nullptr));if(!box.Get())return false;
        void* args[]={box.Get()};if(!artifactApi.Call(list,"Add",1,args)) {destroyArtifactObject(componentGameObject(box.Get(),nullptr),nullptr);return false;}
        visual=CaptureArtifactVisual(box.Get(),componentTransform(box.Get(),nullptr),false,boxes.front(),skill)&&visual;
        boxes.push_back(box.Get());
    }
    while(int(boxes.size())>count) {
        int index=int(boxes.size())-1;void* args[]={&index};void* game=componentGameObject(boxes.back(),nullptr);
        if(!artifactApi.Call(list,"RemoveAt",1,args))return false;
        bool active=false;void* activeArgs[]={&active};artifactApi.Call(game,"SetActive",1,activeArgs);
        ForgetArtifactVisual(boxes.back());destroyArtifactObject(game,nullptr);boxes.pop_back();
    }
    // A layout failure must not leave cloned boxes with the prototype's old
    // callback. Once the pool is complete, always run the native initializer.
    bool fit=FitArtifactLayout(parent,count,nativeCount);if(fit)visual=ApplyArtifactVisuals(boxes,parent,count,nativeCount)&&visual;
    if(skill){skillUiFailed=!(fit&&visual);if(shared)ds::Set(shared->skillStatus,skillUiFailed?-1:(count>6?1:0));}
    else ArtifactUiResult(fit&&visual);return true;
}
bool EnsureEquipBoxes(void* self) {
    originalEquipValidate(self,nullptr);
    if(!getArtifactManager)return true;
    void* owner=getArtifactManager(nullptr);int count=0;void* list=nullptr;void* parent=nullptr;
    if(!Field(owner,0xac,count)||!Field(self,0x28,list)||!Field(self,0x20,parent))return false;
    return count>=7 && count<=ds::MaxArtifactSlots && ResizeArtifactBoxes(list,parent,count);
}
void __fastcall OnEquipInit(void* self,const void* method){if(EnsureEquipBoxes(self))originalEquipInit(self,method);else ArtifactUiResult(false);}
void __fastcall OnViewerEquipInit(void* self,const void* method){if(EnsureEquipBoxes(self))originalViewerEquipInit(self,method);else ArtifactUiResult(false);}
void __fastcall OnArtifactPopupEnable(void* self,const void* method) {
    artifactPopupOpen=true;
    void* owner=getArtifactManager?getArtifactManager(nullptr):nullptr;int count=0;void* list=nullptr;void* grid=nullptr;
    if(Field(owner,0xac,count) && count>=7 && count<=ds::MaxArtifactSlots && Field(self,0xf0,list)&&Field(self,0x100,grid)) {
        void* parent=componentTransform(grid,nullptr);
        if(!ResizeArtifactBoxes(list,parent,count)){ArtifactUiResult(false);return;}
    }
    void* backList=nullptr;void* backGrid=nullptr;
    if(Field(self,0xf8,backList)&&Field(self,0x108,backGrid)&&backList&&backGrid &&
       !ResizeArtifactBoxes(backList,componentTransform(backGrid,nullptr),artifactTracking.backpack,6)){ArtifactUiResult(false);return;}
    originalArtifactPopupEnable(self,method);
    // Native OnEnable also changes cell sizes for the seventh bonus slot.
    if(grid && count>=7 && !ResizeArtifactBoxes(list,componentTransform(grid,nullptr),count))ArtifactUiResult(false);
    if(backList&&backGrid&&!ResizeArtifactBoxes(backList,componentTransform(backGrid,nullptr),artifactTracking.backpack,6))ArtifactUiResult(false);
}
bool EnsureInventoryBoxes(void* self) {
    if(!checkArtifactInventoryPool)return false;
    checkArtifactInventoryPool(self,artifactTracking.backpack,nullptr);
    void* list=nullptr;void* parent=nullptr;
    return Field(self,0x30,list)&&Field(self,0x20,parent)&&ResizeArtifactBoxes(list,parent,artifactTracking.backpack,6);
}
void __fastcall OnInventoryInit(void* self,const void* method){if(EnsureInventoryBoxes(self))originalInventoryInit(self,method);else ArtifactUiResult(false);}
void __fastcall OnViewerInventoryInit(void* self,const void* method){if(EnsureInventoryBoxes(self))originalViewerInventoryInit(self,method);else ArtifactUiResult(false);}
void __fastcall OnArtifactPopupDisable(void* self,const void* method){originalArtifactPopupDisable(self,method);artifactPopupOpen=false;}
void __fastcall OnCharacterEnable(void* self,const void* method){artifactViewerOpen=true;originalCharacterEnable(self,method);}
void __fastcall OnCharacterDisable(void* self,const void* method){originalCharacterDisable(self,method);artifactViewerOpen=false;}

void* ArtifactSaveContainer() {
    if(!initializeMetadata || !getDataController || !dataControllerMethodUsage)return nullptr;
    initializeMetadata(dataControllerMethodUsage);void* method=nullptr;void* container=nullptr;
    if(!Field(dataControllerMethodUsage,0,method)||!method)return nullptr;
    return Field(getDataController(method),0x28,container)?container:nullptr;
}
void StampArtifactSave(void* owner,void* container) {
    int physical=0,allowed=0,saved=0;
    if(artifactTracking.owner!=owner || !Field(owner,0xac,physical)||!Field(owner,0xb0,allowed)||!Field(container,0x1cd8,saved)||saved!=allowed)return;
    if(physical!=7 || allowed!=artifactTracking.nativeAllowed || artifactTracking.backpack!=6 || skillCurrentSlots!=6 || skillCurrentBackpack!=6 || skillCurrentBuffSlots!=2) {
        int marker=artifactTracking.backpack==6&&skillCurrentSlots==6&&skillCurrentBackpack==6&&skillCurrentBuffSlots==2?ds::artifacts::SaveCapacity(physical,artifactTracking.nativeAllowed):ds::artifacts::SaveLayout(physical,artifactTracking.nativeAllowed,artifactTracking.backpack,skillCurrentSlots,skillCurrentBackpack,skillCurrentBuffSlots);
        if(WriteField(container,0x1cd8,marker)){artifactSaveHint=ManagedRoot(artifactApi,container);artifactSaveHintValue=marker;}
    }
    else if(artifactSaveHint.Get()==container){artifactSaveHint.Reset();artifactSaveHintValue=0;}
}
void* __fastcall OnArtifactSaveStates(void* self,const void* method) {
    const uintptr_t caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    void* states=originalArtifactSaveStates(self,method);
    if(caller==gameBase+ds::ArtifactSaveCallRva+5)StampArtifactSave(self,ArtifactSaveContainer());
    return states;
}
void RestoreArtifactSavePreview(void* container) {
    int encoded=0,physical=0,native=0;
    if(Field(container,0x1cd8,encoded)&&ds::artifacts::LoadCapacity(encoded,physical,native))WriteField(container,0x1cd8,physical);
}
void __fastcall OnAutoSave(void* self,void* sceneName,const void* method) {
    if(artifactLoadFailed || skillLoadFailed){ArtifactStatus(-2);return;}
    originalAutoSave(self,sceneName,method);
    // The file contains layout metadata; keep the game's in-memory preview a
    // normal count after the original serializer has completed.
    void* container=nullptr;if(Field(self,0x28,container))RestoreArtifactSavePreview(container);
}
bool PreviewImages(void* self,int count) {
    void* list=nullptr;std::vector<void*> images;
    if(!Field(self,0xb0,list)||!ManagedItems(list,images)||images.empty())return false;
    void* imageTransform=componentTransform(images.front(),nullptr);void* wrapper=nullptr;void* parent=nullptr;int child=0;
    if(!artifactApi.Call(imageTransform,"get_parent",0,nullptr,&wrapper)||!wrapper||
       !artifactApi.Call(wrapper,"get_parent",0,nullptr,&parent)||!parent||!Boxed(imageTransform,"GetSiblingIndex",&child,4))return false;
    void* imageType=artifactApi.Type("UnityEngine.UI","Image");if(!imageType)return false;
    bool visual=true;
    if(count>7)for(void* image:images){void* slot=nullptr;if(!artifactApi.Call(componentTransform(image,nullptr),"get_parent",0,nullptr,&slot)||!slot)return false;visual=CaptureArtifactVisual(image,slot,true)&&visual;}
    while(int(images.size())<count) {
        // Each native image lives inside a slot wrapper. Clone the complete
        // wrapper, preserving its frame, mask and other decorative children.
        ManagedRoot root(artifactApi,cloneArtifactBox(componentGameObject(wrapper,nullptr),parent,false,nullptr));if(!root.Get())return false;
        void* transform=componentTransform(root.Get(),nullptr);void* clonedImageTransform=nullptr;void* childArgs[]={&child};
        if(!artifactApi.Call(transform,"GetChild",1,childArgs,&clonedImageTransform)||!clonedImageTransform){destroyArtifactObject(root.Get(),nullptr);return false;}
        void* image=getTypeComponent(componentGameObject(clonedImageTransform,nullptr),imageType,nullptr);
        void* args[]={image};if(!image||!artifactApi.Call(list,"Add",1,args)){destroyArtifactObject(root.Get(),nullptr);return false;}
        bool active=true;void* activeArgs[]={&active};artifactApi.Call(root.Get(),"SetActive",1,activeArgs);
        visual=CaptureArtifactVisual(image,transform,true,images.front())&&visual;
        images.push_back(image);
    }
    while(int(images.size())>count) {
        int index=int(images.size())-1;void* args[]={&index};void* extraWrapper=nullptr;
        if(!artifactApi.Call(componentTransform(images.back(),nullptr),"get_parent",0,nullptr,&extraWrapper)||!extraWrapper||!artifactApi.Call(list,"RemoveAt",1,args))return false;
        void* game=componentGameObject(extraWrapper,nullptr);bool active=false;void* activeArgs[]={&active};artifactApi.Call(game,"SetActive",1,activeArgs);
        ForgetArtifactVisual(images.back());destroyArtifactObject(game,nullptr);images.pop_back();
    }
    bool fit=FitArtifactLayout(parent,count);if(fit)visual=ApplyArtifactVisuals(images,parent,count)&&visual;
    ArtifactUiResult(fit&&visual);return true;
}
void __fastcall OnLoadPreview(void* self,const void* method) {
    void* container=ArtifactSaveContainer();int saved=0,physical=0,native=0;
    bool tagged=Field(container,0x1cd8,saved)&&ds::artifacts::LoadCapacity(saved,physical,native);
    if(!tagged)physical=std::max(7,std::clamp(saved,6,ds::MaxArtifactSlots));
    if(self && !PreviewImages(self,physical))ArtifactUiResult(false);
    // Native preview tests equality with seven to show the seventh wrapper;
    // its indexed sprite loop can populate every added image once resized.
    int preview=(tagged || saved>7)?7:saved;
    if(preview!=saved)WriteField(container,0x1cd8,preview);
    originalLoadPreview(self,method);
    if(preview!=saved)WriteField(container,0x1cd8,saved);
}
void __fastcall OnArtifactLoad(void* self,void* container,const void* method) {
    int saved=0,physical=7,native=6,backpack=6,skills=6;
    auto failed=[] {artifactLoadFailed=true;ArtifactStatus(-2);};
    if(!Field(container,0x1cd8,saved)){failed();return;}
    bool tagged=ds::artifacts::LoadLayout(saved,physical,native,backpack,skills);
    if(ds::artifacts::Tagged(saved) && !tagged){failed();return;}
    if(!tagged && artifactSaveHint.Get()==container) {
        int cachedPhysical=0,cachedNative=0;
        if(ds::artifacts::LoadLayout(artifactSaveHintValue,cachedPhysical,cachedNative,backpack,skills) && saved==cachedPhysical) {physical=cachedPhysical;native=cachedNative;tagged=true;}
    }
    if(!tagged) {physical=std::max(7,std::clamp(saved,6,ds::MaxArtifactSlots));native=saved==7?7:6;}
    // A tagged save must have enough physical entries before native Init and
    // indexed restoration execute, even while the capacity switch is off.
    artifactBusy=true;bool ready=ResizeArtifacts(self,physical,backpack);artifactBusy=false;
    if(!ready){failed();return;}
    void* records=nullptr;std::vector<void*> savedItems,currentItems;
    if(!Field(container,0x1cd0,records)||!ManagedItems(records,savedItems)){failed();return;}
    void* list=nullptr;if(!Field(self,0xa0,list)||!ManagedItems(list,currentItems)){failed();return;}
    for(void* item:savedItems){int index=-1;if(!Field(item,0x18,index)||index<0||index>=int(currentItems.size())){failed();return;}}
    artifactLoadFailed=false;
    artifactLoading=true;
    if(tagged)WriteField(container,0x1cd8,native);
    originalArtifactLoad(self,container,method);
    if(tagged)WriteField(container,0x1cd8,saved);
    artifactLoading=false;
    // A tagged seven-slot save can contain an occupied custom seventh slot
    // without the native seventh-slot bonus. Keep it usable until removed.
    int restoredAllowed=tagged?physical:(physical>7?physical:native);
    artifactTracking={self,native,restoredAllowed,physical>7 || restoredAllowed!=native,backpack};
    // Existing extra equipment remains coherent while waiting for removal.
    WriteField(self,0xb0,restoredAllowed);
    if(ApplyArtifactCapacity(self))originalArtifactSet(self,nullptr);
    else if(artifactTracking.expanded)originalArtifactSet(self,nullptr);
}
