#include "catalog.hpp"
#include <algorithm>
#include <cctype>
#include <variant>

namespace rep::ui {
namespace {
struct Json {
    using Object=std::map<std::string,Json>;
    using Array=std::vector<Json>;
    std::variant<std::monostate,std::string,Object,Array> value;
    std::string string()const {if(auto v=std::get_if<std::string>(&value))return *v;return {};}
    const Json& field(const char* name)const {
        static const Json empty;
        if(auto object=std::get_if<Object>(&value)){auto it=object->find(name);if(it!=object->end())return it->second;}
        return empty;
    }
};
class Parser {
    std::string_view text_;size_t pos_=0;
    void spaces(){while(pos_<text_.size()&&std::isspace(static_cast<unsigned char>(text_[pos_])))++pos_;}
    char take(){if(pos_==text_.size())throw Error("truncated skill-name-map JSON");return text_[pos_++];}
    void expect(char c){spaces();if(take()!=c)throw Error("invalid skill-name-map JSON");}
    unsigned hex(){unsigned v=0;for(int i=0;i<4;++i){char c=take();v<<=4;if(c>='0'&&c<='9')v+=c-'0';else if(c>='a'&&c<='f')v+=c-'a'+10;else if(c>='A'&&c<='F')v+=c-'A'+10;else throw Error("invalid JSON unicode escape");}return v;}
    std::string string(){
        expect('"');std::string result;
        for(;;){char c=take();if(c=='"')return result;if(static_cast<unsigned char>(c)<32)throw Error("invalid JSON string");
            if(c!='\\'){result+=c;continue;}c=take();
            switch(c){case '"':case '\\':case '/':result+=c;break;case 'b':result+='\b';break;case 'f':result+='\f';break;case 'n':result+='\n';break;case 'r':result+='\r';break;case 't':result+='\t';break;
            case 'u':{unsigned v=hex();std::wstring w;if(v>=0xd800&&v<=0xdbff){if(take()!='\\'||take()!='u')throw Error("invalid JSON surrogate");unsigned low=hex();if(low<0xdc00||low>0xdfff)throw Error("invalid JSON surrogate");w+=wchar_t(v);w+=wchar_t(low);}else w+=wchar_t(v);result+=utf8(w);break;}
            default:throw Error("invalid JSON escape");}
        }
    }
    Json value(){spaces();if(pos_>=text_.size())throw Error("truncated skill-name-map JSON");char c=text_[pos_];
        if(c=='"')return Json{string()};
        if(c=='{'){++pos_;Json::Object result;spaces();if(pos_<text_.size()&&text_[pos_]=='}'){++pos_;return Json{result};}
            for(;;){auto key=string();expect(':');result.emplace(std::move(key),value());spaces();char end=take();if(end=='}')break;if(end!=',')throw Error("invalid JSON object");}return Json{std::move(result)};}
        if(c=='['){++pos_;Json::Array result;spaces();if(pos_<text_.size()&&text_[pos_]==']'){++pos_;return Json{result};}
            for(;;){result.push_back(value());spaces();char end=take();if(end==']')break;if(end!=',')throw Error("invalid JSON array");}return Json{std::move(result)};}
        size_t begin=pos_;while(pos_<text_.size()&&text_[pos_]!=','&&text_[pos_]!=']'&&text_[pos_]!='}'&&!std::isspace(static_cast<unsigned char>(text_[pos_])))++pos_;
        if(begin==pos_)throw Error("invalid JSON value");return {};
    }
public:
    explicit Parser(std::string_view text):text_(text){if(text_.substr(0,3)=="\xef\xbb\xbf")pos_=3;}
    Json parse(){auto result=value();spaces();if(pos_!=text_.size())throw Error("extra skill-name-map bytes");return result;}
};
std::string key(std::string path){return canonical(std::move(path));}
}
Catalog::Catalog(const std::filesystem::path& nameMap){
    auto bytes=readFile(nameMap);auto json=Parser(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size())).parse();
    auto entries=std::get_if<Json::Array>(&json.field("entries").value);if(!entries)throw Error("skill-name-map entries missing");
    for(const auto& row:*entries){SkillItem item;item.relativePath=row.field("relativePath").string();item.job=row.field("job").string();item.jobZh=row.field("jobZh").string();item.english=row.field("english").string();item.zh=row.field("zh").string();item.vpName=row.field("vpName").string();item.displayZh=row.field("displayZh").string();item.displayEn=row.field("displayEn").string();
        if(auto aliases=std::get_if<Json::Array>(&row.field("aliases").value))for(const auto& alias:*aliases)item.aliases.push_back(alias.string());
        if(!item.relativePath.empty())names_.emplace(key(item.relativePath),std::move(item));
    }
}
void Catalog::scan(const std::filesystem::path& replayRoot){
    items_.clear();matched_=0;std::error_code error;
    if(!std::filesystem::is_directory(replayRoot,error))return;
    for(std::filesystem::recursive_directory_iterator it(replayRoot,std::filesystem::directory_options::skip_permission_denied,error),end;it!=end;it.increment(error)){
        if(error){error.clear();continue;}if(!it->is_regular_file(error)||key(utf8(it->path().extension().wstring()))!=".rep")continue;
        auto relative=utf8(it->path().lexically_relative(replayRoot).generic_wstring());SkillItem item;auto found=names_.find(key(relative));
        if(found!=names_.end()){item=found->second;if(!item.zh.empty())++matched_;}
        else {item.relativePath=relative;auto parent=it->path().lexically_relative(replayRoot);item.job=utf8(parent.begin()->wstring());item.english=utf8(it->path().stem().wstring());item.displayEn=item.english;item.displayZh=utf8(it->path().filename().wstring());item.aliases={item.displayZh,item.english,item.job};}
        item.path=it->path();items_.push_back(std::move(item));
    }
    std::sort(items_.begin(),items_.end(),[](const auto& a,const auto& b){return key(a.relativePath)<key(b.relativePath);});
}
bool Catalog::matches(const SkillItem& item,std::string_view query){
    auto q=key(std::string(query));if(q.empty())return true;
    for(const auto& alias:item.aliases)if(key(alias).find(q)!=std::string::npos)return true;
    for(const auto* field:{&item.relativePath,&item.english,&item.zh,&item.vpName})if(key(*field).find(q)!=std::string::npos)return true;
    return false;
}
std::wstring Catalog::display(const SkillItem& item,bool english){return wide(english?item.displayEn:item.displayZh);}
}
