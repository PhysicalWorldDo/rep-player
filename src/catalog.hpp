#pragma once
#include "protocol.hpp"
#include <map>

namespace rep::ui {
struct SkillItem {
    std::filesystem::path path;
    std::string relativePath,job,jobZh,english,zh,vpName,displayZh,displayEn;
    std::vector<std::string> aliases;
};
class Catalog {
    std::map<std::string,SkillItem> names_;
    std::vector<SkillItem> items_;
    size_t matched_=0;
    void loadNames(std::string_view text);
public:
    Catalog();
    explicit Catalog(const std::filesystem::path& nameMap);
    void scan(const std::filesystem::path& replayRoot);
    const std::vector<SkillItem>& items() const {return items_;}
    size_t matchedCount() const {return matched_;}
    static bool matches(const SkillItem& item,std::string_view query);
    static std::wstring display(const SkillItem& item,bool english);
};
}
