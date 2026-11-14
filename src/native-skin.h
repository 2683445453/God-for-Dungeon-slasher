#pragma once
#include <filesystem>
#include <fstream>
#include <vector>
#include <array>

namespace nativeui {
enum Art { Gray, Gold, Slot, SlotBold, Outline, Check, Back, Count };
struct Sprite {
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    float width=0, height=0;
    std::array<float,4> border{}; // Unity: left, bottom, right, top.
};
inline std::array<Sprite,Count> sprites;
inline std::vector<unsigned char> font;
inline float scale=1;
inline void Clear() { for(auto& sprite:sprites)sprite={}; font.clear(); }
inline std::filesystem::path Directory() {
    HMODULE module=nullptr; wchar_t path[32768]{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&Directory),&module);
    GetModuleFileNameW(module,path,DWORD(std::size(path)));
    return std::filesystem::path(path).parent_path();
}
inline bool Load(ID3D11Device* device) {
    Clear();
    std::ifstream input(Directory()/L"native-ui.bin",std::ios::binary);
    auto read=[&](void* out,size_t size) {return bool(input.read(static_cast<char*>(out),std::streamsize(size)));};
    char magic[8]{}; uint32_t count=0;
    if(!read(magic,8) || memcmp(magic,"DSUI0001",8) || !read(&count,4) || count!=Count)return false;
    for(auto& sprite:sprites) {
        uint32_t w=0,h=0,bytes=0;
        if(!read(&w,4)||!read(&h,4)||!read(sprite.border.data(),16)||!read(&bytes,4) ||
           !w||!h||w>1024||h>1024||bytes!=w*h*4) {Clear();return false;}
        for(float value:sprite.border)if(!std::isfinite(value)||value<0||value>std::min(w,h)){Clear();return false;}
        std::vector<unsigned char> pixels(bytes);
        if(!read(pixels.data(),bytes)){Clear();return false;}
        D3D11_TEXTURE2D_DESC desc{}; desc.Width=w;desc.Height=h;desc.MipLevels=1;desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
        desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{pixels.data(),w*4,0};
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        if(FAILED(device->CreateTexture2D(&desc,&data,&texture)) ||
           FAILED(device->CreateShaderResourceView(texture.Get(),nullptr,&sprite.view))) {Clear();return false;}
        sprite.width=float(w);sprite.height=float(h);
    }
    uint32_t length=0;
    if(!read(&length,4)||length<1000||length>32*1024*1024){Clear();return false;}
    font.resize(length);
    if(!read(font.data(),length)){Clear();return false;}
    return true;
}
inline float S(float value) {return value*scale;}
inline ImVec2 Size(float x,float y) {return ImVec2(S(x),S(y));}
inline void Image(Art art,ImVec2 a,ImVec2 b,ImU32 tint=IM_COL32_WHITE) {
    const auto& sprite=sprites[art]; auto* draw=ImGui::GetWindowDrawList();
    if(!sprite.view) {
        if(art==Check) {
            draw->AddLine(ImVec2(a.x,a.y+(b.y-a.y)*.5f),ImVec2(a.x+(b.x-a.x)*.35f,b.y),IM_COL32_WHITE,S(3));
            draw->AddLine(ImVec2(a.x+(b.x-a.x)*.35f,b.y),ImVec2(b.x,a.y),IM_COL32_WHITE,S(3));
        } else {
            draw->AddRectFilled(a,b,art==Gold?IM_COL32(167,123,40,255):IM_COL32(48,47,56,255));
            draw->AddRect(a,b,IM_COL32(160,153,151,255));
        }
        return;
    }
    const float l=std::min(S(sprite.border[0]*2), (b.x-a.x)*.45f);
    const float r=std::min(S(sprite.border[2]*2), (b.x-a.x)*.45f);
    const float t=std::min(S(sprite.border[3]*2), (b.y-a.y)*.45f);
    const float d=std::min(S(sprite.border[1]*2), (b.y-a.y)*.45f);
    const float x[]={a.x,a.x+l,b.x-r,b.x},y[]={a.y,a.y+t,b.y-d,b.y};
    const float u[]={0,sprite.border[0]/sprite.width,1-sprite.border[2]/sprite.width,1};
    const float v[]={0,sprite.border[3]/sprite.height,1-sprite.border[1]/sprite.height,1};
    if(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest)draw->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest);
    for(int j=0;j<3;++j)for(int i=0;i<3;++i)
        if(x[i+1]>x[i]&&y[j+1]>y[j])draw->AddImage((ImTextureID)(intptr_t)sprite.view.Get(),
            ImVec2(x[i],y[j]),ImVec2(x[i+1],y[j+1]),ImVec2(u[i],v[j]),ImVec2(u[i+1],v[j+1]),tint);
    if(ImGui::GetPlatformIO().DrawCallback_SetSamplerLinear)draw->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerLinear);
}
inline void Text(ImVec2 at,const char* text,float size,ImU32 color=IM_COL32_WHITE) {
    auto* draw=ImGui::GetWindowDrawList();const float offset=S(1);
    for(ImVec2 delta:{ImVec2(-offset,0),ImVec2(offset,0),ImVec2(0,-offset),ImVec2(0,offset)})
        draw->AddText(ImGui::GetFont(),size,ImVec2(at.x+delta.x,at.y+delta.y),IM_COL32(0,0,0,255),text);
    draw->AddText(ImGui::GetFont(),size,at,color,text);
}
inline void Corners(ImVec2 a,ImVec2 b) {
    const auto& sprite=sprites[SlotBold];
    auto* draw=ImGui::GetWindowDrawList();
    const float side=S(22),inset=S(4);
    for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
        ImVec2 p(x?b.x-inset-side:a.x+inset,y?b.y-inset-side:a.y+inset);
        if(sprite.view) {
            const float u=sprite.border[x?2:0]/sprite.width;
            const float v=sprite.border[y?1:3]/sprite.height;
            if(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest)draw->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest);
            draw->AddImage((ImTextureID)(intptr_t)sprite.view.Get(),p,ImVec2(p.x+side,p.y+side),
                ImVec2(x?1-u:0,y?1-v:0),ImVec2(x?1:u,y?1:v));
            if(ImGui::GetPlatformIO().DrawCallback_SetSamplerLinear)draw->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerLinear);
        } else {
            ImVec2 corner(x?p.x+side:p.x,y?p.y+side:p.y);
            draw->AddLine(corner,ImVec2(x?p.x:p.x+side,corner.y),IM_COL32(220,205,176,255),S(3));
            draw->AddLine(corner,ImVec2(corner.x,y?p.y:p.y+side),IM_COL32(220,205,176,255),S(3));
        }
    }
}
inline bool Button(const char* label,ImVec2 size,bool accent=false) {
    ImVec2 a=ImGui::GetCursorScreenPos(),b(a.x+size.x,a.y+size.y);
    bool hover=ImGui::IsMouseHoveringRect(a,b);
    Image(accent?Gold:Gray,a,b,hover?IM_COL32_WHITE:IM_COL32(220,220,220,255));
    ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(1,1,1,.08f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,ImVec4(0,0,0,.15f));
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(0,0,0,0));
    bool clicked=ImGui::Button(label,size);ImGui::PopStyleColor(4);
    ImVec2 text=ImGui::CalcTextSize(label);
    Text(ImVec2(a.x+(size.x-text.x)/2,a.y+(size.y-text.y)/2),label,ImGui::GetFontSize());
    return clicked;
}
}
