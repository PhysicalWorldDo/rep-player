#include "audio_resources.hpp"
#include "resource_archive.hpp"
#include "resources.hpp"
#include <fstream>
#include <iostream>

template<class Reader, class... Arguments>
std::optional<rep::ResourceArchiveEntry> readExport(Reader& reader, Arguments&&... arguments){
    if constexpr(requires { reader.exportEntry(std::forward<Arguments>(arguments)...); })
        return reader.exportEntry(std::forward<Arguments>(arguments)...);
    throw rep::Error("resource export source lookup is unavailable");
}

int wmain(int argc, wchar_t** argv){
    try{
        if(argc<6)throw rep::Error("expected KIND CLIENT PATH PATCHES DESTINATION [FRAME...]");
        const std::wstring_view kind=argv[1];
        const std::filesystem::path client=argv[2];
        const auto path=rep::utf8(argv[3]);
        const bool patches=std::wstring_view(argv[4])==L"1";
        std::optional<rep::ResourceArchiveEntry> entry;
        if(kind==L"img"){
            rep::Assets source(client/L"ImagePacks2",patches);
            rep::Assets exporter(source.index());
            std::vector<int> frames;
            for(int i=6;i<argc;++i)frames.push_back(std::stoi(argv[i]));
            entry=readExport(exporter,path,std::function<bool()>{},std::span<const int>(frames));
        }else if(kind==L"ogg"||kind==L"rejected-ogg"){
            const auto index=rep::AudioResources::buildIndex(client,patches);
            if(kind==L"rejected-ogg"){
                rep::AudioResources playback(index);
                if(!playback.clip("TONE"))throw rep::Error("expected official OGG fallback");
                std::filesystem::copy_file(client/L"valid.npk",client/L"SoundPacks"/L"!patch.npk",
                                           std::filesystem::copy_options::overwrite_existing);
            }
            rep::AudioResources exporter(index);
            entry=readExport(exporter,path);
        }else throw rep::Error("unknown test resource kind");
        if(!entry)return 2;
        std::ofstream output(std::filesystem::path(argv[5]),std::ios::binary);
        output.write(reinterpret_cast<const char*>(entry->data.data()),entry->data.size());
        if(!output)throw rep::Error("test output write failed");
        std::cout<<entry->path<<'\n';
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
