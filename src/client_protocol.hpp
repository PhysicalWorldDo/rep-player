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
    if(name==L"dnf-compatible")return ProtocolProfile::DnfCompatible;
    throw Error("Unknown replay profile; use dfo, dnf-july or dnf-compatible");
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
        const bool dnf=std::filesystem::is_regular_file(client/L"DNF.exe");
        options.profile=dnf?ProtocolProfile::DnfCompatible:ProtocolProfile::Dfo;
    }
    options.resourceCodePage=selection.codePage;
    return options;
}
}
