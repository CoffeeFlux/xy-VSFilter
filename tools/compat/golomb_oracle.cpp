// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_golomb.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <climits>
#include <vector>

static uint64_t comparisons=0,reads=0,copies=0,sequences=0;
struct Readers {
    std::array<BYTE,256> a,b;
    std::array<BYTE,64> out_a,out_b;
    ReferenceGolomb reference;
    CandidateGolomb candidate;
    int base=0;
    Readers(int size,unsigned salt):reference(a.data(),size),candidate(b.data(),size) {
        for(unsigned i=0;i<a.size();++i)a[i]=b[i]=static_cast<BYTE>((i*37+salt)^(salt>>8));
        out_a.fill(0xa5);out_b=out_a;++sequences;check(0,0);
    }
    void check(UINT64 x,UINT64 y) {
        if(x!=y || reference.m_nBitPos!=candidate.m_nBitPos || reference.m_bitlen!=candidate.m_bitlen ||
            reference.m_bitbuff!=candidate.m_bitbuff || reference.GetPos()!=candidate.GetPos() ||
            reference.GetSize()!=candidate.GetSize() || reference.RemainingSize()!=candidate.RemainingSize() ||
            reference.IsEOF()!=candidate.IsEOF() || out_a!=out_b ||
            reference.GetBufferPos()-a.data()!=candidate.GetBufferPos()-b.data()) {
            fprintf(stderr,"Golomb mismatch at operation %llu, sequence %llu\n",
                static_cast<unsigned long long>(comparisons),static_cast<unsigned long long>(sequences));exit(1);
        }
        ++comparisons;
    }
    void reset() {reference.Reset();candidate.Reset();check(0,0);}
    void read(int bits,bool peek) {const UINT64 x=reference.BitRead(bits,peek);const UINT64 y=candidate.BitRead(bits,peek);++reads;check(x,y);}
    void align() {reference.BitByteAlign();candidate.BitByteAlign();check(0,0);}
    void skip(int count) {reference.SkipBytes(count);candidate.SkipBytes(count);check(0,0);}
    void size(int value) {reference.SetSize(value);candidate.SetSize(value);check(0,0);}
    void copy(int count) {reference.ReadBuffer(out_a.data()+8,count);candidate.ReadBuffer(out_b.data()+8,count);++copies;check(0,0);}
    void reset(int offset,int length) {base=offset;reference.Reset(a.data()+offset,length);candidate.Reset(b.data()+offset,length);check(0,0);}
    void mutate(unsigned offset,BYTE value) {a[offset]=b[offset]=value;check(0,0);}
};
static std::vector<BYTE> golomb(unsigned value) {
    const unsigned code=value+1;unsigned width=0;for(unsigned n=code;n;n>>=1)++width;
    std::vector<BYTE> data(16,0xff);unsigned pos=0;
    const auto bit=[&](unsigned n){const unsigned mask=1u<<(7-pos%8);data[pos/8]=static_cast<BYTE>((data[pos/8]&~mask)|(n ? mask : 0));++pos;};
    for(unsigned i=1;i<width;++i)bit(0);
    for(unsigned i=width;i>0;--i)bit((code>>(i-1))&1);
    return data;
}
int main() {
    // Exhaustive short inputs, peek/nonpeek and later reads from cached state.
    for(unsigned byte=0;byte<256;++byte)for(int size=0;size<=4;++size)
        for(int bits=0;bits<=32;++bits)for(bool peek:{false,true}) {
            Readers p(size,byte);p.a[0]=p.b[0]=static_cast<BYTE>(byte);
            p.read(bits,peek);p.read(1,false);p.align();p.read(8,false);p.reset();p.read(32,true);
        }
    // The retained wider path hands its resulting state back to common reads.
    // Never request a zero-bit read with a cached 64-bit word (retained native
    // shift-count behavior, outside this port's supported branch).
    for(int bits=33;bits<=64;++bits)for(int size=0;size<=9;++size)for(bool peek:{false,true}) {
        Readers p(size,0xe5);p.read(bits,peek);p.read(1,false);p.read(16,true);p.read(8,false);
    }
    for(int seed:{1,3,7,8,16,31,32})for(int size=0;size<=32;++size) {
        Readers p(size,0x9812);p.read(seed,true);
        // ReadBuffer uses the underlying byte cursor, ignoring cached bits.
        p.copy(std::min(9,p.reference.RemainingSize()));p.read(7,false);p.align();p.read(8,false);
        p.reset(17,32);p.read(seed,true);p.mutate(17,0x5c);p.read(seed,false);
        p.skip(-p.reference.m_nBitPos);p.read(8,false);
    }
    // Negative logical positions can still refer to valid bytes in the same
    // backing allocation. Preserve the reader's signed position/size rules.
    for(int offset=1;offset<=32;++offset)for(int length:{-1,0,1,32}) {
        Readers p(32,offset);p.reset(64,length);p.skip(-offset);
        p.read(17,true);p.align();p.read(8,false);
        p.skip(-offset-p.reference.m_nBitPos);p.copy(16);
    }
    uint32_t random=0x31a57b29;
    const auto next=[&]() -> uint32_t {random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    for(unsigned trial=0;trial<1000;++trial) {
        Readers p(64,trial);
        for(unsigned command=0;command<256;++command) {
            const unsigned mode=next()%8;
            switch(mode) {
            case 0: {
                const int bits=next()%33;
                // Bound generated refill histories so the pinned C++ shift
                // remains defined under the fatal sanitizer configuration.
                if(p.reference.m_bitlen<bits && static_cast<UINT64>(p.reference.m_bitbuff)>0x7fffff)p.reset();
                p.read(bits,(next()&1)!=0);break;
            }
            case 1:p.align();break;
            case 2:p.skip(static_cast<int>(next()%129)-p.reference.m_nBitPos);break;
            case 3:p.size(static_cast<int>(next()%130)-1);break;
            case 4:if(p.reference.RemainingSize()>=0)p.copy(std::min(static_cast<int>(next()%33),p.reference.RemainingSize()));else p.check(0,0);break;
            case 5:p.reset();break;
            case 6:{const int offset=next()%65,length=next()%129;p.reset(offset,length);break;}
            case 7:{const unsigned offset=p.base+next()%128;const BYTE value=static_cast<BYTE>(next());p.mutate(offset,value);break;}
            }
        }
    }
    // Both signed/unsigned Exp-Golomb entry points still terminate on these
    // complete codewords, and their internal 1-bit reads use the Rust branch.
    for(unsigned value=0;value<65536;++value) {
        const auto encoded=golomb(value);Readers p(static_cast<int>(encoded.size()),0);
        std::copy(encoded.begin(),encoded.end(),p.a.begin());p.b=p.a;
        const UINT64 a=p.reference.UExpGolombRead(),b=p.candidate.UExpGolombRead();p.check(a,b);
        if(a!=value){fprintf(stderr,"Invalid Exp-Golomb fixture\n");return 1;}
        p.reset();p.check(static_cast<UINT64>(p.reference.SExpGolombRead()),static_cast<UINT64>(p.candidate.SExpGolombRead()));
    }
    printf("%llu exact Golomb reader operation comparisons passed (%llu sequences, %llu direct bit reads, %llu buffer copies; cursor, cached bits, EOF, borrowed-byte mutations and complete Exp-Golomb codewords)\n",
        static_cast<unsigned long long>(comparisons),static_cast<unsigned long long>(sequences),
        static_cast<unsigned long long>(reads),static_cast<unsigned long long>(copies));
}
