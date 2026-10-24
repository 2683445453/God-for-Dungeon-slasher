// Keep the game's retry delegate alive, and invoke it from DataController.Update
// after the failing callback has unwound. Never repeat the player's click or
// synthesize a successful server response.
using ConnectFail = void(__fastcall*)(void*,void*,int,const void*);
using TimeOut = void(__fastcall*)(void*,void*,const void*);
ConnectFail originalConnectFail=nullptr;
TimeOut originalTimeOut=nullptr;
struct SyncRetry {
    ManagedRoot owner,callback;
    DWORD due=0;
    unsigned attempt=0;
    int code=0;
    bool timeout=false;
    bool inFlight=false;
};
std::vector<SyncRetry> syncRetries;
// A delegate may synchronously fail again; preserve its backoff and defer it
// to a later Update rather than recursively invoking it inside its own call.
thread_local unsigned syncRetryAttempt=0;
DWORD SyncRetryDelay(unsigned attempt) {return 1000u<<std::min(attempt,5u);}
bool RetryableSyncError(int code) {
    return code==2 || code==408 || code==429 || code==500 || code==502 || code==503 || code==504 ||
        code==1110 || code==1123 || code==1199;
}
void SyncPending() {
    if(shared)ds::Set(shared->syncPending,LONG(std::count_if(syncRetries.begin(),syncRetries.end(),[](const auto& i){return !i.inFlight;})));
}
bool SameSyncCallback(void* a,void* b) {
    if(a==b)return true;
    uintptr_t am=0,bm=0;void* at=nullptr;void* bt=nullptr;
    // System.Delegate method_ptr / m_target from this build's metadata.
    // A closure target distinguishes separate purchase/save operations.
    return Field(a,0x10,am) && Field(b,0x10,bm) && am && am==bm &&
        Field(a,0x20,at) && Field(b,0x20,bt) && at==bt;
}
bool QueueSyncRetry(void* owner,void* callback,int code,bool timeout,DWORD now) {
    if(!owner || !callback || !ds::Active(shared,ds::AutoRetry,now))return false;
    for(auto& item:syncRetries)if(SameSyncCallback(item.callback.Get(),callback)) {
        // Duplicate notifications must not delay or multiply the same retry.
        if(item.inFlight) {
            item.inFlight=false;item.attempt=std::min(item.attempt+1,5u);
            item.due=now+SyncRetryDelay(item.attempt);item.code=code;item.timeout=timeout;SyncPending();
        }
        return true;
    }
    // Retain completed callback identities for at most two minutes / 128
    // requests to recognize asynchronous failure and preserve backoff.
    for(size_t i=0;syncRetries.size()>=128 && i<syncRetries.size();)
        if(syncRetries[i].inFlight)syncRetries.erase(syncRetries.begin()+i);else ++i;
    SyncRetry item;
    item.owner=ManagedRoot(artifactApi,owner);item.callback=ManagedRoot(artifactApi,callback);
    if(!item.owner.Get() || !item.callback.Get())return false;
    item.attempt=syncRetryAttempt;item.due=now+SyncRetryDelay(item.attempt);
    item.code=code;item.timeout=timeout;
    syncRetries.push_back(std::move(item));SyncPending();ds::Set(shared->syncError,code);return true;
}
void __fastcall OnConnectFail(void* self,void* callback,int code,const void* method) {
    try {if(RetryableSyncError(code) && QueueSyncRetry(self,callback,code,false,GetTickCount()))return;}catch(...) {}
    originalConnectFail(self,callback,code,method);
}
void __fastcall OnTimeOut(void* self,void* callback,const void* method) {
    try {if(QueueSyncRetry(self,callback,2,true,GetTickCount()))return;}catch(...) {}
    originalTimeOut(self,callback,method);
}
void ProcessSyncRetries(DWORD now) {
    // All producers are native main-thread callbacks, as is this consumer.
    // Restore the original prompt when deliberately disabled so its unfinished
    // operation can still be retried manually; no requests are abandoned.
    for(size_t i=0;i<syncRetries.size();) {
        const bool active=ds::Active(shared,ds::AutoRetry,now);
        if(syncRetries[i].inFlight) {
            if(!active || DWORD(now-syncRetries[i].due)>=120000) {
                syncRetries.erase(syncRetries.begin()+i);SyncPending();
            } else ++i;
            continue;
        }
        if(active && LONG(now-syncRetries[i].due)<0){++i;continue;}
        SyncRetry item=std::move(syncRetries[i]);syncRetries.erase(syncRetries.begin()+i);SyncPending();
        if(!active) {
            if(item.timeout)originalTimeOut(item.owner.Get(),item.callback.Get(),nullptr);
            else originalConnectFail(item.owner.Get(),item.callback.Get(),item.code,nullptr);
            continue;
        }
        const unsigned previous=syncRetryAttempt;syncRetryAttempt=std::min(item.attempt+1,5u);
        // Keep the identity in the queue before invoking, including re-entry.
        item.inFlight=true;item.due=now;void* callback=item.callback.Get();
        syncRetries.push_back(std::move(item));SyncPending();
        const bool invoked=artifactApi.Call(callback,"Invoke",0);
        syncRetryAttempt=previous;
        if(invoked) {
            InterlockedIncrement(&shared->syncRetries);
        } else {
            // Invocation failure is retained for another attempt, not swallowed.
            for(auto& pending:syncRetries)if(pending.callback.Get()==callback && pending.inFlight) {
                pending.inFlight=false;pending.attempt=std::min(pending.attempt+1,5u);
                pending.due=now+SyncRetryDelay(pending.attempt);break;
            }
            SyncPending();break;
        }
        // At most one retry per Update, including synchronous re-entry.
        break;
    }
}
