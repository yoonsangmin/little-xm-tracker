#include "model/SampleTools.hpp"
#include "io/Codec.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
int checks=0;
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class Fn> void reject(Fn fn,const char* message){bool failed=false;try{fn();}catch(const std::exception&){failed=true;}check(failed,message);}
brick::Sample pcm(std::initializer_list<std::int16_t> values){brick::Sample s;s.pcm=values;return s;}
void sampleTools() {
    using namespace brick;
    auto resampled=pcm({1000,-1000,2000,-2000});resampled.loopType=1;resampled.loopStart=1;resampled.loopLength=2;
    edit::resampleSample(resampled,12);
    check(resampled.pcm==std::vector<std::int16_t>({1000,1000,-1000,-1000,2000,2000,-2000,-2000}),"One octave up doubles stored PCM using FT2 nearest-neighbour");
    check(resampled.relativeNote==12&&resampled.loopStart==2&&resampled.loopLength==4,"Retuning and loop scaling compensate for the new PCM rate");
    edit::resampleSample(resampled,-12);
    check(resampled.pcm==std::vector<std::int16_t>({1000,-1000,2000,-2000})&&resampled.relativeNote==0,"Exact octave resampling round trip retains the fixture");
    const auto before=resampled.pcm;resampled.relativeNote=127;
    reject([&]{edit::resampleSample(resampled,1);},"Relative-note overflow must fail");
    check(resampled.pcm==before&&resampled.relativeNote==127,"Rejected resample preserves PCM and tuning");
    auto shortLoop=pcm({100,200});shortLoop.loopType=1;shortLoop.loopLength=1;
    reject([&]{edit::resampleSample(shortLoop,-12);},"Resampling cannot silently disable a collapsed loop");

    auto crossfade=pcm({1000,2000,3000,4000,-4000,-3000,-2000,-1000});
    crossfade.loopType=1;crossfade.loopStart=2;crossfade.loopLength=4;
    edit::crossfadeLoop(crossfade,{1,4});
    check(crossfade.pcm[1]==crossfade.pcm[5]&&crossfade.pcm[2]==crossfade.pcm[6]&&crossfade.pcm[3]==crossfade.pcm[7],"Loop boundary neighbourhoods share the blended transition");
    check(crossfade.pcm[0]==1000&&crossfade.pcm[4]==-4000&&crossfade.loopStart==2&&crossfade.loopLength==4,"Crossfade keeps outside PCM and loop geometry");
    const auto faded=crossfade.pcm;
    reject([&]{edit::crossfadeLoop(crossfade,{1,5});},"Missing post-loop counterpart is rejected");
    check(crossfade.pcm==faded,"Rejected crossfade preserves original PCM");

    auto mixed=pcm({1000,2000,-2000});edit::mixPcm(mixed,1,{{-2000,2000,4000},true},50);
    check(mixed.pcm==std::vector<std::int16_t>({1000,0,0,2000}),"Equal mix cancels opposite samples and extends with scaled source PCM");
    const auto mixBefore=mixed.pcm;edit::mixPcm(mixed,0,{{1000},true},0);
    check(mixed.pcm==mixBefore,"Zero source balance is a no-op");
    auto eight=pcm({0,256});eight.sixteenBit=false;
    edit::drawPcmPoint(eight,0,32767);edit::drawPcmPoint(eight,1,-32768);
    check(eight.pcm[0]==32512&&eight.pcm[1]==-32768,"Drawing retains canonical signed 8-bit quantization");
    reject([&]{edit::drawPcmPoint(eight,2,0);},"Drawing beyond PCM is rejected");

    auto stroke=pcm({800,0,0,0,0,0,900});stroke.loopType=1;stroke.loopStart=1;stroke.loopLength=5;
    stroke.relativeNote=7;stroke.finetune=-23;
    edit::drawPcmStroke(stroke,1,3,5,4000);
    check(stroke.pcm==std::vector<std::int16_t>({800,0,2000,4000,2000,0,900}),
          "Coarse drawing updates both linear spans in one operation and keeps outside PCM");
    check(stroke.loopStart==1&&stroke.loopLength==5&&stroke.relativeNote==7&&stroke.finetune==-23,
          "Coarse drawing preserves length, loop and tuning");
    const auto strokeBefore=stroke.pcm;
    reject([&]{edit::drawPcmStroke(stroke,1,5,7,4000);},"Invalid coarse drawing bounds are rejected");
    check(stroke.pcm==strokeBefore,"Rejected coarse drawing is transactional");
    edit::drawPcmStroke(stroke,3,3,3,-123);
    check(stroke.pcm[3]==-123&&stroke.pcm[2]==2000&&stroke.pcm[4]==2000,
          "Frame resolution edits only the chosen PCM value");

    Sample preset;preset.name="Preserved";preset.pcm.assign(10,1234);preset.sixteenBit=true;
    preset.loopType=2;preset.loopStart=1;preset.loopLength=8;preset.relativeNote=-12;preset.finetune=37;
    edit::fillWavePreset(preset,{1,9},edit::WavePreset::Sine);
    check(preset.pcm.front()==1234&&preset.pcm.back()==1234&&preset.pcm[1]==0&&preset.pcm[3]==16384&&preset.pcm[7]==-16384,
          "Sine preset fills exactly one period inside the half-open selection");
    check(preset.pcm.size()==10&&preset.name=="Preserved"&&preset.loopType==2&&preset.loopStart==1&&preset.loopLength==8&&
          preset.relativeNote==-12&&preset.finetune==37&&preset.sixteenBit,
          "Applying a preset never resizes or rewrites metadata on an existing sample");
    edit::fillWavePreset(preset,{1,9},edit::WavePreset::Triangle);
    check(preset.pcm[1]==-16384&&preset.pcm[3]==0&&preset.pcm[5]==16384,"Triangle preset has the expected quarter-cycle shape");
    edit::fillWavePreset(preset,{1,9},edit::WavePreset::Square);
    check(preset.pcm[1]==16384&&preset.pcm[4]==16384&&preset.pcm[5]==-16384,"Square preset changes sign halfway through the period");
    edit::fillWavePreset(preset,{1,9},edit::WavePreset::Saw);
    check(preset.pcm[1]==-16384&&preset.pcm[5]==0&&preset.pcm[8]>0,"Saw preset rises across one period");
    edit::fillWavePreset(preset,{1,9},edit::WavePreset::Noise);
    const auto noise=preset.pcm;
    edit::fillWavePreset(preset,{1,9},edit::WavePreset::Noise);
    check(preset.pcm==noise,"Noise preset is deterministic so preview/reapply cannot vary accidentally");
    check(std::any_of(noise.begin()+2,noise.begin()+9,[&](auto value){return value!=noise[1];}),"Noise preset is not a constant waveform");
    reject([&]{edit::fillWavePreset(preset,{3,3},edit::WavePreset::Sine);},"Empty preset selection is rejected");
    reject([&]{edit::fillWavePreset(preset,{1,9},static_cast<edit::WavePreset>(99));},"Unknown preset is rejected");
    check(preset.pcm==noise,"Rejected preset requests preserve original PCM");
    edit::fillWavePreset(preset,{1,9},edit::WavePreset::Noise,16384,12345u);
    const auto seededNoise=preset.pcm;
    edit::fillWavePreset(preset,{1,9},edit::WavePreset::Noise,16384,12345u);
    check(preset.pcm==seededNoise,"A chosen Noise seed produces the same PCM on repeated generation");
    edit::fillWavePreset(preset,{1,9},edit::WavePreset::Noise,16384,12346u);
    check(preset.pcm!=seededNoise,"A newly selected Noise seed produces a different waveform");
    check(preset.pcm.front()==1234&&preset.pcm.back()==1234&&preset.loopStart==1&&preset.loopLength==8,
          "Changing Noise seeds preserves PCM outside the selection and loop metadata");
    Instrument noiseInstrument;noiseInstrument.samples.push_back(preset);
    check(io::decodeXi(io::encodeXi(noiseInstrument)).samples[0].pcm==preset.pcm,
          "A generated Noise waveform is ordinary PCM and survives XI save and reload unchanged");
    Sample eightPreset;eightPreset.sixteenBit=false;eightPreset.pcm.assign(32,0);
    for(const auto shape:{edit::WavePreset::Sine,edit::WavePreset::Triangle,edit::WavePreset::Square,edit::WavePreset::Saw,edit::WavePreset::Noise}) {
        edit::fillWavePreset(eightPreset,{0,32},shape);
        edit::drawPcmStroke(eightPreset,0,8,16,12345);
        check(std::all_of(eightPreset.pcm.begin(),eightPreset.pcm.end(),[](auto value){return value%256==0;}),
              "Presets and coarse strokes retain canonical eight-bit quantization");
    }

    auto echo=pcm({1000,0});edit::echoSample(echo,2,2,50,true);
    check(echo.pcm==std::vector<std::int16_t>({1000,0,500,0,250,0}),"An impulse creates independently decayed delay taps");
    auto truncated=pcm({1000,0});edit::echoSample(truncated,1,2,50,false);
    check(truncated.pcm==std::vector<std::int16_t>({1000,500}),"No tail extension retains original sample length");
    const auto echoed=echo.pcm;
    reject([&]{edit::echoSample(echo,32u*1024u*1024u,64,100,true);},"Oversized echo is rejected before allocation");
    check(echo.pcm==echoed,"Rejected echo keeps PCM intact");

    Instrument instrument;instrument.name="MORPH";instrument.samples={pcm({-1000,1000}),pcm({5,6}),pcm({1000,-1000})};
    instrument.noteToSample[0]=1;instrument.fadeout=123;
    const auto first=instrument.samples[0].pcm,last=instrument.samples[2].pcm;
    edit::morphSamples(instrument,0,2);
    check(instrument.samples[1].pcm==std::vector<std::int16_t>({0,0}),"Middle morph slot is the equal PCM blend");
    check(instrument.samples[0].pcm==first&&instrument.samples[2].pcm==last&&instrument.noteToSample[0]==1&&instrument.fadeout==123,"Morph keeps endpoints, mapping and instrument settings");
    auto snapshot=io::encodeXi(instrument);instrument.samples[2].relativeNote=1;
    const auto invalid=io::encodeXi(instrument);
    reject([&]{edit::morphSamples(instrument,0,2);},"Mismatched tuning is rejected");
    check(io::encodeXi(instrument)==invalid,"Rejected morph is transactional across all slots");
    auto restored=io::decodeXi(snapshot);
    check(restored.samples[1].pcm==std::vector<std::int16_t>({0,0}),"Edited multisample instrument survives XI round trip");
}
}
int main(){try{sampleTools();std::cout<<"Sample tools passed: "<<checks<<" checks\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
