#include "client_protocol.hpp"
#include <iostream>

// Exercise the same client selection used by preview and export, without GPU
// work or writing to the client tree.
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=3)throw rep::Error("usage: opcode66_protocol_probe CLIENT REP");
        rep::Replay replay(argv[2],rep::clientReplayOptions(argv[1]));
        auto stats=replay.validate();
        uint64_t references=0,commands=0;
        for(const auto& [id,command]:replay.dictionary)
            for(const auto& instruction:command.instructions)
                if(instruction.opcode==66)++commands;
        if(stats.opcodes.size()>66)references=stats.opcodes[66];
        std::cout<<"{\"profile\":\""<<rep::profileName(replay.options.profile)
                 <<"\",\"scenes\":"<<stats.scenes<<",\"references\":"<<stats.references
                 <<",\"aux_bytes\":"<<stats.auxBytes<<",\"timeline_crc32\":"<<stats.timelineCrc
                 <<",\"opcode66_commands\":"<<commands<<",\"opcode66_references\":"<<references
                 <<",\"exact_eof\":"<<(stats.exactEof?"true":"false")<<"}\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
