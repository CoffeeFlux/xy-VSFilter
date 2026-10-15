// SPDX-License-Identifier: GPL-2.0-or-later
// Link separately against the unmodified pinned and candidate libssf libraries.
#include "subtitles/libssf/stdafx.h"
#include "SubPic/ISubPic.h"
#include "subtitles/libssf/SubtitleFile.h"
#include "subtitles/libssf/Renderer.h"
#include <vector>
#include <cstdio>
#include <memory>

static void writeFile(const CStringW& path,const void* data,size_t count){
    FILE* file=NULL;
    if(_wfopen_s(&file,path,L"wb")||!file)throw 1;
    const bool ok=count==0||fwrite(data,1,count,file)==count;
    const int closed=fclose(file);
    if(!ok||closed)throw 1;
}
// Observe the retained public node API. Cleanup removed OutputStream and Dump.
static void appendString(std::vector<unsigned>& output,const CStringW& text){
    output.push_back(static_cast<unsigned>(text.GetLength()));
    for(int i=0;i<text.GetLength();++i)output.push_back(static_cast<unsigned>(text[i]));
}
static std::vector<unsigned> snapshotNodes(ssf::Reference* root){
    if(!root)throw 1;
    std::vector<ssf::Node*> nodes(1,root);
    std::vector<unsigned> output;
    for(size_t index=0;index<nodes.size();++index){
        if(nodes.size()>10000)throw 1;
        ssf::Node* node=nodes[index];
        output.push_back(static_cast<unsigned>(index));
        appendString(output,node->m_type);appendString(output,node->m_name);
        output.push_back(static_cast<unsigned>(node->m_priority));
        output.push_back(node->m_predefined?1:0);
        ssf::Definition* definition=dynamic_cast<ssf::Definition*>(node);
        output.push_back(definition?1:0);
        if(definition){
            for(int status=1;status<=4;++status)
                output.push_back(definition->IsValue(static_cast<ssf::Definition::status_t>(status))?1:0);
            CStringW value;if(definition->IsValue())definition->GetAsString(value);
            appendString(output,value);
        }
        output.push_back(static_cast<unsigned>(node->m_nodes.GetCount()));
        POSITION position=node->m_nodes.GetHeadPosition();
        while(position){
            ssf::Node* child=node->m_nodes.GetNext(position);
            size_t childIndex=0;
            while(childIndex<nodes.size()&&nodes[childIndex]!=child)++childIndex;
            if(childIndex==nodes.size())nodes.push_back(child);
            output.push_back(static_cast<unsigned>(childIndex));
        }
    }
    return output;
}
int wmain(int argc,wchar_t** argv){
    if(argc!=4)return 64;
    setvbuf(stdout,NULL,_IONBF,0);
    if(!AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0))return 65;
    try{
        FILE* input=NULL;
        if(_wfopen_s(&input,argv[1],L"rb")||!input)return 66;
        if(fseek(input,0,SEEK_END)){fclose(input);return 66;}
        const long size=ftell(input);
        if(size<0||size>1024*1024||size%2){fclose(input);return 66;}
        rewind(input);std::vector<WCHAR> units(size/2+1,0);
        const bool read=size==0||fread(units.data(),1,size,input)==static_cast<size_t>(size);
        fclose(input);if(!read)return 66;
        const CStringW text(units.data(),size/2),directory(argv[2]);
        std::unique_ptr<ssf::InputStream> stream;
        std::vector<BYTE> bytes(static_cast<size_t>(size)+2);
        bytes[0]=0xff;bytes[1]=0xfe;if(size)memcpy(bytes.data()+2,units.data(),size);
        if(wcscmp(argv[3],L"wide")==0)stream.reset(new ssf::WCharInputStream(text));
        else if(wcscmp(argv[3],L"borrow")==0)stream.reset(new ssf::MemoryInputStream(bytes.data(),static_cast<int>(bytes.size()),false,false));
        else if(wcscmp(argv[3],L"copy")==0){
            stream.reset(new ssf::MemoryInputStream(bytes.data(),static_cast<int>(bytes.size()),true,false));
            // The copied stream must survive mutation of the original bytes.
            memset(bytes.data(),0x5a,bytes.size());
        }else if(wcscmp(argv[3],L"adopt")==0){
            std::unique_ptr<BYTE[]> adopted(new BYTE[bytes.size()]);memcpy(adopted.get(),bytes.data(),bytes.size());
            stream.reset(new ssf::MemoryInputStream(adopted.get(),static_cast<int>(bytes.size()),false,true));
            adopted.release();
        }else return 64;
        ssf::SubtitleFile file;
        file.Parse(*stream);
        printf("ssf-native-v1 parsed segments %llu\n",static_cast<unsigned long long>(file.m_segments.GetCount()));
        const std::vector<unsigned> snapshot=snapshotNodes(file.GetRootRef());
        CStringW nodePath;nodePath.Format(L"%s\\nodes.units",directory.GetString());
        writeFile(nodePath,snapshot.data(),snapshot.size()*sizeof(unsigned));
        printf("nodes units %llu\n",static_cast<unsigned long long>(snapshot.size()));
        ssf::Renderer renderer;
        const float times[]={-0.25f,0.0f,0.5f,1.75f,2.0f,0.5f};
        for(unsigned frame=0;frame<sizeof(times)/sizeof(times[0]);++frame){
            std::vector<DWORD> pixels(320*180,0xff000000);
            SubPicDesc desc;desc.type=MSP_RGBA;desc.w=320;desc.h=180;desc.bpp=32;desc.pitch=320*4;
            desc.bits=reinterpret_cast<BYTE*>(pixels.data());SetRect(&desc.vidrect,0,0,320,180);
            CAutoPtrList<ssf::Subtitle> subtitles;const bool found=file.Lookup(times[frame],subtitles);
            renderer.NextSegment(subtitles);CRect bounds(0,0,0,0);
            printf("frame %u found %u subtitles %llu\n",frame,found?1:0,static_cast<unsigned long long>(subtitles.GetCount()));
            POSITION position=subtitles.GetHeadPosition();
            while(position){
                const ssf::Subtitle* subtitle=subtitles.GetNext(position);
                POSITION run=subtitle->m_text.GetHeadPosition();
                while(run){
                    const ssf::Text& text=subtitle->m_text.GetNext(run);printf("text units");
                    for(int i=0;i<text.str.GetLength();++i)printf(" %04x",static_cast<unsigned>(text.str[i]));
                    printf("\n");
                }
                const ssf::RenderedSubtitle* rendered=renderer.Lookup(subtitle,CSize(320,180),desc.vidrect);
                printf("rendered %u glyphs",rendered?1:0);
                if(rendered){
                    POSITION glyph=rendered->m_glyphs.GetHeadPosition();
                    while(glyph)printf(" %04x",static_cast<unsigned>(rendered->m_glyphs.GetNext(glyph)->c));
                    bounds|=rendered->Draw(desc);
                }
                printf("\n");
            }
            unsigned changed=0;for(DWORD pixel:pixels)changed+=pixel!=0xff000000;
            printf("pixels %u changed %u bounds %ld %ld %ld %ld\n",frame,changed,bounds.left,bounds.top,bounds.right,bounds.bottom);
            CStringW path;path.Format(L"%s\\frame-%u.bgra",directory.GetString(),frame);
            writeFile(path,pixels.data(),pixels.size()*sizeof(DWORD));
        }
        printf("ssf-native-v1 complete\n");
        return 0;
    }catch(ssf::Exception& error){
        const CString message=error.ToString();printf("ssf exception utf16");
        for(int i=0;i<message.GetLength();++i)printf(" %04x",static_cast<unsigned>(message[i]));
        printf("\nssf-native-v1 exception complete\n");return 2;
    }catch(...){printf("ssf probe failed\n");return 3;}
}
