#include "image_categories.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>

void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("Expected policy case");
    std::string mode=argv[1];rep::ImageVisibilityPolicy policy;
    const std::string background="sprite/map/room/tile.img",monster="sprite/monster/test/body.img",person="sprite/character/test/body.img",effect="sprite/monster/test/effects/flash.img";
    if(mode=="defaults"){
        for(auto path:{background,monster,person,effect})require(!policy.hidden(path),"Defaults must leave every category visible");
        require(rep::normalizeImagePath("///CHARACTER\\TEST\\BODY.IMG")==person,"IMG path normalization must be shared");
        require(rep::normalizeImagePath("monster/test/body.ani").empty(),"Non IMG path must not be classified");
        require(rep::ui::matchesImageCategory("MONSTER\\test\\body.IMG",rep::ui::ImageCategory::Monsters),"Existing UI classifier alias must work");
    }else if(mode=="categories"){
        policy.hideBackground=policy.hideMonsters=policy.hideCharacters=true;
        for(auto path:{background,monster,person,std::string("sprite/npc/test/body.img")})require(policy.hidden(path),"Enabled category must hide future paths");
        require(!policy.hidden(effect),"Monster effects must survive automatic removal");
        require(!policy.hidden("sprite/character/test/effect/hit.img"),"Character effects must survive automatic removal");
        require(!policy.hidden("sprite/custom/frame.img"),"Unclassified IMG must remain visible");
        require(policy.hidden("SPRITE\\MAP\\ROOM\\TILE.IMG"),"Category lookup must normalize input");
    }else if(mode=="manual"){
        policy.hideMonsters=true;policy.setOverride("MONSTER\\TEST\\BODY.IMG",true);
        require(!policy.hidden(monster),"Manual visible must override category removal");
        policy.setOverride(effect,false);require(policy.hidden(effect),"Effects may be hidden manually");
        policy.setCategoryEnabled(rep::ImageCategory::Monsters,false);
        policy.setCategoryEnabled(rep::ImageCategory::Monsters,true);
        require(!policy.hidden(monster)&&policy.hidden(effect),"Category round trip must retain all manual exceptions");
        policy.clearManual();require(policy.hidden(monster)&&!policy.hidden(effect),"REP switch must clear exceptions while keeping category choice");
    }else if(mode=="show_all"){
        policy.hideBackground=policy.hideMonsters=policy.hideCharacters=true;policy.setOverride(effect,false);policy.showAll();
        require(!policy.hideBackground&&!policy.hideMonsters&&!policy.hideCharacters&&policy.overrides.empty(),"Show all must clear categories and manual exceptions");
    }else if(mode=="settings"){
        policy.hideBackground=policy.hideCharacters=true;policy.setOverride(effect,false);std::ostringstream saved;rep::saveImageVisibilitySettings(saved,policy);
        require(saved.str()=="1 0 1","Only three category settings should be saved");
        rep::ImageVisibilityPolicy restored;std::istringstream input(saved.str());require(rep::loadImageVisibilitySettings(input,restored),"Valid choices must load");
        require(restored.hideBackground&&!restored.hideMonsters&&restored.hideCharacters&&restored.overrides.empty(),"Manual exceptions must not persist");
    }else if(mode=="invalid"){
        for(auto text:{"","1","1 0","2 0 0","1 0 1 extra","-1 0 0","true 0 0"}){
            policy.hideBackground=policy.hideMonsters=policy.hideCharacters=true;policy.setOverride(effect,false);std::istringstream input(text);
            require(!rep::loadImageVisibilitySettings(input,policy),"Malformed settings must be rejected");
            require(!policy.hideBackground&&!policy.hideMonsters&&!policy.hideCharacters&&policy.overrides.empty(),"Malformed settings must use all-off defaults");
        }
    }else throw std::runtime_error("Unknown policy case");
    std::cout<<mode<<" passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
