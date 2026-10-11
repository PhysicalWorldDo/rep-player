#pragma once
#include <string>
#include <string_view>
#include <istream>
#include <ostream>
#include <unordered_map>

namespace rep {
enum class ImageCategory { Background, Monsters, Characters };

inline std::string normalizeImagePath(std::string_view logical){
    std::string path(logical);
    for(auto& ch:path){if(ch=='\\')ch='/';else if(ch>='A'&&ch<='Z')ch=char(ch-'A'+'a');}
    while(!path.empty()&&path.front()=='/')path.erase(path.begin());
    if(!path.ends_with(".img"))return {};
    if(!path.starts_with("sprite/"))path="sprite/"+path;
    return path;
}
inline bool matchesNormalizedImageCategory(std::string_view path,ImageCategory category){
    if(category==ImageCategory::Background)return path.starts_with("sprite/map/")||path.starts_with("sprite/background/");
    if(path.find("/effect/")!=std::string::npos||path.find("/effects/")!=std::string::npos)return false;
    if(category==ImageCategory::Monsters)return path.starts_with("sprite/monster/");
    return path.starts_with("sprite/character/")||path.starts_with("sprite/npc/");
}
// Common asset folders are a first pass. Effects stay visible when hiding
// bodies; a manual checkbox may override either outcome for the current REP.
inline bool matchesImageCategory(std::string_view logical,ImageCategory category){return matchesNormalizedImageCategory(normalizeImagePath(logical),category);}
struct ImageVisibilityPolicy {
    bool hideBackground=false,hideMonsters=false,hideCharacters=false;
    std::unordered_map<std::string,bool> overrides; // true = show, false = hide
    bool categoryEnabled(ImageCategory category)const{
        if(category==ImageCategory::Background)return hideBackground;
        if(category==ImageCategory::Monsters)return hideMonsters;
        return hideCharacters;
    }
    void setCategoryEnabled(ImageCategory category,bool enabled){
        if(category==ImageCategory::Background)hideBackground=enabled;
        else if(category==ImageCategory::Monsters)hideMonsters=enabled;
        else hideCharacters=enabled;
    }
    void setOverride(std::string_view logical,bool visible){auto path=normalizeImagePath(logical);if(!path.empty())overrides[std::move(path)]=visible;}
    void clearManual(){overrides.clear();}
    void showAll(){hideBackground=hideMonsters=hideCharacters=false;clearManual();}
    bool hidden(std::string_view logical)const{
        if(overrides.empty()&&!hideBackground&&!hideMonsters&&!hideCharacters)return false;
        auto path=normalizeImagePath(logical);if(path.empty())return false;
        if(auto found=overrides.find(path);found!=overrides.end())return !found->second;
        return (hideBackground&&matchesNormalizedImageCategory(path,ImageCategory::Background))||
            (hideMonsters&&matchesNormalizedImageCategory(path,ImageCategory::Monsters))||
            (hideCharacters&&matchesNormalizedImageCategory(path,ImageCategory::Characters));
    }
};
inline void saveImageVisibilitySettings(std::ostream& output,const ImageVisibilityPolicy& policy){output<<int(policy.hideBackground)<<' '<<int(policy.hideMonsters)<<' '<<int(policy.hideCharacters);}
inline bool loadImageVisibilitySettings(std::istream& input,ImageVisibilityPolicy& policy){
    policy={};int background,monsters,characters;char trailing;
    if(!(input>>background>>monsters>>characters)||(background!=0&&background!=1)||(monsters!=0&&monsters!=1)||(characters!=0&&characters!=1)||(input>>trailing))return false;
    policy.hideBackground=background==1;policy.hideMonsters=monsters==1;policy.hideCharacters=characters==1;return true;
}
namespace ui {using ::rep::ImageCategory;using ::rep::matchesImageCategory;}
}
