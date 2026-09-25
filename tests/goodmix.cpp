#include "AnalysisEngine.h"
#include "ReportBuilder.h"
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>
int main(){ double fs=48000; int N=fs*60; std::vector<float> L(N),R(N); std::mt19937 rng(3); std::normal_distribution<double> nd;
 // bed: noise with -3dB/oct extra tilt via one-pole LP chain, partially decorrelated stereo
 double a=0,b=0,c=0,a2=0,ph=0; 
 for(int i=0;i<N;++i){ double t=i/fs,tk=fmod(t,0.469);
  double w1=nd(rng), w2=nd(rng); a=0.995*a+0.05*w1; b=0.995*b+0.05*w2; // brownish
  double wc=0.6*w1+0.4*w2; c = c; 
  double bedL=0.04*(a+0.35*w1), bedR=0.04*(0.7*a+0.3*b+0.35*(0.7*w1+0.3*w2));
  if(tk<1/fs) ph=0; double fk=50+110*exp(-tk*35); ph+=2*M_PI*fk/fs; double kick=0.45*exp(-tk*14)*sin(ph);
  double duck = 1-0.7*exp(-tk*10);
  double bass=0.14*duck*sin(2*M_PI*98*t)+0.03*duck*sin(2*M_PI*196*t);
  L[i]=kick+bass+bedL; R[i]=kick+bass+bedR; }
 pa::AnalysisEngine e; e.prepare(fs); for(int i=0;i<N;i+=512) e.process(&L[i],&R[i],std::min(512,N-i));
 auto m=e.finalize(); auto r=pa::buildReport(m,0);
 printf("score %d TP %.1f LUFS %.1f crest %.1f PLR %.1f LRA %.1f tilt %.2f corr %.2f low %.2f\n",r.score,m.truePeakDb,m.integratedLufs,m.crestDb,m.plr,m.lra,m.tiltDbPerOct,m.correlation,m.lowCorrelation);
 for(auto&f:r.findings) printf("[%d] %s | %s\n",(int)f.status,f.id.c_str(),f.value.c_str());
}
