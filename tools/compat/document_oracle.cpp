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
int main(int argc,char** argv){OriginalDocument d,saved;char op;
 std::ofstream replay;
 if(argc==3 && std::string(argv[1])=="--replay-output"){replay.open(argv[2],std::ios::binary);if(!replay)return 3;}
 else if(argc!=1)return 3;
 std::string line;
 while(std::getline(std::cin,line)){std::istringstream input(line);if(!(input>>op))return 2;std::string replay_line=line;
 if(op=='A'){int mode,id,order;int64_t start,end;input>>mode>>id>>start>>end>>order;auto e=prepared(id,start,end,order);int layer;if(input>>layer)e.layer=layer;else{if(!input.eof())return 2;input.clear();}
 d.m_subtitleType=mode==2?EXTVTT:0;if(mode==1)d.EntryOnly(e);else d.AddPrepared(e);}
 else if(op=='Q'){int prior_mode=d.m_mode;d.m_mode=TIME;int mode,index,count;int64_t time;input>>mode>>time;bool found;if(mode==0){index=d.SearchSub(time,25.0);found=index>=0;count=d.m_entries.GetCount();}else{found=(mode==1?d.SearchSubs(time,25.0,&index,&count):d.SearchSubs2(time,25.0,&index,&count))!=nullptr;}std::cout<<"Q "<<index<<' '<<found<<' '<<count<<' ';d.m_mode=prior_mode;}
 else if(op=='E'){int index,id,flag;input>>index>>id>>flag;auto e=prepared(id,0,0,0);d.SetPrepared(index,e.str,flag);}
 else if(op=='O'){size_t count;input>>count;std::vector<size_t> order(count);for(auto& i:order)input>>i;d.ReplayOrder(order);}
 else if(op=='T'){size_t count;input>>count;std::vector<std::pair<REFERENCE_TIME,REFERENCE_TIME>> times(count);for(auto& t:times)input>>t.first>>t.second;d.ReplayTimes(times);}
 else if(op=='P'){}
 else if(op=='S'){
  int restore;input>>restore;auto before=d.m_entries;
  // Comparator subtraction remains bounded; never execute unresolved overflow.
  for(const auto& a:before)for(const auto& b:before){
   if((b.start>0 && a.start<INT64_MIN+b.start)||(b.start<0 && a.start>INT64_MAX+b.start))return 4;
   const int64_t layers=int64_t(a.layer)-b.layer,orders=int64_t(a.readorder)-b.readorder;
   if(layers<INT32_MIN||layers>INT32_MAX||orders<INT32_MIN||orders>INT32_MAX)return 4;
  }
  d.Sort(restore!=0);
  std::ostringstream values;values<<"O "<<before.GetCount();
  for(const auto& entry:d.m_entries){size_t index=0;while(index<before.GetCount() && entry.str.value!=before[index].str.value)++index;if(index==before.GetCount())return 4;values<<' '<<index;}
  replay_line=values.str();
 }
 else if(op=='V'){
  int mode;double fps;input>>mode>>fps;if((mode!=TIME&&mode!=FRAME)||!std::isfinite(fps)||fps==0)return 4;
  bool changed=d.m_mode!=mode;
  if(changed)for(const auto& e:d.m_entries)for(auto time:{e.start,e.end}){
   const double value=mode==TIME?time*UNITS_FLOAT/fps:time*fps/UNITS;
   if(!std::isfinite(value)||std::fabs(value)>=9223372036854775808.0)return 4;
  }
  if(mode==TIME)d.ConvertToTimeBased(fps);else d.ConvertToFrameBased(fps);
  std::ostringstream values;if(changed){values<<"T "<<d.m_entries.GetCount();for(const auto& e:d.m_entries)values<<' '<<e.start<<' '<<e.end;}else values<<"P";
  replay_line=values.str();
 }
 else if(op=='R')d.CreateSegments();
 else if(op=='D')d.RemoveAllEntries();
 else if(op=='C')d.ClearPrepared();
 else if(op=='F'){size_t i;int value;input>>i>>value;d.Feedback(i,value!=0);}
 else if(op=='L'){int64_t bound;input>>bound;if(!d.m_entries.IsEmpty())d.CorrectLast(bound);}
 else if(op=='N'){int old,new_;input>>old>>new_;d.RenamePrepared({static_cast<uint16_t>(old)},{static_cast<uint16_t>(new_)});}
 else if(op=='K'){saved.m_entries.Copy(d.m_entries);saved.m_segments.Copy(d.m_segments);}
 else if(op=='B'){d.m_entries.Copy(saved.m_entries);d.m_segments.Copy(saved.m_segments);}
 else return 2;
 if(!input)return 2;
 if(replay.is_open()){replay<<replay_line<<'\n';if(!replay)return 3;}
 snapshot(d);
 }
 // Callbacks are native observation seams, recorded separately by the runner.
 std::cerr<<d.changed<<' '<<d.logged<<'\n';
}
