#pragma once
#include "protocol.hpp"

namespace rep {
struct ResourceArchiveEntry {
    std::string path; // Resolved archive path.
    Bytes data;
};

// Writes one NPK with the available, unique resolved resources. A null entry
// is unavailable. Progress counts requested paths and the return value counts
// archive entries. Cancellation and errors leave the destination untouched.
size_t writeResourceArchive(const std::filesystem::path& destination,
    const std::vector<std::string>& paths,
    const std::function<std::optional<ResourceArchiveEntry>(const std::string&)>& load,
    const std::function<void(size_t completed,size_t total)>& progress={},
    const std::function<bool()>& cancelled={});
}
