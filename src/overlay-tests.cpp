#include "overlay.h"
#include "MinHook.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <iterator>
using Microsoft::WRL::ComPtr;
void OverlaySmokeTest() {
    const wchar_t* name=L"DungeonSlasherTrainer.HiddenWARPTest";
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=DefWindowProcW;cls.lpszClassName=name;
    assert(RegisterClassW(&cls));RECT rect{0,0,960,760};AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW,FALSE);
    HWND window=CreateWindowExW(0,name,L"",WS_OVERLAPPEDWINDOW,0,0,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,cls.hInstance,nullptr);
    assert(window && !IsWindowVisible(window));
    DXGI_SWAP_CHAIN_DESC desc{};desc.OutputWindow=window;desc.BufferCount=1;desc.BufferDesc.Width=960;desc.BufferDesc.Height=760;
    desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Windowed=TRUE;
    desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.SwapEffect=DXGI_SWAP_EFFECT_SEQUENTIAL;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;ComPtr<IDXGISwapChain> chain;
    assert(SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&chain,&device,nullptr,&context)));
    ds::Shared state{};state.state=ds::Ready;state.available=ds::All;state.clientHeartbeat=LONG(GetTickCount());
    state.damageMultiplier=3500;state.goldMultiplier=5000;state.rewardMultiplier=10000;
    state.runePowderMultiplier=5000;state.diamondMultiplier=3000;
    state.sinPoints=73;
    state.unlockStatus=-1;state.unlockError=ds::UnlockSkillType;
    state.artifactSlots=50;state.artifactPhysicalSlots=7;state.artifactAllowed=6;
    state.inventorySlots=50;state.inventoryAllowed=50;state.skillSlots=50;state.skillAllowed=6;
    state.skillInventorySlots=50;state.skillInventoryAllowed=50;
    state.buffAllowed=6;state.buffEquipped=6;state.buffStatus=1;
    state.enabled=ds::God|ds::Damage|ds::Gold|ds::Skins|ds::Pets|ds::BuffCapacity;
    assert(MH_Initialize()==MH_OK && ds::InstallOverlay(&state) && MH_ApplyQueued()==MH_OK);
    assert(state.overlayState==1);state.panelCommand=2;
    ComPtr<ID3D11Texture2D> buffer;ComPtr<ID3D11RenderTargetView> target;
    assert(SUCCEEDED(chain->GetBuffer(0,IID_PPV_ARGS(&buffer))));assert(SUCCEEDED(device->CreateRenderTargetView(buffer.Get(),nullptr,&target)));
    ID3D11RenderTargetView* view=target.Get();context->OMSetRenderTargets(1,&view,nullptr);
    const float clear[]={.06f,.1f,.16f,1};context->ClearRenderTargetView(view,clear);
    assert(SUCCEEDED(chain->Present(0,0)) && state.overlayState==2 && state.panelVisible==1 && state.overlayFrames==1);
    state.clientHeartbeat=LONG(GetTickCount());assert(SUCCEEDED(chain->Present(0,0)) && state.overlayFrames==2);
    ComPtr<ID3D11RenderTargetView> restored;context->OMGetRenderTargets(1,&restored,nullptr);assert(restored.Get()==target.Get());restored.Reset();
    // Inspect the unlock controls below the fold in the actual rendered
    // panel. This remains a hidden WARP fixture, without desktop input.
    auto* panel=ImGui::FindWindowByName("DUNGEON SLASHER  |  F2##trainer");assert(panel && panel->DC.ChildWindows.Size==1);
    auto* common=panel->DC.ChildWindows[0];assert(common->ScrollMax.y>400 && panel->ScrollMax.y==0);
    ImGui::SetScrollY(common,common->ScrollMax.y);context->ClearRenderTargetView(view,clear);state.clientHeartbeat=LONG(GetTickCount());
    assert(SUCCEEDED(chain->Present(0,0))&&state.overlayFrames==3);
    D3D11_TEXTURE2D_DESC texture{};buffer->GetDesc(&texture);texture.Usage=D3D11_USAGE_STAGING;texture.BindFlags=0;texture.CPUAccessFlags=D3D11_CPU_ACCESS_READ;texture.MiscFlags=0;
    ComPtr<ID3D11Texture2D> staging;assert(SUCCEEDED(device->CreateTexture2D(&texture,nullptr,&staging)));context->CopyResource(staging.Get(),buffer.Get());
    D3D11_MAPPED_SUBRESOURCE pixels{};assert(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&pixels)));
    const unsigned width=texture.Width,height=texture.Height;std::vector<unsigned char> bitmap(width*height*4);size_t distinct=0;
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
        auto* source=static_cast<unsigned char*>(pixels.pData)+y*pixels.RowPitch+x*4;size_t at=((height-y-1)*width+x)*4;
        bitmap[at]=source[2];bitmap[at+1]=source[1];bitmap[at+2]=source[0];bitmap[at+3]=255;
        if(source[0]>70 || source[1]>80 || source[2]>100)++distinct;
    }
    context->Unmap(staging.Get(),0);assert(distinct>1000);
    wchar_t executable[32768];GetModuleFileNameW(nullptr,executable,DWORD(std::size(executable)));
    auto file=std::filesystem::path(executable).parent_path()/L"panel-preview.bmp";
    BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+DWORD(bitmap.size());
    BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=LONG(width);info.biHeight=LONG(height);info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;
    std::ofstream out(file,std::ios::binary);out.write(reinterpret_cast<char*>(&header),sizeof(header));out.write(reinterpret_cast<char*>(&info),sizeof(info));out.write(reinterpret_cast<char*>(bitmap.data()),bitmap.size());assert(out.good());out.close();
    state.panelCommand=3;chain->Present(0,0);assert(state.panelVisible==0 && state.overlayFrames==3);
    state.panelCommand=1;state.clientHeartbeat=LONG(GetTickCount());chain->Present(0,0);assert(state.panelVisible==1 && state.overlayFrames==4);
    context->OMSetRenderTargets(0,nullptr,nullptr);target.Reset();buffer.Reset();staging.Reset();
    assert(SUCCEEDED(chain->ResizeBuffers(1,1024,768,DXGI_FORMAT_UNKNOWN,0)));
    state.clientHeartbeat=LONG(GetTickCount());chain->Present(0,0);assert(state.overlayFrames==5);
    ComPtr<IDXGISwapChain1> modern;
    if(SUCCEEDED(chain.As(&modern))) {
        DXGI_PRESENT_PARAMETERS parameters{};state.clientHeartbeat=LONG(GetTickCount());
        assert(SUCCEEDED(modern->Present1(0,0,&parameters)) && state.overlayFrames==6);
    }
    auto frames=state.overlayFrames;
    state.clientHeartbeat=LONG(GetTickCount()-4000);chain->Present(0,0);assert(state.panelVisible==0 && state.overlayFrames==frames);
    modern.Reset();
    // Unity can replace a swap chain for the same HWND, even if the old device
    // remains valid. Recover instead of keeping the stale chain forever.
    ds::RemoveOverlayHooks();assert(MH_Uninitialize()==MH_OK);
    chain.Reset();context.Reset();device.Reset();
    assert(reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window,GWLP_WNDPROC))==DefWindowProcW);
    assert(SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&chain,&device,nullptr,&context)));
    assert(MH_Initialize()==MH_OK && ds::InstallOverlay(&state) && MH_ApplyQueued()==MH_OK);
    state.panelCommand=2;state.clientHeartbeat=LONG(GetTickCount());chain->Present(0,0);assert(state.overlayFrames==frames+1);
    ComPtr<IDXGISwapChain> replacement;
    assert(SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&replacement,nullptr,nullptr,nullptr)));
    state.clientHeartbeat=LONG(GetTickCount());replacement->Present(0,0);assert(state.overlayFrames==frames+2);
    // Select both new tabs in the hidden renderer. No desktop mouse/keyboard
    // or real game process participates in this UI verification.
    ComPtr<ID3D11Device> previewDevice;ComPtr<ID3D11DeviceContext> previewContext;ComPtr<ID3D11Texture2D> previewBuffer,previewStaging;ComPtr<ID3D11RenderTargetView> previewView;
    assert(SUCCEEDED(replacement->GetDevice(IID_PPV_ARGS(&previewDevice))));previewDevice->GetImmediateContext(&previewContext);
    assert(SUCCEEDED(replacement->GetBuffer(0,IID_PPV_ARGS(&previewBuffer))));
    assert(SUCCEEDED(previewDevice->CreateRenderTargetView(previewBuffer.Get(),nullptr,&previewView)));
    D3D11_TEXTURE2D_DESC previewDesc{};previewBuffer->GetDesc(&previewDesc);previewDesc.Usage=D3D11_USAGE_STAGING;previewDesc.BindFlags=0;previewDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;previewDesc.MiscFlags=0;
    assert(SUCCEEDED(previewDevice->CreateTexture2D(&previewDesc,nullptr,&previewStaging)));
    state.challengeNormalChance=1000;state.challengeBossChance=5000;state.challengeQuantity=3;state.enabled|=ds::RuneMaximum|ds::ChallengeRate|ds::ChallengeQuantity;
    state.walletSelection=0;state.walletTargets[0]=100000;state.walletReadIndex=0;state.walletReadStatus=1;state.walletBalance=INT_MIN;
    state.enabled|=ds::NoCooldown|ds::AutoRetry;
    for(int captureIndex:{0,3,1,2}) {
        const int tabIndex=captureIndex==3?0:captureIndex;
        auto* root=ImGui::FindWindowByName("DUNGEON SLASHER  |  F2##trainer");assert(root);
        auto* tabs=ImGui::GetCurrentContext()->TabBars.GetByKey(root->GetID("Features"));assert(tabs && tabs->Tabs.Size==3);
        const auto tabId=tabs->Tabs[tabIndex].ID;tabs->NextSelectedTabId=tabId;
        if(tabIndex==0)for(auto* child:root->DC.ChildWindows)ImGui::SetScrollY(child,captureIndex==3?650.0f:0.0f);
        for(int frame=0;frame<2;++frame){previewContext->ClearRenderTargetView(previewView.Get(),clear);state.clientHeartbeat=LONG(GetTickCount());assert(SUCCEEDED(replacement->Present(0,0)));}
        assert(tabs->SelectedTabId==tabId && root->ScrollMax.y==0);
        previewContext->CopyResource(previewStaging.Get(),previewBuffer.Get());D3D11_MAPPED_SUBRESOURCE previewPixels{};
        assert(SUCCEEDED(previewContext->Map(previewStaging.Get(),0,D3D11_MAP_READ,0,&previewPixels)));
        std::vector<unsigned char> bytes(previewDesc.Width*previewDesc.Height*4);
        for(unsigned y=0;y<previewDesc.Height;++y)for(unsigned x=0;x<previewDesc.Width;++x) {
            auto* from=static_cast<unsigned char*>(previewPixels.pData)+y*previewPixels.RowPitch+x*4;
            const size_t to=((previewDesc.Height-y-1)*previewDesc.Width+x)*4;
            bytes[to]=from[2];bytes[to+1]=from[1];bytes[to+2]=from[0];bytes[to+3]=255;
        }
        previewContext->Unmap(previewStaging.Get(),0);
        auto tabFile=file.parent_path()/(captureIndex==3?L"panel-capacity.bmp":tabIndex==0?L"panel-common.bmp":tabIndex==1?L"panel-runes.bmp":L"panel-wallet.bmp");
        BITMAPFILEHEADER bmp{};bmp.bfType=0x4d42;bmp.bfOffBits=sizeof(bmp)+sizeof(BITMAPINFOHEADER);bmp.bfSize=bmp.bfOffBits+DWORD(bytes.size());
        BITMAPINFOHEADER layout{};layout.biSize=sizeof(layout);layout.biWidth=LONG(previewDesc.Width);layout.biHeight=LONG(previewDesc.Height);layout.biPlanes=1;layout.biBitCount=32;
        std::ofstream capture(tabFile,std::ios::binary);capture.write(reinterpret_cast<char*>(&bmp),sizeof(bmp));capture.write(reinterpret_cast<char*>(&layout),sizeof(layout));capture.write(reinterpret_cast<char*>(bytes.data()),bytes.size());assert(capture.good());
    }
    previewView.Reset();previewStaging.Reset();previewBuffer.Reset();previewContext.Reset();previewDevice.Reset();
    ds::RemoveOverlayHooks();assert(MH_Uninitialize()==MH_OK);
    replacement.Reset();
    assert(reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window,GWLP_WNDPROC))==DefWindowProcW);
    chain.Reset();context.Reset();device.Reset();DestroyWindow(window);UnregisterClassW(name,cls.hInstance);
    std::cout<<"PASS: hidden D3D11 WARP Present/Resize hooks, Chinese panel pixels, render target restore, commands, heartbeat expiry; preview="<<file.string()<<"\n";
}
