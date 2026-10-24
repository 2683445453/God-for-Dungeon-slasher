#include "preferences.h"
#include <filesystem>
void PreferencesTests() {
    wchar_t temporary[MAX_PATH],name[MAX_PATH];assert(GetTempPathW(MAX_PATH,temporary));
    assert(GetTempFileNameW(temporary,L"DSp",0,name));
    const ds::Preferences defaults;assert(ds::ReadPreferences(name)==defaults);
    ds::Preferences expected;expected.enabled=ds::All;
    expected.factors={100,1234,1000000,3750,987654};expected.sin=37;expected.slots={50,50,50,50};
    expected.challenge={0,10000,20};expected.walletSelection=40;
    for(int i=0;i<ds::WalletCount;++i)expected.walletTargets[i]=i==0?INT_MAX:i*73;
    assert(ds::WritePreferences(name,expected));assert(ds::ReadPreferences(name)==expected);
    ds::Shared state{};state.available=ds::All;state.state=ds::Ready;state.clientHeartbeat=LONG(GetTickCount());
    ds::RestorePreferences(&state,ds::ReadPreferences(name));assert(ds::SnapshotPreferences(&state)==expected);
    assert(state.unlockCommand==0 && state.unlockStatus==0 && state.panelVisible==0);
    assert(state.walletCommand==0 && state.walletStatus==0);
    // Closing stops the live hooks after taking the saved snapshot. A fresh
    // controller must still restore the user's flags, including F2-only flags.
    ds::Disable(&state);assert(!state.enabled);assert(ds::ReadPreferences(name)==expected);
    ds::RestorePreferences(&state,ds::ReadPreferences(name));assert((state.enabled&ds::SkillInventoryCapacity)!=0);
    // Intentional all-off is remembered while all numeric values survive.
    ds::Disable(&state);expected.enabled=0;assert(ds::WritePreferences(name,ds::SnapshotPreferences(&state)));
    assert(ds::ReadPreferences(name)==expected);
    // Legacy numeric settings remain compatible, and corrupt / out-of-range
    // settings never enable unknown features or propagate invalid parameters.
    assert(WritePrivateProfileStringW(L"Features",L"Enabled",nullptr,name));
    assert(ds::ReadPreferences(name)==expected);
    assert(WritePrivateProfileStringW(L"Features",L"Enabled",L"2147483647",name));
    assert(WritePrivateProfileStringW(L"Multipliers",L"Damage",L"nan",name));
    assert(WritePrivateProfileStringW(L"Multipliers",L"RunePowder",L"1001",name));
    assert(WritePrivateProfileStringW(L"SkillInventory",L"Slots",L"-1",name));
    assert(WritePrivateProfileStringW(L"Artifacts",L"Slots",L"51",name));
    assert(WritePrivateProfileStringW(L"Skills",L"Slots",L"51",name));
    assert(WritePrivateProfileStringW(L"ArtifactInventory",L"Slots",L"51",name));
    assert(WritePrivateProfileStringW(L"Sin",L"Total",L"999999999999999999999999",name));
    const auto sanitized=ds::ReadPreferences(name);
    assert(sanitized.enabled==(ds::All&~ds::AutoRetry) && sanitized.factors[0]==2000 && sanitized.factors[3]==2000 && sanitized.slots[3]==12 && sanitized.sin==140);
    assert((sanitized.slots==std::array<LONG,4>{12,12,12,12}));
    state.available=ds::God|ds::RunePowder;ds::RestorePreferences(&state,sanitized);
    assert(state.enabled==(ds::God|ds::RunePowder));
    assert(DeleteFileW(name));
    assert(WritePrivateProfileStringW(L"Sync",L"AutoRetry",nullptr,name));
    assert((ds::ReadPreferences(name).enabled&ds::AutoRetry)!=0);
    std::cout<<"PASS: INI preferences for 23 toggles including six buff slots, all four capacities up to 50, legacy quiet-retry default and explicit all-off, multipliers/challenge and 41 wallet targets, malformed values, availability mask, no command replay\n";
}
