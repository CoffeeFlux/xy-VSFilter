// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_document.h"
static STSEntry prepared(int id,int64_t start,int64_t end,int order) {
 STSEntry e;
 e.str={static_cast<uint16_t>(id),0,0xd800,0xdcff,0xffff};
 if(id==0)e.str.clear();
 e.style={static_cast<uint16_t>(id%3+1)}; e.actor={0xdc00,static_cast<uint16_t>(id)};
 e.effect={0,0xdfff};e.fUnicode=id%2!=0;
 for(int i=0;i<4;++i)e.marginRect[i]=id*(i+1)-7;
 e.layer=id-3;e.start=start;e.end=end;e.readorder=order;return e;
}
static void units(const std::vector<uint16_t>& v) {std::cout<<' '<<v.size();for(auto x:v)std::cout<<' '<<x;}
static void snapshot(const OriginalDocument& d) {
 std::cout<<d.m_entries.GetCount();
 for(const auto& e:d.m_entries){units(e.str);std::cout<<' '<<e.fUnicode;units(e.style);units(e.actor);units(e.effect);
 for(int x:e.marginRect)std::cout<<' '<<x;
 std::cout<<' '<<e.layer<<' '<<e.start<<' '<<e.end<<' '<<e.readorder;}
 std::cout<<' '<<d.m_segments.GetCount();
 for(const auto& s:d.m_segments){std::cout<<' '<<s.start<<' '<<s.end<<' '<<s.animated<<' '<<s.subs.GetCount();for(int x:s.subs)std::cout<<' '<<x;}
 std::cout<<'\n';
}
int main(){OriginalDocument d,saved;char op;
 while(std::cin>>op){
 if(op=='A'){int mode,id,order;int64_t start,end;std::cin>>mode>>id>>start>>end>>order;auto e=prepared(id,start,end,order);
 d.m_subtitleType=mode==2?EXTVTT:0;if(mode==1)d.EntryOnly(e);else d.AddPrepared(e);}
 else if(op=='Q'){int mode,index,count;int64_t time;std::cin>>mode>>time;bool found;if(mode==0){index=d.SearchSub(time,25.0);found=index>=0;count=d.m_entries.GetCount();}else{found=(mode==1?d.SearchSubs(time,25.0,&index,&count):d.SearchSubs2(time,25.0,&index,&count))!=nullptr;}std::cout<<"Q "<<index<<' '<<found<<' '<<count<<' ';}
 else if(op=='R')d.CreateSegments();
 else if(op=='D')d.RemoveAllEntries();
 else if(op=='C')d.ClearPrepared();
 else if(op=='F'){size_t i;int value;std::cin>>i>>value;d.Feedback(i,value!=0);}
 else if(op=='L'){int64_t bound;std::cin>>bound;if(!d.m_entries.IsEmpty())d.CorrectLast(bound);}
 else if(op=='N'){int old,new_;std::cin>>old>>new_;d.RenamePrepared({static_cast<uint16_t>(old)},{static_cast<uint16_t>(new_)});}
 else if(op=='K'){saved.m_entries.Copy(d.m_entries);saved.m_segments.Copy(d.m_segments);}
 else if(op=='B'){d.m_entries.Copy(saved.m_entries);d.m_segments.Copy(saved.m_segments);}
 else return 2;
 snapshot(d);
 }
 // Callbacks are native observation seams, recorded separately by the runner.
 std::cerr<<d.changed<<' '<<d.logged<<'\n';
}
