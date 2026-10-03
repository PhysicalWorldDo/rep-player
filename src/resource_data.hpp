#pragma once
#include "protocol.hpp"
#include <windows.h>

namespace rep {
// Named resources are shared by the GUI's catalog and original DXBC loaders.
// Validation tools omit app.rc and use their explicit development file paths.
inline std::span<const uint8_t> resourceData(std::wstring name){
    for(auto& c:name){if(c>=L'a'&&c<=L'z')c-=L'a'-L'A';else if(c==L'.')c=L'_';}
    auto module=GetModuleHandleW(nullptr);auto resource=FindResourceW(module,name.c_str(),RT_RCDATA);
    if(!resource)return {};
    auto size=SizeofResource(module,resource);auto data=LockResource(LoadResource(module,resource));
    if(!data||!size)throw Error("cannot load embedded application resource: "+utf8(name));
    return {static_cast<const uint8_t*>(data),size};
}
}
