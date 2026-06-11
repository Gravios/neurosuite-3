// custody_conformance_test.cpp — locks the chain-of-custody policy in
// custody.hpp (classification, method parsing, anchor parse, class-based
// resolution).  Self-contained (own main, assert-style); built when
// NS_BUILD_TESTS=ON, run via ctest.  The bash (ndm_custody) and Python
// (ndm_resolve_io) mirrors must satisfy the same vectors.
#include "neurosuite/core/custody.hpp"
#include <cstdio>
#include <cstdlib>
using namespace neurosuite::custody;
static int fails=0;
#define CK(cond,msg) do{ if(!(cond)){printf("FAIL: %s\n",msg);++fails;} }while(0)

int main(){
    // classify
    CK(classify("clu")==Klass::MethodSpecific,"clu method-specific");
    CK(classify("fet")==Klass::MethodSpecific,"fet method-specific");
    CK(classify("col")==Klass::MethodSpecific,"col method-specific");
    CK(classify("res")==Klass::Shared,"res shared");
    CK(classify("spk")==Klass::Shared,"spk shared");
    CK(classify("fil")==Klass::SessionWide,"fil session-wide");

    // methodOf
    CK(methodOf("sess.clu.stderiv.5")=="stderiv","methodOf tagged stderiv");
    CK(methodOf("sess.spk.standard.5")=="standard","methodOf tagged standard");
    CK(methodOf("sess.spk.5")=="","methodOf untagged");
    CK(methodOf("a.b.spk.5")=="","methodOf untagged dotted-base (robust)");
    CK(methodOf("/path/to/sirotaA-jg-000005-20120312.clu.stderiv.5")=="stderiv","methodOf with dir + real name");
    CK(methodOf("sess.fil")=="","methodOf session-wide");

    // parseAnchor
    Anchor a=parseAnchor("sess.clu.stderiv.5");
    CK(a.ok&&a.base=="sess"&&a.type=="clu"&&a.method=="stderiv"&&a.group==5&&a.suffix=="","parseAnchor tagged");
    Anchor b=parseAnchor("sess.clu.5");
    CK(b.ok&&b.base=="sess"&&b.method==""&&b.group==5,"parseAnchor untagged");
    Anchor c=parseAnchor("sess.clu.stderiv.5.drift");
    CK(c.ok&&c.method=="stderiv"&&c.group==5&&c.suffix=="drift","parseAnchor suffix");
    Anchor d=parseAnchor("exp.v2.clu.sdiff.8");
    CK(d.ok&&d.base=="exp.v2"&&d.method=="sdiff"&&d.group==8,"parseAnchor dotted base");

    // resolve: create a shared-raw-spk stderiv layout in a temp dir
    char tmpl[]="/tmp/custodyXXXXXX"; char* dir=mkdtemp(tmpl);
    std::string B=std::string(dir)+"/sess";
    auto touch=[](const std::string&p){ std::ofstream(p).put('x'); };
    touch(B+".clu.stderiv.5"); touch(B+".fet.stderiv.5"); touch(B+".pca.stderiv.5");
    touch(B+".spk.5"); touch(B+".res.standard.5"); touch(B+".fil");

    Resolved rs=resolve(B,"spk",5,"stderiv");
    CK(rs.found&&rs.path==B+".spk.5"&&!resolvedIsStderiv(rs),"resolve spk -> shared raw, not stderiv");
    Resolved rr=resolve(B,"res",5,"stderiv");
    CK(rr.found&&rr.path==B+".res.standard.5","resolve res -> .standard fallback");
    Resolved rf=resolve(B,"fet",5,"stderiv");
    CK(rf.found&&rf.path==B+".fet.stderiv.5"&&resolvedIsStderiv(rf),"resolve fet -> strict stderiv");
    Resolved rp=resolve(B,"pca",5,"stderiv");
    CK(rp.found&&rp.path==B+".pca.stderiv.5","resolve pca -> strict stderiv");
    Resolved rw=resolve(B,"fil",5,"stderiv");
    CK(rw.found&&rw.path==B+".fil","resolve fil -> session-wide");
    Resolved rmiss=resolve(B,"clu",9,"stderiv");
    CK(!rmiss.found&&rmiss.path==B+".clu.stderiv.9","resolve missing method-specific -> composed path, not found");

    // tagged spk present -> picks it and is stderiv
    touch(B+".spk.stderiv.7");
    Resolved rt=resolve(B,"spk",7,"stderiv");
    CK(rt.found&&rt.path==B+".spk.stderiv.7"&&resolvedIsStderiv(rt),"resolve spk -> tagged when present (stderiv)");

    if(fails==0) printf("ALL CUSTODY CONFORMANCE TESTS PASS\n");
    return fails;
}
