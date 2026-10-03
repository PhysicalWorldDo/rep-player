#pragma once
#include "protocol.hpp"
#include <optional>
#include <limits>

namespace rep {
struct ClientProtocolSelection {
    std::optional<ProtocolProfile> profile;
    std::optional<unsigned> codePage;
};
inline ProtocolProfile clientProfile(std::wstring_view name) {
    if(name==L"dfo")return ProtocolProfile::Dfo;
    if(name==L"dnf-july")return ProtocolProfile::DnfJuly2026;
    throw Error("Unknown replay profile; use dfo or dnf-july");
}
inline unsigned clientCodePage(std::wstring_view text) {
    size_t used=0;
    if(text.empty()||text.front()==L'-')throw Error("Invalid resource codepage");
    auto value=std::stoul(std::wstring(text),&used,10);
    if(used!=text.size()||value>std::numeric_limits<unsigned>::max())throw Error("Invalid resource codepage");
    return unsigned(value);
}
inline ReplayOptions clientReplayOptions(const std::filesystem::path& client,const ClientProtocolSelection& selection={}) {
    ReplayOptions options;
    if(selection.profile)options.profile=*selection.profile;
    else {
        const bool dfo=std::filesystem::is_regular_file(client/L"DFO.exe");
        const bool dnf=std::filesystem::is_regular_file(client/L"DNF.exe");
        if(dfo&&dnf)throw Error("Client contains both DFO.exe and DNF.exe; select --profile dfo or dnf-july");
        options.profile=dnf?ProtocolProfile::DnfJuly2026:ProtocolProfile::Dfo;
    }
    options.resourceCodePage=selection.codePage;
    return options;
}
}
