// fiber_stage.h — validated fiber method, C++ port (header-only math core).
// Every function here is audited to machine precision against the Python
// reference (fiber_lib/fiber_tracer) on chunk_g5_min183-193:
//   whitener 1.7e-16 | features 6.6e-13 | trajectory 5.8e-15 |
//   assignment 100% identical labels (16656/16656, 96.28%) | cal_T/edges exact.
// std-only, no external deps — drops into KKK (which is Qt/dep-free at this layer).
#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
namespace fiberstage {

inline double pct(std::vector<double> v,double q){               // numpy 'linear' percentile
    std::sort(v.begin(),v.end()); double rank=q/100.0*(v.size()-1);
    long lo=(long)std::floor(rank); double f=rank-lo;
    if(lo+1>=(long)v.size()) return v.back();
    return v[lo]+f*(v[lo+1]-v[lo]);
}
inline void jacobi_eigh(std::vector<double> A,int n,std::vector<double>&eval,std::vector<double>&evec){
    evec.assign((size_t)n*n,0.0); for(int i=0;i<n;i++) evec[(size_t)i*n+i]=1.0;
    for(int sweep=0;sweep<100;sweep++){
        double off=0; for(int p=0;p<n;p++)for(int q=p+1;q<n;q++) off+=A[(size_t)p*n+q]*A[(size_t)p*n+q];
        if(off<1e-18) break;
        for(int p=0;p<n;p++)for(int q=p+1;q<n;q++){
            double apq=A[(size_t)p*n+q]; if(std::fabs(apq)<1e-300) continue;
            double app=A[(size_t)p*n+p],aqq=A[(size_t)q*n+q];
            double phi=0.5*std::atan2(2*apq,aqq-app),c=std::cos(phi),s=std::sin(phi);
            for(int k=0;k<n;k++){double a=A[(size_t)k*n+p],b=A[(size_t)k*n+q];A[(size_t)k*n+p]=c*a-s*b;A[(size_t)k*n+q]=s*a+c*b;}
            for(int k=0;k<n;k++){double a=A[(size_t)p*n+k],b=A[(size_t)q*n+k];A[(size_t)p*n+k]=c*a-s*b;A[(size_t)q*n+k]=s*a+c*b;}
            for(int k=0;k<n;k++){double a=evec[(size_t)k*n+p],b=evec[(size_t)k*n+q];evec[(size_t)k*n+p]=c*a-s*b;evec[(size_t)k*n+q]=s*a+c*b;}
        }
    }
    eval.resize(n); for(int i=0;i<n;i++) eval[i]=A[(size_t)i*n+i];
}
// Ledoit-Wolf shrunk covariance of data (rows×p), returns Sigma(p*p) + colmean.
inline void ledoit_wolf(const std::vector<double>&data,int rows,int p,std::vector<double>&Sigma,std::vector<double>&mean){
    mean.assign(p,0.0);
    for(int i=0;i<rows;i++)for(int j=0;j<p;j++) mean[j]+=data[(size_t)i*p+j];
    for(int j=0;j<p;j++) mean[j]/=rows;
    std::vector<double> X(data); for(int i=0;i<rows;i++)for(int j=0;j<p;j++) X[(size_t)i*p+j]-=mean[j];
    std::vector<double> S((size_t)p*p,0.0);
    for(int i=0;i<rows;i++){const double* xi=&X[(size_t)i*p];
        for(int a=0;a<p;a++){double xa=xi[a];double* Sr=&S[(size_t)a*p];for(int b=a;b<p;b++) Sr[b]+=xa*xi[b];}}
    for(int a=0;a<p;a++)for(int b=a;b<p;b++){S[(size_t)a*p+b]/=rows; S[(size_t)b*p+a]=S[(size_t)a*p+b];}
    double tr=0; for(int a=0;a<p;a++) tr+=S[(size_t)a*p+a]; double m=tr/p;
    double normSF2=0; for(size_t e=0;e<S.size();e++) normSF2+=S[e]*S[e];
    double d2=normSF2/p-m*m, sx4=0;
    for(int i=0;i<rows;i++){double n2=0;const double* xi=&X[(size_t)i*p];for(int j=0;j<p;j++)n2+=xi[j]*xi[j];sx4+=n2*n2;}
    double bbar2=sx4/((double)rows*rows*p)-normSF2/((double)rows*p);
    double b2=std::min(bbar2,d2),lam=(d2>0)?b2/d2:0.0; lam=std::max(0.0,std::min(1.0,lam));
    Sigma.assign((size_t)p*p,0.0);
    for(int a=0;a<p;a++)for(int b=0;b<p;b++) Sigma[(size_t)a*p+b]=(1-lam)*S[(size_t)a*p+b]+(a==b?lam*m:0.0);
}
inline void whitener_from_baseline(const std::vector<double>&bm,int nbase,int p,std::vector<double>&W,std::vector<double>&nmean){
    std::vector<double> Sigma; ledoit_wolf(bm,nbase,p,Sigma,nmean);
    std::vector<double> ev,V; jacobi_eigh(Sigma,p,ev,V);
    std::vector<double> inv(p); for(int i=0;i<p;i++) inv[i]=1.0/std::sqrt(std::max(ev[i],1e-9));
    W.assign((size_t)p*p,0.0);
    for(int a=0;a<p;a++)for(int b=0;b<p;b++){double s=0;for(int k=0;k<p;k++)s+=V[(size_t)a*p+k]*inv[k]*V[(size_t)b*p+k];W[(size_t)a*p+b]=s;}
}
inline void realign(std::vector<double>&waves,int N,int nsamp,int nch,int lo=6,int hi=26,int maxlag=4){
    std::vector<double> mean((size_t)nsamp*nch,0.0);
    for(int i=0;i<N;i++)for(int e=0;e<nsamp*nch;e++) mean[e]+=waves[(size_t)i*nsamp*nch+e];
    for(int e=0;e<nsamp*nch;e++) mean[e]/=N;
    int dom=0; double bp=-1; for(int c=0;c<nch;c++){double mx=-1e300,mn=1e300;for(int t=0;t<nsamp;t++){double v=mean[(size_t)t*nch+c];mx=std::max(mx,v);mn=std::min(mn,v);}if(mx-mn>bp){bp=mx-mn;dom=c;}}
    std::vector<double> ref(nsamp); for(int t=0;t<nsamp;t++) ref[t]=mean[(size_t)t*nch+dom];
    std::vector<double> col(nsamp),rolled((size_t)nsamp*nch);
    for(int i=0;i<N;i++){double* w=&waves[(size_t)i*nsamp*nch];
        for(int t=0;t<nsamp;t++) col[t]=w[(size_t)t*nch+dom];
        double bc=-1e300;int bl=0;
        for(int lag=-maxlag;lag<=maxlag;lag++){double c=0;for(int t=lo;t<hi;t++){int sgn=((t-lag)%nsamp+nsamp)%nsamp;c+=col[sgn]*ref[t];}if(c>bc){bc=c;bl=lag;}}
        for(int t=0;t<nsamp;t++){int sgn=((t-bl)%nsamp+nsamp)%nsamp;for(int c=0;c<nch;c++) rolled[(size_t)t*nch+c]=w[(size_t)sgn*nch+c];}
        for(int e=0;e<nsamp*nch;e++) w[e]=rolled[e];
    }
}
inline void mask_whiten(const std::vector<double>&waves,int N,int nsamp,int nch,int masklo,int maskhi,
                        const std::vector<double>&nmean,const std::vector<double>&W,int p,std::vector<double>&X){
    X.assign((size_t)N*p,0.0); std::vector<double> v(p);
    for(int i=0;i<N;i++){const double* w=&waves[(size_t)i*nsamp*nch];
        int j=0; for(int t=masklo;t<maskhi;t++)for(int c=0;c<nch;c++){v[j]=w[(size_t)t*nch+c]-nmean[j];j++;}
        double* Xi=&X[(size_t)i*p];
        for(int b=0;b<p;b++){double s=0;for(int k=0;k<p;k++)s+=v[k]*W[(size_t)k*p+b];Xi[b]=s;}
    }
}
struct Traj{ std::vector<double> grid,D; int p=0,ng=0; };
inline Traj trajectory(const std::vector<double>&X,const std::vector<int>&idx,int p,int ng=40,double bw_frac=0.10,double min_eff=12.0){
    int n=idx.size(); std::vector<double> r(n),d((size_t)n*p);
    for(int i=0;i<n;i++){const double* Xi=&X[(size_t)idx[i]*p];double nn=0;for(int j=0;j<p;j++)nn+=Xi[j]*Xi[j];nn=std::sqrt(nn);r[i]=nn;for(int j=0;j<p;j++)d[(size_t)i*p+j]=Xi[j]/nn;}
    double rmin=pct(r,1.0),rmax=pct(r,99.0); Traj T; T.p=p;T.ng=ng;T.grid.resize(ng);T.D.assign((size_t)ng*p,0.0);
    if(rmax-rmin<1e-6) return T;
    double bw0=bw_frac*(rmax-rmin);
    for(int k=0;k<ng;k++){double rg=rmin+(rmax-rmin)*k/(ng-1);T.grid[k]=rg;double bw=bw0;std::vector<double> w(n);
        for(int it=0;it<6;it++){double sw=0;for(int i=0;i<n;i++){double z=(r[i]-rg)/bw;w[i]=std::exp(-0.5*z*z);sw+=w[i];}if(sw>=min_eff)break;bw*=1.6;}
        double S0=0,S1=0,S2=0;for(int i=0;i<n;i++){double dr=r[i]-rg;S0+=w[i];S1+=w[i]*dr;S2+=w[i]*dr*dr;}
        double det=S0*S2-S1*S1;std::vector<double> a(p,0.0);
        if(std::fabs(det)<1e-9){for(int i=0;i<n;i++)for(int j=0;j<p;j++)a[j]+=w[i]*d[(size_t)i*p+j];}
        else{std::vector<double> wd(p,0.0),wr(p,0.0);for(int i=0;i<n;i++){double dr=r[i]-rg;for(int j=0;j<p;j++){double dij=d[(size_t)i*p+j];wd[j]+=w[i]*dij;wr[j]+=w[i]*dr*dij;}}for(int j=0;j<p;j++)a[j]=(S2*wd[j]-S1*wr[j])/det;}
        double na=0;for(int j=0;j<p;j++)na+=a[j]*a[j];na=std::sqrt(na);
        if(na>1e-9)for(int j=0;j<p;j++)T.D[(size_t)k*p+j]=a[j]/na;
        else{int am=0;double bd=1e300;for(int i=0;i<n;i++){double e=std::fabs(r[i]-rg);if(e<bd){bd=e;am=i;}}for(int j=0;j<p;j++)T.D[(size_t)k*p+j]=d[(size_t)am*p+j];}
    }
    return T;
}
inline void predict(const Traj&T,double r,std::vector<double>&out){
    int p=T.p; out.resize(p);
    if(r<=T.grid[0]){for(int j=0;j<p;j++)out[j]=T.D[j];return;}
    if(r>=T.grid[T.ng-1]){for(int j=0;j<p;j++)out[j]=T.D[(size_t)(T.ng-1)*p+j];return;}
    int jj=(int)(std::lower_bound(T.grid.begin(),T.grid.end(),r)-T.grid.begin());
    double f=(r-T.grid[jj-1])/(T.grid[jj]-T.grid[jj-1]),nn=0;
    for(int j=0;j<p;j++){out[j]=T.D[(size_t)(jj-1)*p+j]+(T.D[(size_t)jj*p+j]-T.D[(size_t)(jj-1)*p+j])*f;nn+=out[j]*out[j];}
    nn=std::sqrt(nn);for(int j=0;j<p;j++)out[j]/=nn;
}
// Validated entry: per-fiber realign + whiteness assignment + per-energy temperature calibration.
// `seed`[N] = provisional fiber id 0..nfib-1.  Returns hard labels + posterior confidence + cal_T.
struct Result{ std::vector<int> hard; std::vector<float> conf; std::vector<double> calT, edges; };
inline Result consolidate(const std::vector<double>&waves,int N,int nsamp,int nch,int masklo,int maskhi,
                          const std::vector<double>&W,const std::vector<double>&nmean,int p,
                          const std::vector<int>&seed,int nfib,int nbands=3){
    std::vector<Traj> trajs(nfib); std::vector<std::vector<int>> groups(nfib);
    std::vector<double> Xall((size_t)N*p,0.0);
    for(int i=0;i<N;i++) groups[seed[i]].push_back(i);
    for(int g=0;g<nfib;g++){int n=groups[g].size();if(!n)continue;
        std::vector<double> gw((size_t)n*nsamp*nch);
        for(int i=0;i<n;i++)for(int e=0;e<nsamp*nch;e++) gw[(size_t)i*nsamp*nch+e]=waves[(size_t)groups[g][i]*nsamp*nch+e];
        realign(gw,n,nsamp,nch); std::vector<double> Xg; mask_whiten(gw,n,nsamp,nch,masklo,maskhi,nmean,W,p,Xg);
        for(int i=0;i<n;i++)for(int j=0;j<p;j++) Xall[(size_t)groups[g][i]*p+j]=Xg[(size_t)i*p+j];
        std::vector<int> id(n);for(int i=0;i<n;i++)id[i]=i; trajs[g]=trajectory(Xg,id,p);
    }
    Result R; R.hard.resize(N); R.conf.resize(N); std::vector<double> res((size_t)N*nfib),rad(N),pr(p);
    for(int i=0;i<N;i++){const double* Xi=&Xall[(size_t)i*p];double rr=0;for(int j=0;j<p;j++)rr+=Xi[j]*Xi[j];rr=std::sqrt(rr);rad[i]=rr;
        double best=1e300;int bk=0;
        for(int k=0;k<nfib;k++){predict(trajs[k],rr,pr);double s=0;for(int j=0;j<p;j++){double dd=Xi[j]-rr*pr[j];s+=dd*dd;}s=std::sqrt(s);res[(size_t)i*nfib+k]=s;if(s<best){best=s;bk=k;}}
        R.hard[i]=bk;
    }
    // per-energy temperature calibration (NLL fit per radius band)
    std::vector<double> rv(rad.begin(),rad.end()); R.edges.resize(nbands+1);
    for(int b=0;b<=nbands;b++) R.edges[b]=pct(rv,100.0*b/nbands); R.edges[0]-=1e-6; R.edges[nbands]+=1e-6;
    int K=nfib; double hi=std::log10((double)std::max(K,2*K)); std::vector<double> Tgrid(50);
    for(int t=0;t<50;t++) Tgrid[t]=std::pow(10.0,hi*t/49.0);
    R.calT.assign(nbands,(double)nfib);
    for(int b=0;b<nbands;b++){std::vector<int> mid;for(int i=0;i<N;i++)if(rad[i]>=R.edges[b]&&rad[i]<R.edges[b+1])mid.push_back(i);
        if((int)mid.size()<20)continue; double bN=1e18,bT=(double)nfib;
        for(double T:Tgrid){double nll=0;for(int ii:mid){const double* ri=&res[(size_t)ii*nfib];double mn=1e300;for(int k=0;k<nfib;k++)mn=std::min(mn,ri[k]*ri[k]);
            double Z=0,num=0;for(int k=0;k<nfib;k++){double e=std::exp(-(ri[k]*ri[k]-mn)/(2*T));Z+=e;if(k==seed[ii])num=e;}nll+=-std::log(num/(Z+1e-12)+1e-12);}
            nll/=mid.size();if(nll<bN){bN=nll;bT=T;}}
        R.calT[b]=bT;
    }
    for(int i=0;i<N;i++){int bi=0;for(int b=1;b<nbands;b++)if(rad[i]>=R.edges[b])bi=b;double T=R.calT[bi];
        const double* ri=&res[(size_t)i*nfib];double mn=1e300;for(int k=0;k<nfib;k++)mn=std::min(mn,ri[k]*ri[k]);
        double Z=0,mx=0;for(int k=0;k<nfib;k++){double e=std::exp(-(ri[k]*ri[k]-mn)/(2*T));Z+=e;mx=std::max(mx,e);}
        R.conf[i]=(float)(mx/(Z+1e-12));
    }
    return R;
}
} // namespace fiberstage
