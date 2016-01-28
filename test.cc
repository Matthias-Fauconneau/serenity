#include "thread.h"
#include "math.h"
#include "simd.h"
#include "plot.h"
#include "window.h"

const float pi=3.141593f;
const float halfpi=1.570796f;
const float quarterpi=0.7853982f;
float atan(float x)
{
    return quarterpi*x - x*(abs(x) - 1)*(0.2447f + 0.0663f*abs(x));
}
float atan2(float y, float x)
{
    if(abs(x)>abs(y)) {
        float atan=::atan(y/x);
        if(x>0.f)
            return atan;
        else
            return y>0.f?atan+pi:atan-pi;
    } else {
        float atan=::atan(x/y);
        return y>0.f?halfpi-atan:-halfpi-atan;
    }
}

struct Test {
 Plot plot;
 unique<Window> window = ::window(&plot, int2(1024));
 Test() {
  plot.dataSets.reserve(7);
  //map<float,float>& A = plot.dataSets.insert("A"__);
  /*map<float,float>& Y = plot.dataSets.insert("Y"__);
  map<float,float>& R = plot.dataSets.insert("R"__);
  map<float,float>& R2 = plot.dataSets.insert("R2"__);
  map<float,float>& A0 = plot.dataSets.insert("0"__);*/
  map<float,float>& A2 = plot.dataSets.insert("2"__);
  /*map<float,float>& X = plot.dataSets.insert("x>0"__);
  map<float,float>& Y = plot.dataSets.insert("y>0"__);
  map<float,float>& XY = plot.dataSets.insert("|x|>|y|"__);*/
  plot.min.y = -1, plot.max.y = 1;
  /*const int N = 4;
        for(int i: range(N)) for(int j: range(N)) {
            float x = ((float)i/N)*2-1, y = (float)j/N;*/
  const int N = 1024;
  for(int i: range(N+1)) {
   //float a = PI/2*i/N-PI/4;
   float a = 2*PI*i/N-PI;
   float x = cos(a), y = sin(a);
   if(0)
   log(str(a)+/*"\t"+str(x)+"\t"+str(y)+"\t"+str(atan(y,x))+"\t"+str(atan(floatX(y),floatX(x))[0])+
     "\t"+str(y/x)+*/"\t"+str(y)+"\t"+str((y/x)*PI/4));
   else {
    /*A.insert(a/PI, a/PI);
    Y.insert(a/PI, y/PI);
    R.insert(a/PI, (y/x)/PI);
    R2.insert(a/PI, ((y/x)*PI/4)/PI);
    A0.insert(a/PI, atan0(floatX(y),floatX(x))[0]/PI);*/
    /*A.insert(a/PI, atan2(y, x)/PI);
    X.insert(a/PI, x>0);
    Y.insert(a/PI, y>0);
    XY.insert(a/PI, abs(x)>abs(y));*/
    A2.insert(a/PI, atan2(floatX(y),floatX(x))[0]/PI);
    //log(str(y-a)+"\t"+str(((y/x)*PI/4)-a));
   }
  }
 }
} test;
