#pragma once
#include <windows.h>
#include <cstring>
#include <cstddef>
#include <cstdint>

// IL2CPP exports are resolved from the hash-checked, installed GameAssembly.
// Reflection invocation uses object pointers for reference arguments and
// pointers to storage for value arguments, matching Runtime::Invoke.
struct ManagedRuntime {
    void* (__cdecl* objectClass)(void*)=nullptr;
    const void* (__cdecl* findMethod)(void*,const char*,int)=nullptr;
    void* (__cdecl* invoke)(const void*,void*,void**,void**)=nullptr;
    void* (__cdecl* newObject)(void*)=nullptr;
    // Unity 6000's GC handles are pointer-sized slot addresses. Truncating
    // these to legacy 32-bit handles would break rooting on this x64 build.
    uintptr_t (__cdecl* rootNew)(void*,bool)=nullptr;
    void (__cdecl* rootFree)(uintptr_t)=nullptr;
    void* (__cdecl* rootTarget)(uintptr_t)=nullptr;
    void (__cdecl* barrier)(void*,void**,void*)=nullptr;
    void* (__cdecl* domain)()=nullptr;
    const void** (__cdecl* assemblies)(void*,size_t*)=nullptr;
    const void* (__cdecl* image)(const void*)=nullptr;
    void* (__cdecl* findClass)(const void*,const char*,const char*)=nullptr;
    const void* (__cdecl* classType)(void*)=nullptr;
    void* (__cdecl* typeObject)(const void*)=nullptr;
    void* (__cdecl* findField)(void*,const char*)=nullptr;
    void (__cdecl* staticFieldValue)(void*,void*)=nullptr;
    bool Load(HMODULE module) {
#define DS_EXPORT(field,name) field=reinterpret_cast<decltype(field)>(GetProcAddress(module,"il2cpp_" name)); if(!field)return false
        DS_EXPORT(objectClass,"object_get_class");DS_EXPORT(findMethod,"class_get_method_from_name");DS_EXPORT(invoke,"runtime_invoke");
        DS_EXPORT(newObject,"object_new");DS_EXPORT(rootNew,"gchandle_new");DS_EXPORT(rootFree,"gchandle_free");DS_EXPORT(rootTarget,"gchandle_get_target");
        DS_EXPORT(barrier,"gc_wbarrier_set_field");DS_EXPORT(domain,"domain_get");DS_EXPORT(assemblies,"domain_get_assemblies");
        DS_EXPORT(image,"assembly_get_image");DS_EXPORT(findClass,"class_from_name");DS_EXPORT(classType,"class_get_type");DS_EXPORT(typeObject,"type_get_object");
        DS_EXPORT(findField,"class_get_field_from_name");DS_EXPORT(staticFieldValue,"field_static_get_value");
#undef DS_EXPORT
        return true;
    }
    bool Call(void* object,const char* name,int count,void** arguments=nullptr,void** result=nullptr) {
        if(!object || !objectClass || !findMethod || !invoke)return false;
        const void* method=findMethod(objectClass(object),name,count); if(!method)return false;
        void* exception=nullptr; void* value=invoke(method,object,arguments,&exception);
        if(result)*result=value; return !exception;
    }
    void* Class(const char* space,const char* name) {
        if(!domain)return nullptr;
        size_t count=0;const void** list=assemblies(domain(),&count);
        for(size_t i=0;list && i<count;++i)if(void* klass=findClass(image(list[i]),space,name))return klass;
        return nullptr;
    }
    void* Type(const char* space,const char* name) {void* klass=Class(space,name);return klass?typeObject(classType(klass)):nullptr;}
    bool Static(void* klass,const char* name,void** result) {
        if(!klass || !findMethod || !invoke)return false;
        const void* method=findMethod(klass,name,0);if(!method)return false;
        void* exception=nullptr;*result=invoke(method,nullptr,nullptr,&exception);return !exception;
    }
    bool StaticField(void* klass,const char* name,void** result) {
        if(!klass || !findField || !staticFieldValue)return false;
        void* field=findField(klass,name);if(!field)return false;
        *result=nullptr;staticFieldValue(field,result);return *result!=nullptr;
    }
};
struct ManagedRoot {
    ManagedRuntime* runtime=nullptr; uintptr_t handle=0;
    ManagedRoot()=default;
    ManagedRoot(ManagedRuntime& api,void* object):runtime(&api),handle(object && api.rootNew?api.rootNew(object,false):0){}
    ManagedRoot(const ManagedRoot&)=delete;ManagedRoot& operator=(const ManagedRoot&)=delete;
    ManagedRoot(ManagedRoot&& other)noexcept:runtime(other.runtime),handle(other.handle){other.handle=0;}
    ManagedRoot& operator=(ManagedRoot&& other)noexcept {Reset();runtime=other.runtime;handle=other.handle;other.handle=0;return *this;}
    ~ManagedRoot(){Reset();}
    void Reset(){if(handle && runtime && runtime->rootFree)runtime->rootFree(handle);handle=0;}
    void* Get()const{return handle && runtime && runtime->rootTarget?runtime->rootTarget(handle):nullptr;}
};
