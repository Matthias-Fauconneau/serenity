#include "vector.h"
#include "string.h"

#define generic template <template <typename> class V, class T, int N>
#define vector vector<V,T,N>

generic vector vector::operator +=(vector v) { for(int i=0;i<N;i++) u(i)+=v[i]; return *this; }
generic vector vector::operator -=(vector v) { for(int i=0;i<N;i++) u(i)-=v[i]; return *this; }
generic vector vector::operator *=(vector v) { for(int i=0;i<N;i++) u(i)*=v[i]; return *this; }
generic vector vector::operator *=(float s) { for(int i=0;i<N;i++) u(i)*=s; return *this; }
generic vector vector::operator /=(float s) { for(int i=0;i<N;i++) u(i)/=s; return *this; }
generic vector vector::operator -() const { vector r; for(int i=0;i<N;i++) r[i]=-u(i); return r; }
generic vector vector::operator +(vector v) const { vector r; for(int i=0;i<N;i++) r[i]=u(i)+v[i]; return r; }
generic vector vector::operator -(vector v) const { vector r; for(int i=0;i<N;i++) r[i]=u(i)-v[i]; return r; }
generic vector vector::operator *(vector v) const { vector r; for(int i=0;i<N;i++) r[i]=u(i)*v[i]; return r; }
generic vector vector::operator *(float s) const { vector r; for(int i=0;i<N;i++) r[i]=u(i)*s; return r; }
generic vector vector::operator /(float s) const { vector r; for(int i=0;i<N;i++) r[i]=u(i)/s; return r; }
generic bool vector::operator ==(vector v) const { for(int i=0;i<N;i++) if(u(i)!=v[i]) return false; return true; }
generic bool vector::operator !=(vector v) const { for(int i=0;i<N;i++) if(u(i)!=v[i]) return true; return false; }
generic bool vector::operator >(vector v) const { for(int i=0;i<N;i++) if(u(i)<=v[i]) return false; return true; }
generic bool vector::operator <(vector v) const { for(int i=0;i<N;i++) if(u(i)>=v[i]) return false; return true; }
generic bool vector::operator >=(vector v) const { for(int i=0;i<N;i++) if(u(i)<v[i]) return false; return true; }
generic bool vector::operator <=(vector v) const { for(int i=0;i<N;i++) if(u(i)>v[i]) return false; return true; }
generic vector::operator bool() const { for(int i=0;i<N;i++) if(u(i)!=0) return true; return false; }

generic vector operator *(float s, vector v){ return v*s; }
generic vector abs(vector v){ vector r; for(int i=0;i<N;i++) r[i]=abs(v[i]); return r;  }
generic vector min(vector a, vector b){ vector r; for(int i=0;i<N;i++) r[i]=min(a[i],b[i]); return r;  }
generic vector max(vector a, vector b){ vector r; for(int i=0;i<N;i++) r[i]=max(a[i],b[i]); return r;  }
generic vector clip(T min, vector x, T max){ vector r; for(int i=0;i<N;i++) r[i]=clip(min,x[i],max); return r;  }
generic float dot(vector a, vector b) { float l=0; for(int i=0;i<N;i++) l+=a[i]*b[i]; return l; }
generic float length(vector a) { return __builtin_sqrtf(dot(a,a)); }
generic vector normalize(vector a){ return a/length(a); }
generic string str(vector v) {
    string s="("_;
    for(int i=0;i<N;i++) { s<<str(v[i]); if(i<N-1) s<<", "_; }
    return s+")"_;
}
#undef vector
#undef generic

#define uvector(V,T,N) \
    template struct vector<V,T,N>; \
    template vector<V,T,N> operator *(float s, vector<V,T,N> v); \
    template vector<V,T,N> min(vector<V,T,N> a, vector<V,T,N> b); \
    template vector<V,T,N> max(vector<V,T,N> a, vector<V,T,N> b); \
    template vector<V,T,N> clip(T min, vector<V,T,N> x, T max); \
    template float dot( vector<V,T,N> a, vector<V,T,N> b); \
    template float length(vector<V,T,N> a); \
    template vector<V,T,N> normalize(vector<V,T,N> a); \
    template string str(vector<V,T,N> v);

#define vector(V,T,N) \
    uvector(V,T,N) \
    template vector<V,T,N> abs(vector<V,T,N> v);
