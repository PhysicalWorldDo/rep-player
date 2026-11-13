#include "resource_archive.hpp"
#include <algorithm>
#include <atomic>
#include <limits>
#include <unordered_set>
#include <windows.h>
#include <bcrypt.h>

namespace rep {
namespace {
constexpr size_t HeaderSize=20, RecordSize=264, DigestSize=32;
constexpr size_t MaxEntries=65535;
constexpr uint64_t MaxArchiveSize=std::numeric_limits<int32_t>::max();

void checkCancelled(const std::function<bool()>& cancelled){
    if(cancelled&&cancelled())throw Error("NPK export cancelled");
}

bool alreadyExists(DWORD error){return error==ERROR_ALREADY_EXISTS||error==ERROR_FILE_EXISTS;}

class ArchiveFile {
    HANDLE file_=INVALID_HANDLE_VALUE;
    std::filesystem::path temporary_;
    void close(){
        if(file_!=INVALID_HANDLE_VALUE){CloseHandle(file_);file_=INVALID_HANDLE_VALUE;}
    }
public:
    explicit ArchiveFile(const std::filesystem::path& destination){
        static std::atomic<uint64_t> sequence=0;
        auto prefix=L".rep-npk-"+std::to_wstring(GetCurrentProcessId())+L"-";
        do {
            temporary_=destination.parent_path()/(prefix+std::to_wstring(sequence++)+L".tmp");
            file_=CreateFileW(temporary_.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        }while(file_==INVALID_HANDLE_VALUE&&alreadyExists(GetLastError()));
        if(file_==INVALID_HANDLE_VALUE)throw Error("Cannot create temporary NPK in the export directory");
    }
    ArchiveFile(const ArchiveFile&)=delete;
    ArchiveFile& operator=(const ArchiveFile&)=delete;
    ~ArchiveFile(){
        close();
        if(!temporary_.empty())DeleteFileW(temporary_.c_str());
    }
    void seek(uint64_t offset){
        LARGE_INTEGER position{};position.QuadPart=offset;
        if(!SetFilePointerEx(file_,position,nullptr,FILE_BEGIN))throw Error("Cannot seek in temporary NPK");
    }
    void write(std::span<const uint8_t> bytes,const std::function<bool()>& cancelled){
        while(!bytes.empty()){
            checkCancelled(cancelled);
            DWORD written=0;
            auto count=static_cast<DWORD>(std::min<size_t>(bytes.size(),1024*1024));
            if(!WriteFile(file_,bytes.data(),count,&written,nullptr)||!written)throw Error("Cannot write temporary NPK");
            bytes=bytes.subspan(written);
        }
    }
    void publish(const std::filesystem::path& destination,const std::function<bool()>& cancelled){
        close();
        checkCancelled(cancelled);
        if(!MoveFileW(temporary_.c_str(),destination.c_str())){
            if(alreadyExists(GetLastError()))throw Error("NPK export destination already exists");
            throw Error("Cannot save NPK to the export destination");
        }
        temporary_.clear();
    }
};

std::array<uint8_t,DigestSize> indexHash(Bytes& header){
    struct Hash {
        BCRYPT_ALG_HANDLE algorithm=nullptr;
        BCRYPT_HASH_HANDLE handle=nullptr;
        ~Hash(){
            if(handle)BCryptDestroyHash(handle);
            if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
        }
    } hash;
    std::array<uint8_t,DigestSize> digest{};
    // The client hashes only complete 17-byte blocks of the NPK header.
    auto length=static_cast<ULONG>(header.size()/17*17);
    if(BCryptOpenAlgorithmProvider(&hash.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0||
       BCryptCreateHash(hash.algorithm,&hash.handle,nullptr,0,nullptr,0,0)<0||
       BCryptHashData(hash.handle,header.data(),length,0)<0||
       BCryptFinishHash(hash.handle,digest.data(),digest.size(),0)<0)throw Error("Cannot compute the NPK index checksum");
    return digest;
}

void addRecord(Bytes& header,const ResourceArchiveEntry& entry,uint64_t offset){
    constexpr std::string_view prefix="puchikon@neople dungeon and fighter ";
    size_t start=header.size();
    header.resize(start+RecordSize);
    put(header,start,static_cast<uint32_t>(offset));
    put(header,start+4,static_cast<uint32_t>(entry.data.size()));
    for(size_t i=0;i<256;++i){
        uint8_t key=i<prefix.size()?prefix[i]:i<255?"DNF"[(i-prefix.size())%3]:0;
        uint8_t name=i<entry.path.size()?entry.path[i]:0;
        header[start+8+i]=key^name;
    }
}
}

size_t writeResourceArchive(const std::filesystem::path& destination,
    const std::vector<std::string>& paths,
    const std::function<std::optional<ResourceArchiveEntry>(const std::string&)>& load,
    const std::function<void(size_t,size_t)>& progress,
    const std::function<bool()>& cancelled){
    if(paths.empty())throw Error("No resources to export");
    if(paths.size()>MaxEntries)throw Error("Too many resources for one NPK");
    if(std::filesystem::exists(destination))throw Error("NPK export destination already exists");
    checkCancelled(cancelled);

    Bytes header(HeaderSize);
    std::memcpy(header.data(),"NeoplePack_Bill",16);
    header.reserve(HeaderSize+RecordSize*paths.size());
    std::unordered_set<std::string> names;
    ArchiveFile file(destination);
    // Reserve space for the largest possible index so payloads can be written
    // as soon as they are loaded. NPK records store absolute payload offsets.
    uint64_t offset=HeaderSize+RecordSize*paths.size()+DigestSize;
    file.seek(offset);
    for(size_t i=0;i<paths.size();++i){
        checkCancelled(cancelled);
        auto entry=load(paths[i]);
        if(entry&&names.insert(canonical(entry->path)).second){
            if(entry->path.empty()||entry->path.size()>255||entry->path.find('\0')!=std::string::npos)
                throw Error("Invalid NPK resource path: "+entry->path);
            if(entry->data.empty())throw Error("Empty NPK resource: "+entry->path);
            if(entry->data.size()>MaxArchiveSize-offset)throw Error("NPK export exceeds the archive size limit");
            addRecord(header,*entry,offset);
            file.write(entry->data,cancelled);
            offset+=entry->data.size();
        }
        if(progress)progress(i+1,paths.size());
    }
    if(names.empty())throw Error("No resources are available to export");

    put(header,16,static_cast<uint32_t>(names.size()));
    auto digest=indexHash(header);
    file.seek(0);
    file.write(header,cancelled);
    file.write(digest,cancelled);
    file.publish(destination,cancelled);
    return names.size();
}
}
