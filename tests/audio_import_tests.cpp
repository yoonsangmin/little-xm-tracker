#include "io/AudioImport.hpp"
#include "io/Codec.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using Bytes = std::vector<std::uint8_t>;
int checks = 0;
void check(bool condition, const char* message) {
    ++checks; if (!condition) throw std::runtime_error(message);
}
template<class Fn> void rejects(Fn fn, const char* message) {
    bool caught = false; try { fn(); } catch (const std::runtime_error&) { caught = true; }
    check(caught, message);
}
void word(Bytes& b, std::uint16_t v) { b.push_back(std::uint8_t(v)); b.push_back(std::uint8_t(v >> 8)); }
void dword(Bytes& b, std::uint32_t v) { word(b, std::uint16_t(v)); word(b, std::uint16_t(v >> 16)); }
void text(Bytes& b, const char* value) { for (int i = 0; i < 4; ++i) b.push_back(std::uint8_t(value[i])); }
void patch(Bytes& b, std::size_t at, std::uint32_t value) { for (int i = 0; i < 4; ++i) b[at + std::size_t(i)] = std::uint8_t(value >> (8 * i)); }
Bytes wave(const Bytes& pcm, int bits, int channels = 1, int format = 1, bool extensible = false) {
    Bytes b; text(b,"RIFF"); dword(b,0); text(b,"WAVE"); text(b,"fmt "); dword(b,extensible?40:16);
    word(b,std::uint16_t(extensible?0xfffe:format)); word(b,std::uint16_t(channels)); dword(b,8363);
    dword(b,std::uint32_t(8363*channels*(bits/8))); word(b,std::uint16_t(channels*(bits/8))); word(b,std::uint16_t(bits));
    if (extensible) {
        word(b,22); word(b,std::uint16_t(bits)); dword(b,channels==1?4:3);
        dword(b,std::uint32_t(format)); word(b,0); word(b,0x10);
        b.insert(b.end(),{0x80,0,0,0xaa,0,0x38,0x9b,0x71});
    }
    text(b,"data"); dword(b,std::uint32_t(pcm.size())); b.insert(b.end(),pcm.begin(),pcm.end());
    if (pcm.size()%2) b.push_back(0);
    patch(b,4,std::uint32_t(b.size()-8)); return b;
}
Bytes signedPcm(const std::vector<std::int32_t>& pcm, int bits) {
    Bytes b; for (auto v:pcm) for (int n=0;n<bits/8;++n) b.push_back(std::uint8_t(std::uint32_t(v)>>(8*n))); return b;
}
template<class T> Bytes floatPcm(const std::vector<T>& values) {
    Bytes b; for (const auto v:values) {
        std::array<std::uint8_t,sizeof(T)> raw{}; std::memcpy(raw.data(),&v,sizeof(T));
        b.insert(b.end(),raw.begin(),raw.end());
    } return b;
}

void waveMatrix() {
    using namespace brick::io;
    auto eight=decodeAudio(wave({0,128,255},8));
    check(eight.channels==1&&eight.sourceBits==8&&eight.frames()==3,"8-bit mono metadata");
    check(convertAudio(eight,StereoMode::Blend,8).pcm==std::vector<std::int16_t>({-32768,0,32512}),"8-bit mono roundtrip must preserve zero and endpoints");
    check(convertAudio(eight,StereoMode::Right,16).pcm==std::vector<std::int16_t>({-32768,0,32512}),"mono ignores stereo choice and promotes without bias");
    auto sixteen=decodeAudio(wave(signedPcm({-32768,0,32767},16),16));
    check(convertAudio(sixteen,StereoMode::Left,16).pcm==std::vector<std::int16_t>({-32768,0,32767}),"16-bit mono must preserve exact PCM");
    for (int bits:{24,32}) {
        const auto pcm=bits==24?signedPcm({-8388608,0,4194304},24):signedPcm({std::numeric_limits<std::int32_t>::min(),0,1073741824},32);
        for (bool ext:{false,true}) {
            const auto audio=decodeAudio(wave(pcm,bits,1,1,ext));
            check(audio.sourceBits==bits&&convertAudio(audio,StereoMode::Blend,16).pcm==std::vector<std::int16_t>({-32768,0,16384}),"24/32-bit ordinary/extensible PCM conversion");
        }
    }
    for (bool ext:{false,true}) {
        const auto audio=decodeAudio(wave(floatPcm<float>({-2.f,-.5f,0,.5f,2.f}),32,1,3,ext));
        check(convertAudio(audio,StereoMode::Blend,16).pcm==std::vector<std::int16_t>({-32768,-16384,0,16384,32767}),"float WAV clips finite peaks only on conversion");
    }
    const auto doubles=decodeAudio(wave(floatPcm<double>({-.5,0,.5}),64,1,3));
    check(convertAudio(doubles,StereoMode::Blend,16).pcm==std::vector<std::int16_t>({-16384,0,16384}),"64-bit float WAV");
    const auto stereo=decodeAudio(wave(signedPcm({-32768,0,32512,-16384,8192,16384,-8192,-16384},16),16,2));
    check(convertAudio(stereo,StereoMode::Left,16).pcm==std::vector<std::int16_t>({-32768,32512,8192,-8192}),"left-only mix");
    check(convertAudio(stereo,StereoMode::Right,16).pcm==std::vector<std::int16_t>({0,-16384,16384,-16384}),"right-only mix");
    check(convertAudio(stereo,StereoMode::Blend,16).pcm==std::vector<std::int16_t>({-16384,8064,12288,-12288}),"blend averages unlike stereo channels");
    const auto half=decodeAudio(wave({127,128,128,129},8,2));
    check(convertAudio(half,StereoMode::Blend,16).pcm==std::vector<std::int16_t>({-128,128}),"stereo8 blend retains half-step precision before output quantization");
    const auto low=convertAudio(half,StereoMode::Blend,8);
    check(!low.sixteenBit&&low.pcm==std::vector<std::int16_t>({-256,256}),"explicit8 output rounds after blending");
    check(low.relativeNote==0&&low.finetune==0,"8363 Hz tuning must remain C4");
}

void invalidAudio() {
    using namespace brick::io;
    const auto valid=wave(signedPcm({0,1234,-1234},16),16);
    for (std::size_t n=0;n<valid.size();++n)
        rejects([&]{decodeAudio(Bytes(valid.begin(),valid.begin()+std::ptrdiff_t(n)));},"truncated WAV must fail before import options");
    auto partial=valid; partial.pop_back(); patch(partial,4,std::uint32_t(partial.size()-8)); patch(partial,40,5);
    rejects([&]{decodeAudio(partial);},"partial WAV frame rejected");
    rejects([&]{decodeAudio(wave(signedPcm({0,0,0},16),16,3));},"multichannel source must not silently downmix");
    rejects([&]{decodeAudio(wave({0,0},16,1,6));},"compressed WAV must report unsupported encoding");
    rejects([&]{decodeAudio(wave(floatPcm<float>({std::numeric_limits<float>::quiet_NaN()}),32,1,3));},"NaN WAV rejected");
    rejects([&]{decodeAudio(wave(floatPcm<float>({std::numeric_limits<float>::infinity()}),32,1,3));},"infinite WAV rejected");
    auto brokenGuid=wave(signedPcm({0},32),32,1,1,true); brokenGuid[45]=3;
    rejects([&]{decodeAudio(brokenGuid);},"unknown extensible WAV GUID rejected");
    rejects([&]{decodeAudio({'O','g','g','S',0,1,2,3});},"invalid Ogg stream rejected");
    rejects([&]{decodeAudio({'f','L','a','C',0,1,2,3});},"invalid FLAC stream rejected");
    rejects([&]{decodeAudio({'I','D','3',0,1,2,3});},"invalid MP3 stream rejected");
    rejects([&]{convertAudio(decodeAudio(valid),StereoMode::Blend,24);},"XM output must be8or16");
    check(isAudioExtension(".WAV")&&isAudioExtension(".flac")&&isAudioExtension(".mp3")&&isAudioExtension(".ogg")&&!isAudioExtension(".opus"),"browser extension filter matches supported formats");
}

void compressedFixtures(const std::filesystem::path& directory) {
    using namespace brick::io;
    for(const char* name:{"tone.flac","tone.mp3","tone.ogg"}) {
        const auto audio=loadAudio(directory/name);
        check(audio.channels==2&&audio.sampleRate==44100&&audio.frames()>=4000&&audio.frames()<8000,"compressed metadata and duration");
        const auto left=convertAudio(audio,StereoMode::Left,16), right=convertAudio(audio,StereoMode::Right,16);
        check(left.pcm!=right.pcm&&std::any_of(left.pcm.begin(),left.pcm.end(),[](auto v){return v>1000;}),"compressed decoding retains distinct nonzero stereo PCM");
        const auto compact=convertAudio(audio,StereoMode::Blend,8);
        check(!compact.sixteenBit&&std::all_of(compact.pcm.begin(),compact.pcm.end(),[](auto v){return v%256==0;}),"compressed data converts to valid XM8 PCM");
        brick::Document doc=brick::makeDemoDocument();doc.instruments[0].samples[0]=compact;
        const auto reload=decodeXm(encodeXm(doc));
        check(reload.instruments[0].samples[0].pcm==compact.pcm,"imported compressed sample survives XM save/reload");
    }
}
} // namespace

int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("Pass the tests/fixtures/audio directory");
        waveMatrix();invalidAudio();compressedFixtures(std::filesystem::u8path(argv[1]));
        std::cout<<"Audio import tests passed: "<<checks<<" checks; PCM/float/extensible WAV, FLAC/MP3/Vorbis, 3 mixes, 2 depths, malformed inputs and XM roundtrip.\n";
    } catch(const std::exception& e) {std::cerr<<"Audio import failure: "<<e.what()<<'\n';return 1;}
}
