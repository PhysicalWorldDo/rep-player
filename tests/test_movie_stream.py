"""Real Neople Video Stream CPU decode and owned payload lifecycle regression."""
import json
import os
from pathlib import Path
import subprocess
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]
CLIENT = Path(os.environ.get("REP_CN_CLIENT", r"E:\WeGameApps\地下城与勇士：创新世纪"))
MOVIES = ("Video/Character/Cutscene_rage/00rage_BG_red.bk2",
          "Video/Character/Cutscene_rage/01_rage_ghost_M_bsk.bk2")
# Independently decoded from July's native Stream reader and corroborated with
# the user-provided neople_video_codec_tool; first/middle/last Bink RGBA frames.
EXPECTED = ((771884, 1377552363, 978611780, 1927767261, 2522649966),
            (769096, 3509860669, 4125987420, 3146778419, 3813243335))

PROBE = r'''
#include "movies.hpp"
#include <iostream>
#include <cmath>

static uint64_t countFiles(const std::filesystem::path& cache) {
    uint64_t result=0;
    for(auto& entry:std::filesystem::directory_iterator(cache))
        if(entry.is_regular_file()&&entry.path().filename()!=L"preserve.txt")result++;
    return result;
}
static uint32_t frameCrc(const std::shared_ptr<rep::Frame>& frame) {
    if(!frame||!frame->texture||frame->texture->rgba.empty())throw rep::Error("movie frame is null or empty");
    return rep::crc(frame->texture->rgba);
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=4)throw rep::Error("usage: movie_stream_probe client logical cache");
        std::filesystem::path cache=argv[3];std::string logical=rep::utf8(argv[2]);
        rep::MovieInfo info;uint32_t first=0,repeat=0,seek=0,last=0,rewind=0,reset=0,payloadCrc=0;
        uint64_t active=0,shared=0,afterStop=0,afterReset=0,afterExit=0,payloadSize=0;
        {
            rep::Movies movies(argv[1],cache);
            first=frameCrc(movies.frame(logical,1,0));info=movies.info(1);active=countFiles(cache);
            for(auto& entry:std::filesystem::directory_iterator(cache))if(entry.path().filename()!=L"preserve.txt"){
                auto bytes=rep::readFile(entry.path());payloadSize=bytes.size();payloadCrc=rep::crc(bytes);
            }
            repeat=frameCrc(movies.frame(logical,1,0));
            auto later=std::max(1u,uint32_t(std::floor(double(std::max(1,info.frames-1))*500./info.rate)));
            seek=frameCrc(movies.frame(logical,1,later));
            last=frameCrc(movies.frame(logical,1,uint32_t(std::ceil((info.frames-1)*1000./info.rate))));
            rewind=frameCrc(movies.frame(logical,1,0));
            frameCrc(movies.frame(logical,2,0));shared=countFiles(cache);
            movies.stop(1);afterStop=countFiles(cache);
            frameCrc(movies.frame(logical,2,later));movies.reset();afterReset=countFiles(cache);
            reset=frameCrc(movies.frame(logical,3,0));
        }
        afterExit=countFiles(cache);
        std::cout<<"{\"width\":"<<info.width<<",\"height\":"<<info.height<<",\"frames\":"<<info.frames<<",\"rate\":"<<info.rate
                 <<",\"bink\":"<<(info.bink?"true":"false")<<",\"first_crc32\":"<<first<<",\"repeat_crc32\":"<<repeat
                 <<",\"seek_crc32\":"<<seek<<",\"last_crc32\":"<<last<<",\"rewind_crc32\":"<<rewind<<",\"reset_crc32\":"<<reset
                 <<",\"payload_bytes\":"<<payloadSize<<",\"payload_crc32\":"<<payloadCrc
                 <<",\"active_files\":"<<active<<",\"shared_files\":"<<shared<<",\"after_stop_files\":"<<afterStop
                 <<",\"after_reset_files\":"<<afterReset<<",\"after_exit_files\":"<<afterExit<<"}\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
'''


class MovieStreamTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / "validation" / "opcode66_20261004" / "movie_stream" / ("cpu_" + uuid.uuid4().hex[:8])
        cls.folder.mkdir(parents=True)
        source = cls.folder / "movie_stream_probe.cpp"
        source.write_text(PROBE, encoding="utf-8")
        cls.probe = cls.folder / "movie_stream_probe.exe"
        compiler = ROOT / "toolchain" / "llvm-mingw-20260616-ucrt-x86_64" / "bin" / "clang++.exe"
        argv = [compiler, "-std=c++20", "-O2", "-DNOMINMAX", "-municode", "-static",
                "-I", ROOT / "src", "-I", ROOT / "vendor" / "zlib", source,
                ROOT / "src" / "movies.cpp", ROOT / "src" / "protocol.cpp",
                ROOT / "vendor" / "zlib" / "libz.a", "-o", cls.probe]
        result = subprocess.run(list(map(str, argv)), capture_output=True, text=True, encoding="utf-8", timeout=60)
        (cls.folder / "compile.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        if result.returncode:
            raise AssertionError("CPU movie probe compilation failed: " + result.stderr)

    def check_movie(self, logical):
        original = CLIENT / logical
        self.assertTrue(original.is_file(), f"Required real stream input is missing: {original}")
        before = original.read_bytes()
        self.assertTrue(before.startswith(b"Neople Video Stream"))
        destination = self.folder / original.stem
        cache = destination / "cache"
        cache.mkdir(parents=True)
        sentinel = cache / "preserve.txt"
        sentinel.write_text("existing file remains", encoding="utf-8")
        argv = [str(self.probe), str(CLIENT), logical, str(cache)]
        result = subprocess.run(argv, capture_output=True, text=True, encoding="utf-8", timeout=45)
        record = {"argv": argv, "returncode": result.returncode, "stdout": result.stdout, "stderr": result.stderr}
        (destination / "result.json").write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding="utf-8")
        self.assertEqual(original.read_bytes(), before)
        self.assertEqual(sentinel.read_text(encoding="utf-8"), "existing file remains")
        self.assertEqual([p.name for p in cache.iterdir()], ["preserve.txt"])
        self.assertEqual(result.returncode, 0, result.stderr)
        got = json.loads(result.stdout)
        self.assertEqual((got["width"], got["height"], got["frames"], got["rate"]), (1067, 250, 74, 60))
        self.assertEqual((got["payload_bytes"], got["payload_crc32"], got["first_crc32"], got["seek_crc32"], got["last_crc32"]),
                         EXPECTED[MOVIES.index(logical)])
        self.assertTrue(got["bink"])
        self.assertEqual(got["first_crc32"], got["repeat_crc32"])
        self.assertEqual(got["first_crc32"], got["rewind_crc32"])
        self.assertEqual(got["first_crc32"], got["reset_crc32"])
        self.assertEqual((got["active_files"], got["shared_files"], got["after_stop_files"]), (1, 1, 1))
        self.assertEqual((got["after_reset_files"], got["after_exit_files"]), (0, 0))

    def test_real_background_stream_decodes_and_releases_owned_files(self):
        self.check_movie(MOVIES[0])

    def test_real_character_stream_decodes_and_releases_owned_files(self):
        self.check_movie(MOVIES[1])


if __name__ == "__main__":
    unittest.main()
