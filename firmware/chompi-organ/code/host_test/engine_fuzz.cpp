/** Randomized stress test of organEngine.h (the CHOMPI wrapper):
 *  random key starts/stops from both sources (including out-of-range ids and
 *  notes far off the manual), octave/transpose changes while keys are held,
 *  out-of-range parameter values and all-notes-off. Checks that every output
 *  sample is finite and within +/-1, that the engine's idea of which keys are
 *  held matches ours, and that nothing is still sounding after everything is
 *  released.
 *
 *  Build (from this folder):
 *    L=../../../chompi-wave/code/libs
 *    g++ -O1 -g -std=c++14 -w -fsanitize=address,undefined \
 *        -Istub -I$L/libDaisy/src -I$L/DaisySP/Source -I../src \
 *        engine_fuzz.cpp -o engine_fuzz && ./engine_fuzz [seed]
 */
#include <cstdio>
#include <cmath>
#include <random>
#include <cstdlib>
#include "organEngine.h"
static daisysp::Reverb reverb; static myEngine eng;
static float o0[48],o1[48],o2[48],o3[48]; static float* outs[4]={o0,o1,o2,o3};
int main(int argc, char** argv){ std::mt19937 rng(argc > 1 ? atoi(argv[1]) : 1234); int fails=0;
 eng.Init(48000,&reverb); eng.setGain(1.f);
 auto R=[&](int a,int b){ return std::uniform_int_distribution<int>(a,b)(rng); };
 auto F=[&](){ return std::uniform_real_distribution<float>(-.5f,1.5f)(rng); };
 bool held_u[128]={},held_s[128]={};
 for(int it=0; it<200000; it++){
  int op=R(0,22);
  if(op<8){ int id=R(-3,130); bool seq=R(0,1); eng.request_fifo.PushBack(KeyRequest(KeyRequest::Type::START,(float)R(-70,70),id,127,seq?KeyRequest::Source::SEQUENCER:KeyRequest::Source::USER)); if(id>=0&&id<128){ (seq?held_s:held_u)[id]=true; } }
  else if(op<16){ int id=R(-3,130); bool seq=R(0,1); eng.request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP,0,id,127,seq?KeyRequest::Source::SEQUENCER:KeyRequest::Source::USER)); if(id>=0&&id<128){ (seq?held_s:held_u)[id]=false; } }
  else switch(op){
   case 16: eng.setDrawbar(R(-2,10),R(-3,12)); break;
   case 17: eng.setVibrato((uint8_t)R(0,255)); eng.setPercBits((uint8_t)R(0,255)); break;
   case 18: eng.setOctave(R(-3,3)); eng.setTranspose(R(-20,20)); break;
   case 19: eng.setDrive(F()); eng.setReverb(F()); eng.setRotorMix(F()); eng.setRotorBalance(F()); eng.setClick(F()); eng.setLeakage(F()); eng.setExpression(F()); break;
   case 20: eng.setRotorFast(R(0,1)); eng.setBrake(R(0,1)); eng.setSwitchState(R(0,1)); break;
   case 21: if(R(0,200)==0){ eng.stopAllVoices(); for(int i=0;i<128;i++) held_u[i]=held_s[i]=false; } break;
   default: { if(eng.request_fifo.IsEmpty()||R(0,3)==0){ eng.Prepare(); size_t n=(size_t)R(1,48); eng.Process(nullptr,outs,n);
      for(size_t i=0;i<n;i++) if(!std::isfinite(o0[i])||!std::isfinite(o1[i])||fabsf(o0[i])>1.001f||fabsf(o1[i])>1.001f){ if(fails++<5) printf("bad sample %f at it %d\n",o0[i],it);} } }
  }
  if(eng.request_fifo.IsFull()){ eng.Prepare(); }
 }
 eng.Prepare();
 // consistency: engine's sounding state matches what we hold
 int mism=0; for(int i=0;i<128;i++){ bool want=held_u[i]||held_s[i]; if(eng.isKeyPlaying(i)!=want) mism++; }
 printf("held-state mismatches: %d\n",mism); if(mism) fails++;
 // release everything, then output must die out (no stuck notes)
 for(int i=0;i<128;i++){ eng.request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP,0,i,127)); eng.request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP,0,i,127,KeyRequest::Source::SEQUENCER)); if(eng.request_fifo.IsFull()) eng.Prepare(); }
 eng.setReverb(0); float pk=0;
 for(int b=0;b<48000*6/24;b++){ eng.Prepare(); eng.Process(nullptr,outs,24); if(b>48000*5/24) for(int i=0;i<24;i++) pk=fmaxf(pk,fabsf(o0[i])); }
 printf("level 5-6 s after releasing everything: %.6f\n",pk); if(pk>1e-3f) fails++;
 printf(fails?"FAIL (%d)\n":"fuzz OK\n",fails); return fails; }
