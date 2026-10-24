#include <map>
#include <memory>

namespace {
template<class T> void Put(void* object,size_t offset,T value){Put(static_cast<unsigned char*>(object),offset,value);}
struct ArtiMemory {alignas(16) unsigned char bytes[0x3400]{};};
std::vector<std::unique_ptr<ArtiMemory>> artiMemory;
std::map<uintptr_t,void*> artiRoots;
std::map<std::string,std::string> artiMethods;
uintptr_t artiNextRoot=0x123450000ULL;int artiAllocationLimit=-1,artiSets=0,artiFrames=0,artiInitializations=0,artiLoads=0,artiDestroyed=0;
bool artiCloneFails=false;void* artiOwner=nullptr;void* artiController=nullptr;const void* artiMethodSeen=nullptr;
void* artiPlayUi=nullptr;
int artiPreviewValue=0;
const void* artiLoadMethod=nullptr;const void* artiFrameMethod=nullptr;
uintptr_t artiCaller=0;int artiFileMetadata=0;
constexpr uintptr_t ArtiListClass=1,ArtiItemClass=2,ArtiRectClass=3,ArtiGridClass=4,ArtiHorizontalClass=5,ArtiBoxClass=6,ArtiImageClass=8;
void* ArtiObject(uintptr_t klass) {
    if(artiAllocationLimit==0)return nullptr;if(artiAllocationLimit>0)--artiAllocationLimit;
    auto memory=std::make_unique<ArtiMemory>();void* object=memory->bytes;Put(object,0,reinterpret_cast<void*>(klass));
    artiMemory.push_back(std::move(memory));return object;
}
void* ArtiClass(void* object) {void* klass=nullptr;assert(Field(object,0,klass));return klass;}
void* ArtiNew(void* klass) {
    void* object=ArtiObject(reinterpret_cast<uintptr_t>(klass));
    if(object && reinterpret_cast<uintptr_t>(klass)==ArtiListClass) {
        void* array=ArtiObject(0x90);if(!array)return nullptr;Put(array,0x18,uintptr_t(ds::artifacts::MaxItems));Put(object,0x10,array);Put(object,0x18,0);
    }
    return object;
}
uintptr_t ArtiRoot(void* object,bool pinned) {assert(!pinned && object);auto id=artiNextRoot++;assert(id>UINT32_MAX);artiRoots[id]=object;return id;}
void ArtiFree(uintptr_t id) {assert(id>UINT32_MAX && artiRoots.erase(id)==1);}
void* ArtiTarget(uintptr_t id){assert(id>UINT32_MAX);return artiRoots.at(id);}
void ArtiBarrier(void* owner,void** field,void* value){assert(owner && *field==value);}
const void* ArtiFindMethod(void*,const char* name,int) {
    auto [it,inserted]=artiMethods.emplace(name,name);return &it->second;
}
void* ArtiBoxValue(const void* data,size_t size) {void* box=ArtiObject(0x91);assert(box);memcpy(static_cast<unsigned char*>(box)+0x10,data,size);return box;}
void* ArtiInvoke(const void* method,void* object,void** args,void** exception) {
    *exception=nullptr;const auto& name=*static_cast<const std::string*>(method);
    if(name=="get_Instance"){assert(!object && !args);return artiPlayUi;}
    auto klass=reinterpret_cast<uintptr_t>(ArtiClass(object));
    if(klass==ArtiListClass) {
        void* array=nullptr;int size=0;assert(Field(object,0x10,array)&&Field(object,0x18,size));
        if(name==".ctor"){assert(!args);return nullptr;}
        if(name=="set_Capacity"){assert(args && *static_cast<int*>(args[0])<=ds::artifacts::MaxItems);return nullptr;}
        if(name=="Add") {assert(args && args[0]);Put(array,0x20+size_t(size)*8,args[0]);Put(object,0x18,size+1);return nullptr;}
        if(name=="RemoveAt") {int at=*static_cast<int*>(args[0]);assert(at==size-1);Put(object,0x18,size-1);return nullptr;}
    }
    if(name=="get_padding")return nullptr;
    if(name=="get_parent" || name=="GetChild") {void* value=nullptr;assert(Field(object,name=="get_parent"?0x50:0x78,value));if(name=="GetChild")assert(args && *static_cast<int*>(args[0])==0);return value;}
    if(name=="GetSiblingIndex"){int value=0;assert(Field(object,0x58,value));return ArtiBoxValue(&value,4);}
    if(name=="SetActive"){assert(args);Put(object,0x134,*static_cast<bool*>(args[0]));return nullptr;}
    const std::map<std::string,std::pair<int,int>> properties={
        {"cellSize",{0x110,8}},{"spacing",{0x118,8}},{"constraint",{0x120,4}},{"constraintCount",{0x124,4}},
        {"startAxis",{0x128,4}},{"startCorner",{0x12c,4}},{"childAlignment",{0x130,4}},{"enabled",{0x134,1}},{"activeInHierarchy",{0x134,1}},{"rect",{0x100,16}},
        {"sizeDelta",{0x140,8}},{"anchoredPosition",{0x148,8}},{"fontSize",{0x150,4}},{"fontSizeMin",{0x154,4}},{"fontSizeMax",{0x158,4}},
        {"anchorMin",{0x160,8}},{"anchorMax",{0x168,8}},{"pivot",{0x170,8}}};
    bool get=name.rfind("get_",0)==0,set=name.rfind("set_",0)==0;
    if(get||set) {
        auto it=properties.find(name.substr(4));assert(it!=properties.end());auto [offset,count]=it->second;
        if(get)return ArtiBoxValue(static_cast<unsigned char*>(object)+offset,count);
        assert(args && args[0]);memcpy(static_cast<unsigned char*>(object)+offset,args[0],count);return nullptr;
    }
    assert(false);return nullptr;
}
void* ArtiDomain(){return reinterpret_cast<void*>(0x92);}
const void** ArtiAssemblies(void*,size_t* count){static const void* image=reinterpret_cast<void*>(0x93);*count=1;return &image;}
const void* ArtiImage(const void* image){return image;}
void* ArtiFindClass(const void*,const char* space,const char* name) {
    if(strcmp(name,"Play_UI")==0){assert(!*space);return reinterpret_cast<void*>(7);}
    assert(strcmp(space,"UnityEngine.UI")==0);
    if(strcmp(name,"Image")==0)return reinterpret_cast<void*>(ArtiImageClass);
    return reinterpret_cast<void*>(strcmp(name,"GridLayoutGroup")==0?ArtiGridClass:ArtiHorizontalClass);
}
const void* ArtiType(void* klass){return klass;}void* ArtiTypeObject(const void* type){return const_cast<void*>(type);}
void ArtiConstructor(void* object,int index,const void* method){assert(!method);Put(object,0x18,index);}
void* ArtiInstance(const void* method){assert(!method);return artiOwner;}
void* ArtiTransform(void* object,const void*) {if(ArtiClass(object)==reinterpret_cast<void*>(ArtiRectClass))return object;void* parent=nullptr;assert(Field(object,0x48,parent));return parent?parent:object;}
void* ArtiGame(void* object,const void*) {void* game=nullptr;assert(Field(object,0x40,game));return game?game:object;}
void* ArtiGetComponent(void* game,void* type,const void*) {void* component=nullptr;assert(Field(game,type==reinterpret_cast<void*>(ArtiImageClass)?0x78:(type==reinterpret_cast<void*>(ArtiGridClass)?0x60:0x68),component));return component;}
void* ArtiAddComponent(void* game,void* type,const void*) {
    assert(type==reinterpret_cast<void*>(ArtiGridClass));void* component=ArtiObject(ArtiGridClass);Put(game,0x60,component);return component;
}
void* ArtiPreviewImage(void* parent) {
    void* wrapperGame=ArtiObject(0x94);void* wrapper=ArtiObject(ArtiRectClass);void* imageGame=ArtiObject(0x94);void* transform=ArtiObject(ArtiRectClass);void* image=ArtiObject(ArtiImageClass);
    Put(wrapperGame,0x48,wrapper);Put(wrapper,0x40,wrapperGame);Put(wrapper,0x50,parent);Put(wrapper,0x78,transform);
    Put(transform,0x40,imageGame);Put(transform,0x50,wrapper);Put(image,0x40,imageGame);Put(image,0x48,transform);Put(imageGame,0x78,image);
    Put(transform,0x108,90.0f);Put(transform,0x10c,90.0f);Put(transform,0x140,90.0f);Put(transform,0x144,90.0f);
    return image;
}
void* ArtiClone(void* prototype,void* parent,bool world,const void* method) {
    assert(!world && !method && prototype && parent);if(artiCloneFails)return nullptr;
    if(ArtiClass(prototype)==reinterpret_cast<void*>(0x94)) {
        void* image=ArtiPreviewImage(parent);void* wrapper=nullptr;void* sourceImage=nullptr;
        assert(Field(ArtiTransform(prototype,nullptr),0x78,sourceImage));
        memcpy(static_cast<unsigned char*>(ArtiTransform(image,nullptr))+0x100,static_cast<unsigned char*>(sourceImage)+0x100,0x60);
        assert(artifactApi.Call(ArtiTransform(image,nullptr),"get_parent",0,nullptr,&wrapper));return ArtiGame(wrapper,nullptr);
    }
    void* object=ArtiObject(ArtiBoxClass);void* game=ArtiObject(0x94);void* rect=ArtiObject(ArtiRectClass);
    Put(object,0x40,game);Put(object,0x48,rect);Put(game,0x48,rect);Put(rect,0x40,game);Put(rect,0x108,130.0f);Put(rect,0x10c,130.0f);
    Put(rect,0x140,130.0f);Put(rect,0x144,130.0f);Put(rect,0x148,7.0f);Put(rect,0x14c,-9.0f);Put(rect,0x164,1.0f);Put(rect,0x16c,1.0f);Put(rect,0x170,.5f);Put(rect,0x174,.5f);
    for(size_t field:{size_t(0x38),size_t(0x30)}) {
        void* component=ArtiObject(field==0x38?ArtiImageClass:9);void* child=ArtiObject(ArtiRectClass);Put(component,0x48,child);Put(object,field,component);
        Put(child,0x140,field==0x38?-30.0f:-14.0f);Put(child,0x144,field==0x38?-30.0f:25.0f);Put(child,0x14c,field==0x38?0.0f:-15.0f);
        Put(component,0x150,30.0f);Put(component,0x154,18.0f);Put(component,0x158,30.0f);
        if(ArtiClass(prototype)==reinterpret_cast<void*>(ArtiBoxClass)) {
            void* source=nullptr;assert(Field(prototype,field,source));
            memcpy(static_cast<unsigned char*>(child)+0x100,static_cast<unsigned char*>(ArtiTransform(source,nullptr))+0x100,0x60);
            memcpy(static_cast<unsigned char*>(component)+0x150,static_cast<unsigned char*>(source)+0x150,12);
        }
    }
    return object;
}
void ArtiDestroy(void* object,const void*) {assert(object);++artiDestroyed;}
void ArtiAdd(void* list,void* object) {void* args[]={object};assert(artifactApi.Call(list,"Add",1,args));}
void* ArtiList(){return ArtiNew(reinterpret_cast<void*>(ArtiListClass));}
std::vector<void*> ArtiItems(void* owner) {void* list=nullptr;std::vector<void*> items;assert(Field(owner,0xa0,list)&&ManagedItems(list,items));return items;}
int ArtiInt(void* object,size_t offset){int value=0;assert(Field(object,offset,value));return value;}
void* ArtiSetup(int physical=7,int allowed=6) {
    artifactTracking={};artifactPopupOpen=artifactViewerOpen=artifactLoading=artifactBusy=artifactUiFailed=artifactLoadFailed=false;
    void* owner=ArtiObject(0x95);Put(owner,0x20,true);Put(owner,0xac,physical);Put(owner,0xb0,allowed);
    void* list=ArtiList();for(int i=0;i<physical+13;++i){void* item=ArtiObject(ArtiItemClass);Put(item,0x18,i);Put(item,0x14,-1);ArtiAdd(list,item);}
    Put(owner,0xa0,list);artiOwner=owner;return owner;
}
__declspec(noinline) void ArtiSetFixture(void* self,const void* method){assert(self==artiOwner);++artiSets;artiMethodSeen=method;}
__declspec(noinline) void ArtiUpdateFixture(void* self,const void* method){assert(self==artiOwner);++artiFrames;artiMethodSeen=artiFrameMethod=method;}
void ArtiValidate(void*,const void*){}
__declspec(noinline) void ArtiEquipFixture(void* self,const void* method) {
    ++artiInitializations;artiMethodSeen=method;void* pool=nullptr;std::vector<void*> boxes;assert(Field(self,0x28,pool)&&ManagedItems(pool,boxes));
    int physical=ArtiInt(artiOwner,0xac);assert(int(boxes.size())>=physical);
    auto items=ArtiItems(artiOwner);for(int i=0;i<physical;++i){assert(ArtiInt(items[i],0x18)==i);Put(boxes[i],0x90,i);Put(boxes[i],0x8c,ArtiInt(items[i],0x14));}
}
__declspec(noinline) void ArtiLoadFixture(void* self,void* container,const void* method) {
    assert(self==artiOwner);++artiLoads;artiMethodSeen=artiLoadMethod=method;
    int native=ArtiInt(container,0x1cd8);assert(native==6||native==7);Put(self,0xb0,native);
    auto items=ArtiItems(self);for(void* item:items)Put(item,0x14,-1);
    void* saved=nullptr;assert(Field(container,0x1cd0,saved));std::vector<void*> records;assert(ManagedItems(saved,records));
    for(void* record:records){int index=ArtiInt(record,0x18);assert(index>=0 && index<int(items.size()));memcpy(static_cast<unsigned char*>(items[index])+0x10,static_cast<unsigned char*>(record)+0x10,0x16);}
}
__declspec(noinline) void* ArtiSaveStatesFixture(void* self,const void* method){assert(self==artiOwner);artiMethodSeen=method;artiCaller=reinterpret_cast<uintptr_t>(_ReturnAddress());return self;}
__declspec(noinline) void ArtiCaptureFixture(void* self,const void* method){ArtifactList volatile capture=ArtiSaveStatesFixture;assert(capture(self,method)==self);}
__declspec(noinline) void ArtiAutoSaveFixture(void* self,void* scene,const void* method) {
    assert(self==artiController && scene);void* container=nullptr;assert(Field(self,0x28,container));Put(container,0x1cd8,ArtiInt(artiOwner,0xb0));
    ArtiCaptureFixture(artiOwner,method);artiFileMetadata=ArtiInt(container,0x1cd8);
}
void ArtiPreviewFixture(void* self,const void*) {
    void* container=nullptr;assert(Field(artiController,0x28,container));artiPreviewValue=ArtiInt(container,0x1cd8);
    if(!self)return;void* pool=nullptr;void* saved=nullptr;std::vector<void*> images,records;
    assert(Field(self,0xb0,pool)&&ManagedItems(pool,images)&&Field(container,0x1cd0,saved)&&ManagedItems(saved,records));
    for(void* image:images)Put(image,0x8c,-1);
    for(void* record:records){int index=ArtiInt(record,0x18);if(index>=0&&index<int(images.size()))Put(images[index],0x8c,ArtiInt(record,0x14));}
}
void ArtiResetFixture(void* self,const void*) {for(void* item:ArtiItems(self))Put(item,0x14,-1);Put(self,0xb0,6);}
void ArtiMetadata(void*){}void* ArtiControllerInstance(const void*){return artiController;}
void ArtifactTests(ds::Shared& data,const void* method) {
    artifactApi={};artifactApi.objectClass=ArtiClass;artifactApi.findMethod=ArtiFindMethod;artifactApi.invoke=ArtiInvoke;artifactApi.newObject=ArtiNew;
    artifactApi.rootNew=ArtiRoot;artifactApi.rootFree=ArtiFree;artifactApi.rootTarget=ArtiTarget;artifactApi.barrier=ArtiBarrier;
    artifactApi.domain=ArtiDomain;artifactApi.assemblies=ArtiAssemblies;artifactApi.image=ArtiImage;artifactApi.findClass=ArtiFindClass;artifactApi.classType=ArtiType;artifactApi.typeObject=ArtiTypeObject;
    constructArtifact=ArtiConstructor;getArtifactManager=ArtiInstance;cloneArtifactBox=ArtiClone;componentTransform=ArtiTransform;componentGameObject=ArtiGame;
    getTypeComponent=ArtiGetComponent;addTypeComponent=ArtiAddComponent;destroyArtifactObject=ArtiDestroy;originalEquipValidate=ArtiValidate;originalEquipInit=originalViewerEquipInit=ArtiEquipFixture;
    originalArtifactSet=ArtiSetFixture;originalArtifactUpdate=ArtiUpdateFixture;originalArtifactLoad=ArtiLoadFixture;originalArtifactSaveStates=ArtiSaveStatesFixture;originalAutoSave=ArtiAutoSaveFixture;
    auto oldMetadata=initializeMetadata;auto oldController=getDataController;void* oldUsage=dataControllerMethodUsage;uintptr_t oldBase=gameBase;
    void* usage=reinterpret_cast<void*>(0x96);dataControllerMethodUsage=&usage;initializeMetadata=ArtiMetadata;getDataController=ArtiControllerInstance;
    data.state=ds::Ready;data.available=ds::All;data.clientHeartbeat=LONG(GetTickCount());data.enabled=0;data.artifactSlots=12;
    void* owner=ArtiSetup();auto original=ArtiItems(owner);Put(original[0],0x14,100);Put(original[0],0x1c,4);Put(original[7],0x14,200);Put(original[19],0x14,201);
    assert(!ApplyArtifactCapacity(owner) && ArtiItems(owner)==original && ArtiInt(owner,0xac)==7);
    void* nativePanel=ArtiObject(0x97);void* nativeParent=ArtiObject(ArtiRectClass);void* nativePool=ArtiList();
    for(int i=0;i<10;++i)ArtiAdd(nativePool,ArtiClone(nativeParent,nativeParent,false,nullptr));Put(nativePanel,0x28,nativePool);Put(nativePanel,0x20,nativeParent);
    OnEquipInit(nativePanel,method);std::vector<void*> nativeBoxes;
    assert(ManagedItems(nativePool,nativeBoxes)&&nativeBoxes.size()==10&&artiDestroyed==0&&artifactLayouts.empty()&&artifactVisuals.empty());
    data.enabled=ds::ArtifactCapacity;assert(ApplyArtifactCapacity(owner));auto expanded=ArtiItems(owner);
    assert(ArtiInt(owner,0xac)==12 && ArtiInt(owner,0xb0)==12 && expanded.size()==25);
    assert(expanded[0]==original[0] && ArtiInt(expanded[0],0x1c)==4 && expanded[12]==original[7] && expanded[24]==original[19]);
    for(int i=7;i<12;++i)assert(ArtiInt(expanded[i],0x14)==-1);
    for(int i=0;i<25;++i)assert(ArtiInt(expanded[i],0x18)==i);
    assert(!ApplyArtifactCapacity(owner));Put(expanded[10],0x14,300);data.artifactSlots=8;
    assert(!ApplyArtifactCapacity(owner) && data.artifactStatus==2 && ArtiInt(owner,0xac)==12 && ArtiInt(expanded[10],0x14)==300);
    Put(expanded[10],0x14,-1);assert(ApplyArtifactCapacity(owner));assert(ArtiInt(owner,0xac)==8 && ArtiItems(owner)[8]==original[7]);
    artifactViewerOpen=true;data.artifactSlots=30;assert(!ApplyArtifactCapacity(owner) && data.artifactStatus==3);artifactViewerOpen=false;
    artiPlayUi=ArtiObject(7);void* existingPopup=ArtiObject(0x97);void* popupGame=ArtiObject(0x94);
    Put(existingPopup,0x10,uintptr_t(1));Put(existingPopup,0x40,popupGame);Put(popupGame,0x134,true);Put(artiPlayUi,0x3d8,existingPopup);
    assert(!ApplyArtifactCapacity(owner) && data.artifactStatus==3 && ArtiInt(owner,0xac)==8);
    Put(popupGame,0x134,false);
    assert(ApplyArtifactCapacity(owner) && ArtiItems(owner).size()==43 && ArtiItems(owner)[30]==original[7]);
    data.clientHeartbeat=LONG(GetTickCount()-4000);assert(ApplyArtifactCapacity(owner) && ArtiInt(owner,0xac)==7 && ArtiInt(owner,0xb0)==6);
    data.clientHeartbeat=LONG(GetTickCount());data.artifactSlots=12;data.enabled=ds::ArtifactCapacity;
    void* oldList=nullptr;assert(Field(owner,0xa0,oldList));artiAllocationLimit=1;
    assert(!ApplyArtifactCapacity(owner));void* afterList=nullptr;assert(Field(owner,0xa0,afterList)&&oldList==afterList && ArtiInt(owner,0xac)==7);
    artiAllocationLimit=-1;assert(ApplyArtifactCapacity(owner));

    // UI adapters reuse the native callback initializer for every new index.
    void* panel=ArtiObject(0x97);void* parent=ArtiObject(ArtiRectClass);float rect[]={0,0,500,180};memcpy(static_cast<unsigned char*>(parent)+0x100,rect,sizeof(rect));
    void* game=ArtiObject(0x94);Put(parent,0x40,game);void* horizontal=ArtiObject(ArtiHorizontalClass);Put(horizontal,0x134,true);Put(game,0x68,horizontal);
    void* pool=ArtiList();for(int i=0;i<7;++i){void* box=ArtiClone(parent,parent,false,nullptr);ArtiAdd(pool,box);}Put(panel,0x28,pool);Put(panel,0x20,parent);
    OnViewerEquipInit(panel,method);assert(artiMethodSeen==method && artiInitializations>0);
    std::vector<void*> boxes;assert(ManagedItems(pool,boxes) && boxes.size()==12);
    for(int i=0;i<12;++i)assert(ArtiInt(boxes[i],0x90)==i);
    bool enabled=true;assert(Field(horizontal,0x134,enabled)&&!enabled);
    data.artifactSlots=30;assert(ApplyArtifactCapacity(owner));artiCloneFails=true;int inits=artiInitializations;
    OnViewerEquipInit(panel,method);assert(artiInitializations==inits && data.artifactStatus==-1);artiCloneFails=false;
    OnViewerEquipInit(panel,method);assert(ManagedItems(pool,boxes)&&boxes.size()==30);
    auto style=[&](void* box,size_t field,size_t offset){void* component=nullptr;float value=0;assert(Field(box,field,component)&&Field(ArtiTransform(component,nullptr),offset,value));return value;};
    float smallFactor=ds::artifacts::FitGrid(500,180,30).cell/130;
    assert(std::abs(style(boxes[0],0x38,0x140)+30*smallFactor)<.01f);
    assert(std::abs(style(boxes[29],0x38,0x140)-style(boxes[0],0x38,0x140))<.01f);
    void* smallText=nullptr;assert(Field(boxes[29],0x30,smallText));float smallFont=0;assert(Field(smallText,0x150,smallFont)&&std::abs(smallFont-30*smallFactor)<.01f);
    // A transient zero-size layout still initializes all native callbacks and
    // reports an error; the next valid layout recovers without reallocating.
    Put(parent,0x108,0.0f);inits=artiInitializations;OnViewerEquipInit(panel,method);
    assert(artiInitializations==inits+1 && data.artifactStatus==-1 && ArtiInt(boxes[29],0x90)==29);
    assert(!ApplyArtifactCapacity(owner) && data.artifactStatus==-1);Put(parent,0x108,500.0f);OnViewerEquipInit(panel,method);assert(data.artifactStatus==1);
    // Native-like replacement/disassembly changes contents at the same index;
    // UI callback IDs and inventory objects remain usable after expansion.
    expanded=ArtiItems(owner);int index=ArtiInt(boxes[29],0x90);Put(expanded[index],0x14,777);Put(expanded[index],0x1c,3);
    assert(index==29 && ArtiInt(expanded[index],0x14)==777);Put(expanded[index],0x14,-1);
    void* originalBoxRect=ArtiTransform(boxes[0],nullptr);Put(originalBoxRect,0x140,25.0f);Put(originalBoxRect,0x148,500.0f);Put(originalBoxRect,0x164,0.0f);
    ds::Disable(&data);assert(ApplyArtifactCapacity(owner));OnViewerEquipInit(panel,method);
    assert(ManagedItems(pool,boxes)&&boxes.size()==7 && artiDestroyed==23);assert(Field(horizontal,0x134,enabled)&&enabled);
    assert(style(boxes[0],0x38,0x140)==-30 && style(boxes[0],0x30,0x14c)==-15);
    assert(Field(boxes[0],0x30,smallText)&&Field(smallText,0x150,smallFont)&&smallFont==30);
    float restoredRoot=0;assert(Field(originalBoxRect,0x140,restoredRoot)&&restoredRoot==130);assert(Field(originalBoxRect,0x148,restoredRoot)&&restoredRoot==7);assert(Field(originalBoxRect,0x164,restoredRoot)&&restoredRoot==1);
    for(int count=8;count<=50;++count)for(auto dimensions:{std::pair{700.0f,48.0f},std::pair{500.0f,180.0f},std::pair{400.0f,320.0f}}) {
        auto grid=ds::artifacts::FitGrid(dimensions.first,dimensions.second,count);int rows=(count+grid.columns-1)/grid.columns;
        assert(grid.cell>0 && grid.cell*grid.columns+grid.spacing*(grid.columns-1)<=dimensions.first+.01f && grid.cell*rows+grid.spacing*(rows-1)<=dimensions.second+.01f);
    }

    // Real x64 detours for update, tagged save capture, serializer and load.
    for(auto hooks:{std::pair{reinterpret_cast<void*>(&ArtiUpdateFixture),reinterpret_cast<void*>(&OnArtifactUpdate)},
                    std::pair{reinterpret_cast<void*>(&ArtiSaveStatesFixture),reinterpret_cast<void*>(&OnArtifactSaveStates)},
                    std::pair{reinterpret_cast<void*>(&ArtiAutoSaveFixture),reinterpret_cast<void*>(&OnAutoSave)},
                    std::pair{reinterpret_cast<void*>(&ArtiLoadFixture),reinterpret_cast<void*>(&OnArtifactLoad)}}) {
        void** originalHook=hooks.first==reinterpret_cast<void*>(&ArtiUpdateFixture)?reinterpret_cast<void**>(&originalArtifactUpdate):
            hooks.first==reinterpret_cast<void*>(&ArtiSaveStatesFixture)?reinterpret_cast<void**>(&originalArtifactSaveStates):
            hooks.first==reinterpret_cast<void*>(&ArtiAutoSaveFixture)?reinterpret_cast<void**>(&originalAutoSave):reinterpret_cast<void**>(&originalArtifactLoad);
        assert(MH_CreateHook(hooks.first,hooks.second,originalHook)==MH_OK);
    }
    ArtiCaptureFixture(owner,method);gameBase=artiCaller-ds::ArtifactSaveCallRva-5;
    for(void* target:{reinterpret_cast<void*>(&ArtiUpdateFixture),reinterpret_cast<void*>(&ArtiSaveStatesFixture),reinterpret_cast<void*>(&ArtiAutoSaveFixture),reinterpret_cast<void*>(&ArtiLoadFixture)})assert(MH_EnableHook(target)==MH_OK);
    GameAction volatile frame=ArtiUpdateFixture;AutoSave volatile save=ArtiAutoSaveFixture;ArtifactLoad volatile load=ArtiLoadFixture;
    data.enabled=ds::ArtifactCapacity;data.artifactSlots=20;frame(owner,method);assert(ArtiInt(owner,0xac)==20 && artiFrames>0 && artiFrameMethod==method);
    // Capture an original seven-slot bonus capability without losing it on save.
    artifactTracking.nativeAllowed=7;expanded=ArtiItems(owner);Put(expanded[19],0x14,888);Put(expanded[19],0x1c,5);
    void* container=ArtiObject(0x98);void* saved=ArtiList();for(void* item:expanded)if(ArtiInt(item,0x14)>=0){void* copy=ArtiObject(ArtiItemClass);memcpy(static_cast<unsigned char*>(copy)+0x10,static_cast<unsigned char*>(item)+0x10,0x16);ArtiAdd(saved,copy);}
    Put(container,0x1cd0,saved);artiController=ArtiObject(0x99);Put(artiController,0x28,container);
    int scene=12;save(artiController,&scene,method);int physical=0,native=0;
    assert(ds::artifacts::LoadCapacity(artiFileMetadata,physical,native) && physical==20 && native==7 && ArtiInt(container,0x1cd8)==20);
    void* previewPanel=ArtiObject(0x97);void* previewParent=ArtiObject(ArtiRectClass);void* previewGame=ArtiObject(0x94);Put(previewParent,0x40,previewGame);
    memcpy(static_cast<unsigned char*>(previewParent)+0x100,rect,sizeof(rect));void* previewPool=ArtiList();
    for(int i=0;i<7;++i)ArtiAdd(previewPool,ArtiPreviewImage(previewParent));Put(previewPanel,0xb0,previewPool);
    originalLoadPreview=ArtiPreviewFixture;OnLoadPreview(previewPanel,method);std::vector<void*> previewImages;
    assert(artiPreviewValue==7 && ArtiInt(container,0x1cd8)==20 && ManagedItems(previewPool,previewImages) && previewImages.size()==20 && ArtiInt(previewImages[19],0x8c)==888);
    float previewSize=0;assert(Field(ArtiTransform(previewImages[19],nullptr),0x140,previewSize)&&std::abs(previewSize-ds::artifacts::FitGrid(500,180,20).cell)<.01f);
    Put(container,0x1cd8,artiFileMetadata);OnLoadPreview(previewPanel,method);
    assert(artiPreviewValue==7 && ArtiInt(container,0x1cd8)==artiFileMetadata && previewImages.size()==20);
    Put(container,0x1cd8,20);
    ds::Disable(&data);void* newOwner=ArtiSetup();load(newOwner,container,method);
    assert(artiLoads==1 && artiLoadMethod==method && ArtiInt(newOwner,0xac)==20 && data.artifactStatus==2 && artifactTracking.nativeAllowed==7);
    auto loaded=ArtiItems(newOwner);assert(ArtiInt(loaded[19],0x14)==888 && ArtiInt(loaded[19],0x1c)==5 && ArtiInt(loaded[20],0x14)==200);
    Put(loaded[19],0x14,-1);frame(newOwner,method);assert(ArtiInt(newOwner,0xac)==7 && ArtiInt(newOwner,0xb0)==7 && ArtiInt(ArtiItems(newOwner)[7],0x14)==200);
    // Fresh file decoding, native preview count and the occupied custom bonus
    // slot must also survive with every feature switch disabled.
    artifactSaveHint.Reset();void* bonusSave=ArtiObject(0x98);void* bonusRecords=ArtiList();
    void* bonusItem=ArtiObject(ArtiItemClass);Put(bonusItem,0x18,6);Put(bonusItem,0x14,666);ArtiAdd(bonusRecords,bonusItem);
    Put(bonusSave,0x1cd0,bonusRecords);int bonusTag=ds::artifacts::SaveCapacity(7,6);Put(bonusSave,0x1cd8,bonusTag);Put(artiController,0x28,bonusSave);
    originalLoadPreview=ArtiPreviewFixture;OnLoadPreview(nullptr,method);assert(artiPreviewValue==7 && ArtiInt(bonusSave,0x1cd8)==bonusTag);
    void* bonusOwner=ArtiSetup();load(bonusOwner,bonusSave,method);
    assert(data.artifactStatus==2 && ArtiInt(bonusOwner,0xb0)==7 && ArtiInt(ArtiItems(bonusOwner)[6],0x14)==666 && artifactTracking.nativeAllowed==6);
    data.clientHeartbeat=LONG(GetTickCount()-4000);frame(bonusOwner,method);assert(data.artifactStatus==2 && ArtiInt(bonusOwner,0xb0)==7);
    data.clientHeartbeat=LONG(GetTickCount());Put(ArtiItems(bonusOwner)[6],0x14,-1);frame(bonusOwner,method);assert(ArtiInt(bonusOwner,0xb0)==6 && data.artifactStatus==0);
    for(int base:{6,7}) {
        Put(bonusSave,0x1cd8,base);Put(bonusSave,0x1cd0,ArtiList());load(bonusOwner,bonusSave,method);
        assert(ArtiInt(bonusOwner,0xb0)==base && artifactTracking.nativeAllowed==base && data.artifactStatus==0);
        OnLoadPreview(previewPanel,method);assert(artiPreviewValue==base && ManagedItems(previewPool,previewImages) && previewImages.size()==7);
        assert(Field(ArtiTransform(previewImages[0],nullptr),0x140,previewSize)&&previewSize==90);
    }
    // Reject broken tags and impossible saved indices before native indexed
    // loading. Do not let a partial resume overwrite the original autosave.
    int loadsBefore=artiLoads;int previousFile=artiFileMetadata;Put(bonusSave,0x1cd8,0x4453ff06);load(bonusOwner,bonusSave,method);
    assert(artiLoads==loadsBefore && artifactLoadFailed && data.artifactStatus==-2);save(artiController,&scene,method);assert(artiFileMetadata==previousFile);
    Put(bonusSave,0x1cd8,ds::artifacts::SaveCapacity(30,6));artiAllocationLimit=1;load(bonusOwner,bonusSave,method);
    assert(artiLoads==loadsBefore && artifactLoadFailed);artiAllocationLimit=-1;
    Put(bonusItem,0x18,99);Put(bonusSave,0x1cd0,bonusRecords);load(bonusOwner,bonusSave,method);assert(artiLoads==loadsBefore && artifactLoadFailed);
    Put(bonusItem,0x18,29);load(bonusOwner,bonusSave,method);assert(artiLoads==loadsBefore+1 && !artifactLoadFailed && data.artifactStatus==2 && ArtiInt(ArtiItems(bonusOwner)[29],0x14)==666);
    originalArtifactReset=ArtiResetFixture;OnArtifactReset(bonusOwner,method);assert(!artifactLoadFailed && ArtiInt(bonusOwner,0xac)==7 && ArtiInt(bonusOwner,0xb0)==6);
    // The last equipment slot and its preview must survive a real resume;
    // disabling expansion cannot discard an occupied slot 50.
    artiOwner=bonusOwner;data.enabled=ds::ArtifactCapacity;data.artifactSlots=50;
    assert(ApplyArtifactCapacity(bonusOwner) && ArtiInt(bonusOwner,0xac)==50 && ArtiItems(bonusOwner).size()==63);
    auto fifty=ArtiItems(bonusOwner);Put(fifty[49],0x14,749);Put(fifty[49],0x1c,7);
    OnEquipInit(panel,method);assert(ManagedItems(pool,boxes) && boxes.size()==50 && ArtiInt(boxes[49],0x90)==49);
    Put(bonusItem,0x18,49);Put(bonusItem,0x14,749);Put(bonusItem,0x1c,7);Put(bonusSave,0x1cd0,bonusRecords);Put(bonusSave,0x1cd8,50);
    StampArtifactSave(bonusOwner,bonusSave);artifactSaveHint.Reset();
    assert(ds::artifacts::LoadCapacity(ArtiInt(bonusSave,0x1cd8),physical,native) && physical==50 && native==6);
    OnLoadPreview(previewPanel,method);assert(ManagedItems(previewPool,previewImages) && previewImages.size()==50 && ArtiInt(previewImages[49],0x8c)==749);
    ds::Disable(&data);void* fiftyOwner=ArtiSetup();load(fiftyOwner,bonusSave,method);
    assert(!artifactLoadFailed && data.artifactStatus==2 && ArtiInt(fiftyOwner,0xac)==50 && ArtiInt(ArtiItems(fiftyOwner)[49],0x1c)==7);
    Put(ArtiItems(fiftyOwner)[49],0x14,-1);frame(fiftyOwner,method);assert(ArtiInt(fiftyOwner,0xac)==7 && ArtiInt(fiftyOwner,0xb0)==6);
    assert(!ds::artifacts::LoadCapacity(ds::artifacts::SaveCapacity(51,6),physical,native));
    for(void* target:{reinterpret_cast<void*>(&ArtiUpdateFixture),reinterpret_cast<void*>(&ArtiSaveStatesFixture),reinterpret_cast<void*>(&ArtiAutoSaveFixture),reinterpret_cast<void*>(&ArtiLoadFixture)}) {assert(MH_DisableHook(target)==MH_OK);assert(MH_RemoveHook(target)==MH_OK);}
    assert(!ds::artifacts::LoadCapacity(0x4453ff06,physical,native));assert(!ds::artifacts::LoadCapacity(0x44531408,physical,native));
    artifactSaveHint.Reset();artifactLayouts.clear();artifactVisuals.clear();assert(artiRoots.empty());artifactApi={};artiMemory.clear();artiMethods.clear();artifactTracking={};artifactPlayUiClass=nullptr;artiPlayUi=nullptr;
    getArtifactManager=nullptr;constructArtifact=nullptr;data.enabled=0;data.clientHeartbeat=LONG(GetTickCount());
    initializeMetadata=oldMetadata;getDataController=oldController;dataControllerMethodUsage=oldUsage;gameBase=oldBase;
    std::cout<<"PASS: artifact 6..50 limits, transactional managed-list growth/shrink, equipment/inventory indices and object/counter preservation, occupied-tail/open-UI deferral incl. pre-attach windows, stale restore, allocation rollback, 64-bit GC handles, native UI pool/callback indices/grid fit, icon margins/text/font scaling, scaled clone normalization and original root anchors/geometry/style restore, expanded resume preview icons, save metadata and native/custom seventh slots, native resume/reset, invalid save autosave guard/retry, 4 real x64 detours and argument forwarding; isolated fixtures only\n";
}
}
