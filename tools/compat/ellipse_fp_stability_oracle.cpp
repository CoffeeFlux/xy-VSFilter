// SPDX-License-Identifier: GPL-2.0-or-later
// Optional diagnostic, deliberately not a passing compatibility campaign.
// Compile without strict FP flags to check ambient flag repeatability in the
// pinned native initializer after an integer-only allocation-history change.
#include "original_ellipse_storage.h"
static void seed(){errno=EDOM;std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(FE_DIVBYZERO);}
int main(){
    for(int rx=0;rx<=64;++rx)for(int ry=1;ry<=64;++ry){
        ReferenceEllipseStorage::XyEllipse first,second;
        seed();int a=first.init(rx,ry),ea=errno,fa=std::fetestexcept(FE_ALL_EXCEPT);
        seed();int before=std::fetestexcept(FE_ALL_EXCEPT);
        void* allocation=xy_vsfilter_ellipse_owned_create(rx,ry);xy_vsfilter_ellipse_release(allocation);
        if(errno!=EDOM||std::fetestexcept(FE_ALL_EXCEPT)!=before){fprintf(stderr,"Allocation perturbation changed seeded effects\n");return 3;}
        int b=second.init(rx,ry),eb=errno,fb=std::fetestexcept(FE_ALL_EXCEPT);
        if(a!=b||ea!=eb||fa!=fb){
            fprintf(stderr,"Unresolved reference/reference initialization effects rx=%d ry=%d returns=%d/%d errno=%d/%d flags=%d/%d\n",rx,ry,a,b,ea,eb,fa,fb);return 2;
        }
    }
    puts("No reference flag instability observed in this diagnostic; this is not candidate compatibility evidence.");
}
