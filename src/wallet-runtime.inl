// Invoke native balance APIs on DataController.Update's main thread. Requests
// capture the chosen row and target before the UI can change its selection.
bool LoadWallet(void* controller,const ds::WalletEntry& entry,int& balance) {
    if(entry.mode==ds::WalletMode::Gold) {
        void* player=getPlayerData?getPlayerData(nullptr):nullptr;ObscuredIntBytes value{};
        if(!player || !decodeMovementInt || !Field(player,0x1e4,value))return false;
        balance=decodeMovementInt(&value,nullptr);return true;
    }
    int argument=entry.argument,index=entry.index;void* args[]={&argument,&index};
    const int argc=entry.mode==ds::WalletMode::Period?2:(entry.mode==ds::WalletMode::Coupon || entry.mode==ds::WalletMode::Hunting?1:0);
    void* boxed=nullptr;
    return artifactApi.Call(controller,entry.load,argc,args,&boxed) && Field(boxed,0x10,balance);
}
bool SaveWallet(void* controller,const ds::WalletEntry& entry,int current,int target) {
    if(target<0)return false;
    int value=ds::WalletValue(target,current,entry.mode),argument=entry.argument,index=entry.index;bool option=false;
    void* args[]={&value,&option};int argc=1;
    if(entry.mode==ds::WalletMode::DeltaBool || entry.mode==ds::WalletMode::AbsoluteBool)argc=2;
    else if(entry.mode==ds::WalletMode::Coupon){args[0]=&argument;args[1]=&value;argc=2;}
    void* eventArgs[]={&argument,&value,&option};
    void* periodArgs[]={&argument,&index,&value,&option};
    if(entry.mode==ds::WalletMode::Hunting)return artifactApi.Call(controller,entry.save,3,eventArgs);
    if(entry.mode==ds::WalletMode::Period)return artifactApi.Call(controller,entry.save,4,periodArgs);
    if(entry.mode==ds::WalletMode::Gold) {
        void* player=getPlayerData?getPlayerData(nullptr):nullptr;
        return artifactApi.Call(player,entry.save,1,args);
    }
    return artifactApi.Call(controller,entry.save,argc,args);
}
DWORD lastWalletRead=0;int lastWalletSelection=-1;
void WalletFailure(int code){ds::Set(shared->walletError,code);ds::Set(shared->walletStatus,-1);}
void ProcessWallet(void* controller) {
    if(!shared)return;
    if(ds::Read(shared->state)!=ds::Ready || !ds::Fresh(GetTickCount(),DWORD(ds::Read(shared->clientHeartbeat)))) {
        ds::Set(shared->walletCommand,0);InterlockedCompareExchange(&shared->walletStatus,0,1);return;
    }
    bool refresh=false;
    if(InterlockedExchange(&shared->walletCommand,0)==1) {
        ds::Set(shared->walletStatus,2);
        const int selected=ds::Read(shared->walletRequestIndex),target=ds::Read(shared->walletRequestTarget);
        if(!ds::ValidWallet(selected) || target<0){WalletFailure(1);return;}
        const auto& entry=ds::Wallets[selected];int before=0,after=0;
        if(!LoadWallet(controller,entry,before)){WalletFailure(entry.mode==ds::WalletMode::Gold?2:3);return;}
        // Resolve the local save object before any balance mutation.
        void* file=nullptr;
        if(entry.mode!=ds::WalletMode::Gold && (!artifactApi.Call(controller,"get_ES3File",0,nullptr,&file) || !file)){WalletFailure(7);return;}
        ManagedRoot fileRoot(artifactApi,file);
        if(file && !fileRoot.handle){WalletFailure(7);return;}
        struct EditingScope {bool old=editingWallet;EditingScope(){editingWallet=true;}~EditingScope(){editingWallet=old;}} scope;
        // At worst INT_MIN -> INT_MAX requires three signed-int increments.
        // Some native APIs clamp an intermediate negative result to zero, so
        // recalculate from the actual balance rather than a predicted sum.
        for(int step=0;;++step) {
            const bool split=ds::WalletUsesDelta(entry.mode) && int64_t(target)-before>INT_MAX;
            if(before!=target && !SaveWallet(controller,entry,before,target)){WalletFailure(4);return;}
            if(!LoadWallet(controller,entry,after)){WalletFailure(5);return;}
            if(after==target)break;
            if(!split || step>=2 || after<=before || after>target){WalletFailure(5);return;}
            before=after;
        }
        ds::Set(shared->walletReadIndex,selected);ds::Set(shared->walletBalance,after);ds::Set(shared->walletReadStatus,1);
        // A failed flush can be retried with the same target without adding the
        // amount again. Rune APIs receive upload=false; event APIs boost=false.
        if(file && (!fileRoot.handle || !artifactApi.Call(fileRoot.Get(),"Sync",0))){WalletFailure(6);return;}
        ds::Set(shared->walletError,0);ds::Set(shared->walletStatus,3);refresh=true;
    }
    const int selected=ds::Read(shared->walletSelection);const DWORD now=GetTickCount();
    if(!ds::Read(shared->panelVisible) || !ds::ValidWallet(selected))return;
    if(!refresh && selected==lastWalletSelection && DWORD(now-lastWalletRead)<500)return;
    lastWalletSelection=selected;lastWalletRead=now;int current=0;
    const bool ready=LoadWallet(controller,ds::Wallets[selected],current);
    ds::Set(shared->walletReadIndex,selected);ds::Set(shared->walletBalance,current);ds::Set(shared->walletReadStatus,ready?1:-1);
}
