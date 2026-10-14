// SPDX-License-Identifier: GPL-3.0-or-later
// Disconnected contracts in a private volatile HKCU view. No DLL registration.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dshow.h>
#include <ocidl.h>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <string>
#include <stdexcept>

static bool observePinIds=true;
static void require(bool value,const char* what){if(!value)throw std::runtime_error(what);}
static void hr(const std::string& name,HRESULT value){std::cout<<name<<" hr "<<static_cast<uint32_t>(value)<<"\n";}
static void number(const std::string& name,uint64_t value){std::cout<<name<<" value "<<value<<"\n";}
static void string(const std::string& name,const wchar_t* value,size_t maximum){
    std::cout<<name<<" utf16";
    if(value){size_t i=0;for(;i<maximum&&value[i];++i)std::cout<<" "<<std::hex<<static_cast<unsigned>(value[i]);require(i<maximum,"unterminated returned string");}
    std::cout<<std::dec<<"\n";
}
static void guid(const std::string& name,REFGUID value){wchar_t text[40]={};require(StringFromGUID2(value,text,40)!=0,"format GUID");string(name,text,40);}
template<class T>class ComPtr {
    T* value=nullptr;
public:
    ComPtr()=default;ComPtr(const ComPtr&)=delete;ComPtr& operator=(const ComPtr&)=delete;
    ~ComPtr(){if(value)value->Release();}
    T* get()const{return value;}T* operator->()const{return value;}
    T** out(){require(!value,"nonempty COM output");return &value;}
    void** address(){return reinterpret_cast<void**>(out());}
    explicit operator bool()const{return value!=nullptr;}
};
static bool identity(IUnknown* a,IUnknown* b){
    ComPtr<IUnknown> left,right;require(SUCCEEDED(a->QueryInterface(IID_IUnknown,left.address()))&&left,"left identity");
    require(SUCCEEDED(b->QueryInterface(IID_IUnknown,right.address()))&&right,"right identity");return left.get()==right.get();
}
static void query(IUnknown* object,REFIID iid,const std::string& name){
    ComPtr<IUnknown> result;const HRESULT status=object->QueryInterface(iid,result.address());hr("qi."+name,status);number("qi."+name+".pointer",bool(result));
    if(SUCCEEDED(status)){require(bool(result),"successful QI without pointer");number("qi."+name+".identity",identity(object,result.get()));}
}
static void pin(IPin* value,IBaseFilter* filter,const std::string& label){
    PIN_DIRECTION direction=static_cast<PIN_DIRECTION>(-1);hr(label+".direction",value->QueryDirection(&direction));number(label+".direction",static_cast<uint32_t>(direction));
    PIN_INFO info={};const HRESULT status=value->QueryPinInfo(&info);hr(label+".info",status);
    if(SUCCEEDED(status)){string(label+".name",info.achName,sizeof(info.achName)/sizeof(info.achName[0]));number(label+".info.direction",info.dir);number(label+".parent",info.pFilter&&identity(info.pFilter,filter));}
    if(info.pFilter)info.pFilter->Release();
    if(observePinIds){
    LPWSTR id=nullptr;const HRESULT idStatus=value->QueryId(&id);hr(label+".id",idStatus);number(label+".id.pointer",id!=nullptr);
    if(SUCCEEDED(idStatus)&&id){string(label+".id",id,4096);ComPtr<IPin> found;hr(label+".find",filter->FindPin(id,found.out()));number(label+".find.identity",found&&identity(value,found.get()));}
    CoTaskMemFree(id);
    }else std::cout<<label<<".id excluded_unresolved_reference\n";
    ComPtr<IPin> connected;hr(label+".connected",value->ConnectedTo(connected.out()));number(label+".connected.pointer",bool(connected));
    query(value,IID_IUnknown,label+".unknown");query(value,IID_IPin,label+".pin");
}
static void inspect(IUnknown* object){
    number("object.addref",object->AddRef());number("object.release",object->Release());
    query(object,IID_IUnknown,"unknown");query(object,IID_IBaseFilter,"base_filter");query(object,IID_IMediaFilter,"media_filter");query(object,IID_IPersist,"persist");
    query(object,IID_ISpecifyPropertyPages,"property_pages");query(object,IID_IAMStreamSelect,"stream_select");
    // These are only queried through IUnknown; custom method tables remain out
    // of scope. Their UUID provenance is checked by the Python driver.
    const wchar_t* custom[]={L"{EBE1FB08-3957-47ca-AF13-5827E5442E56}",L"{FE6EC6A0-21CA-4970-9EF0-B296F7F38AF0}",L"{85E5D6F9-BEFB-4E01-B047-758359CDF9AB}",L"{8871ac83-89c8-44e9-913f-a9a2a322440a}"};
    for(unsigned i=0;i<4;++i){GUID iid={};require(SUCCEEDED(CLSIDFromString(custom[i],&iid)),"parse IID");query(object,iid,"custom."+std::to_string(i));}
    const GUID unsupported={0xf4fca0f1,0x8359,0x4304,{0x95,0x43,0x5b,0x2b,0x21,0xff,0x91,0x65}};query(object,unsupported,"unsupported");
    ComPtr<IBaseFilter> filter;require(SUCCEEDED(object->QueryInterface(IID_IBaseFilter,filter.address()))&&filter,"filter interface");
    CLSID clsid={};hr("filter.class",filter->GetClassID(&clsid));guid("filter.class",clsid);
    FILTER_STATE state=static_cast<FILTER_STATE>(-1);hr("filter.state",filter->GetState(0,&state));number("filter.state",static_cast<uint32_t>(state));
    FILTER_INFO info={};const HRESULT infoStatus=filter->QueryFilterInfo(&info);hr("filter.info",infoStatus);
    if(SUCCEEDED(infoStatus))string("filter.name",info.achName,sizeof(info.achName)/sizeof(info.achName[0]));number("filter.graph",info.pGraph!=nullptr);if(info.pGraph)info.pGraph->Release();
    ComPtr<IReferenceClock> clock;hr("filter.clock",filter->GetSyncSource(clock.out()));number("filter.clock.pointer",bool(clock));
    ComPtr<ISpecifyPropertyPages> pages;if(SUCCEEDED(object->QueryInterface(IID_ISpecifyPropertyPages,pages.address()))&&pages){
        CAUUID ids={};const HRESULT status=pages->GetPages(&ids);hr("pages.get",status);number("pages.count",ids.cElems);require(ids.cElems<=64,"too many property pages");
        if(SUCCEEDED(status)){require(!ids.cElems||ids.pElems,"page array");for(ULONG i=0;i<ids.cElems;++i)guid("pages."+std::to_string(i),ids.pElems[i]);}CoTaskMemFree(ids.pElems);
    }
    ComPtr<IAMStreamSelect> streams;if(SUCCEEDED(object->QueryInterface(IID_IAMStreamSelect,streams.address()))&&streams){DWORD count=0x12345678;hr("streams.count",streams->Count(&count));number("streams.count",count);}
    ComPtr<IEnumPins> pins;require(SUCCEEDED(filter->EnumPins(pins.out()))&&pins,"pin enumerator");
    unsigned count=0;
    for(;count<64;++count){ComPtr<IPin> next;ULONG fetched=123;const HRESULT status=pins->Next(1,next.out(),&fetched);hr("pins.next."+std::to_string(count),status);number("pins.fetched."+std::to_string(count),fetched);if(status==S_FALSE){require(!next,"end pin pointer");break;}require(status==S_OK&&next&&fetched==1,"pin enumeration");pin(next.get(),filter.get(),"pin."+std::to_string(count));}
    require(count<64,"too many pins");number("pins.count",count);hr("pins.reset",pins->Reset());
    ComPtr<IEnumPins> clone;hr("pins.clone",pins->Clone(clone.out()));require(bool(clone),"clone pointer");
    ComPtr<IPin> a,b;ULONG ca=0,cb=0;const HRESULT ha=pins->Next(1,a.out(),&ca),hb=clone->Next(1,b.out(),&cb);hr("pins.first",ha);hr("pins.clone.first",hb);number("pins.first.count",ca);number("pins.clone.first.count",cb);number("pins.clone.identity",a&&b&&identity(a.get(),b.get()));
    hr("pins.clone.reset",clone->Reset());hr("pins.clone.skip",clone->Skip(count));ComPtr<IPin> end;ULONG endCount=0;hr("pins.clone.end",clone->Next(1,end.out(),&endCount));number("pins.clone.end.count",endCount);number("pins.clone.end.pointer",bool(end));
}
int wmain(int argc,wchar_t** argv){
    if(argc!=6){std::cerr<<"usage: com-contract-probe DLL CLSID sta|mta full|autoload_without_ids REGISTRY_KEY\n";return 2;}
    const std::wstring profile=argv[4],registry=argv[5];
    if(profile!=L"full"&&profile!=L"autoload_without_ids")return 2;
    if(profile==L"autoload_without_ids"){
        CLSID actual={},expected={};
        if(FAILED(CLSIDFromString(argv[2],&actual))||FAILED(CLSIDFromString(L"{6b237877-902b-4c6c-92f6-e63169a5166c}",&expected))||actual!=expected)return 2;
        observePinIds=false;
    }
    const std::wstring prefix=L"Software\\xyVSFilterCompatibility\\";
    if(registry.compare(0,prefix.size(),prefix)!=0||registry.size()!=prefix.size()+36)return 2;
    for(size_t i=prefix.size();i<registry.size();++i)if(!((registry[i]>=L'0'&&registry[i]<=L'9')||(registry[i]>=L'a'&&registry[i]<=L'f')||registry[i]==L'-'))return 2;
    HKEY settings=nullptr;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,registry.c_str(),0,nullptr,REG_OPTION_VOLATILE,KEY_ALL_ACCESS,nullptr,&settings,nullptr)!=ERROR_SUCCESS)return 5;
    // Keep this override and handle alive through DLL process-detach callbacks.
    // The parent deletes the private key only after this process has terminated.
    if(RegOverridePredefKey(HKEY_CURRENT_USER,settings)!=ERROR_SUCCESS)return 5;
    std::cout<<std::unitbuf;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    const DWORD mode=std::wstring(argv[3])==L"sta"?COINIT_APARTMENTTHREADED:COINIT_MULTITHREADED;
    const HRESULT initialized=CoInitializeEx(nullptr,mode);if(FAILED(initialized))return 3;
    try{
        HMODULE module=LoadLibraryExW(argv[1],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);require(module!=nullptr,"load DLL");
        using GetFactory=HRESULT(STDAPICALLTYPE*)(REFCLSID,REFIID,LPVOID*);using CanUnload=HRESULT(STDAPICALLTYPE*)();
        auto factoryFn=reinterpret_cast<GetFactory>(GetProcAddress(module,"DllGetClassObject"));auto canUnload=reinterpret_cast<CanUnload>(GetProcAddress(module,"DllCanUnloadNow"));require(factoryFn&&canUnload,"COM exports");
        CLSID clsid={};require(SUCCEEDED(CLSIDFromString(argv[2],&clsid)),"parse CLSID");hr("dll.before",canUnload());
        {ComPtr<IClassFactory> factory;const HRESULT status=factoryFn(clsid,IID_IClassFactory,factory.address());hr("factory.get",status);require(SUCCEEDED(status)&&factory,"class factory");
         ComPtr<IUnknown> object;const HRESULT created=factory->CreateInstance(nullptr,IID_IUnknown,object.address());hr("factory.create",created);require(SUCCEEDED(created)&&object,"create filter");number("control.created",1);inspect(object.get());}
        const HRESULT unload=canUnload();hr("dll.after",unload);CoUninitialize();if(unload==S_OK)FreeLibrary(module);
        std::cout<<"COM_TRACE_COMPLETE\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<"\n";CoUninitialize();return 4;}
}
