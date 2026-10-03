#pragma once
#include <string>
#include <string_view>

namespace rep::ui {
enum class ImageCategory { Background, Monsters, Characters };

// These buttons use common asset folders as a convenient first pass. Effects
// stay visible when hiding bodies, and individual checkboxes can refine it.
inline bool matchesImageCategory(std::string_view logical,ImageCategory category){
    std::string path(logical);
    for(auto& ch:path){if(ch=='\\')ch='/';else if(ch>='A'&&ch<='Z')ch=char(ch-'A'+'a');}
    while(!path.empty()&&path.front()=='/')path.erase(path.begin());
    if(!path.starts_with("sprite/"))path="sprite/"+path;
    if(!path.ends_with(".img"))return false;
    if(category==ImageCategory::Background)return path.starts_with("sprite/map/")||path.starts_with("sprite/background/");
    if(path.find("/effect/")!=std::string::npos||path.find("/effects/")!=std::string::npos)return false;
    if(category==ImageCategory::Monsters)return path.starts_with("sprite/monster/");
    return path.starts_with("sprite/character/")||path.starts_with("sprite/npc/");
}
}
