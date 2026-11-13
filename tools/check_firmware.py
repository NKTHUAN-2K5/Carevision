"""Host checks: streamed WAV/base64 and embedded JavaScript. No test framework."""
import base64
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="carevision-") as directory:
    folder = pathlib.Path(directory)
    (folder / "Arduino.h").write_text('''#pragma once
#include <string>
#include <cstdint>
#include <cstring>
using String = std::string;
class Stream { public: virtual int available()=0; virtual int peek()=0;
virtual int read()=0; virtual void flush()=0; virtual size_t write(uint8_t)=0; };
''')
    (folder / "check.cpp").write_text('''#include "gemini_stream.h"
#include <cassert>
#include <iostream>
int main() {
  String head="PREFIX", tail="SUFFIX";
  for (unsigned n=0;n<8;++n) {
    uint8_t pcm[8]={0,1,2,3,4,5,6,7};
    GeminiStream stream(head,pcm,n,tail);
    std::string out;
    while(stream.available()) { int a=stream.peek(); assert(a==stream.peek()); assert(a==stream.read()); out+=char(a); }
    assert(stream.read()==-1); assert(out.size()==stream.length());
    std::cout<<out<<"\\n";
  }
}''')
    binary = folder / "check"
    subprocess.run(["g++", "-std=c++11", "-I" + directory, "-I" + str(ROOT / "include"), str(folder / "check.cpp"), "-o", str(binary)], check=True)
    lines = subprocess.check_output([str(binary)], text=True).splitlines()
    import struct
    for n, line in enumerate(lines):
        expected = b""
        if n:
            pcm = bytes(range(n))
            expected = struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", n+36, b"WAVE", b"fmt ", 16, 1, 1, 8000, 16000, 2, 16, b"data", n) + pcm
        assert line == "PREFIX" + base64.b64encode(expected).decode() + "SUFFIX", n
    source = (ROOT / "include/web_ui.h").read_text()
    script = source.split("<script>", 1)[1].split("</script>", 1)[0]
    javascript = folder / "ui.js"
    javascript.write_text(script)
    subprocess.run(["node", "--check", str(javascript)], check=True)
    (folder / "vad.cpp").write_text('''#include "voice_activity.h"
#include <cassert>
int main(){ VoiceActivity v; for(int i=0;i<200;++i) assert(!v.update(100));
for(int i=0;i<4;++i) assert(!v.update(1500)); assert(v.update(1500));
for(int i=0;i<49;++i){v.update(100);assert(!v.finished());}
v.update(100);assert(v.finished());v.reset();assert(!v.active); }
''')
    subprocess.run(["g++", "-I" + str(ROOT / "include"), str(folder / "vad.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: streamed WAV/base64 (all padding cases), UI JavaScript syntax")
