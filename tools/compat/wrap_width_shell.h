// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <memory>
#include <cerrno>
#include "xy_rust_wrap.h"
#ifdef _WIN32
#include <atlbase.h>
#include <atlcoll.h>
#else
typedef void* POSITION;
#endif
struct WrapWord {int m_width;bool m_fLineBreak;int id;};
typedef std::shared_ptr<WrapWord> SharedPtrCWord;
static std::vector<int> wrapReads;
class WrapWords {
#ifdef _WIN32
    ATL::CAtlList<SharedPtrCWord> values;
#else
    struct Node {SharedPtrCWord value;Node* next;Node(const SharedPtrCWord& v):value(v),next(NULL){}};
    Node* head=NULL;Node* tail=NULL;
#endif
public:
#ifndef _WIN32
    ~WrapWords(){while(head){Node* node=head;head=head->next;delete node;}}
#endif
    void AddTail(const SharedPtrCWord& word){
#ifdef _WIN32
        values.AddTail(word);
#else
        Node* node=new Node(word);if(tail)tail->next=node;else head=node;tail=node;
#endif
    }
    POSITION GetHeadPosition()const{
#ifdef _WIN32
        return values.GetHeadPosition();
#else
        return head;
#endif
    }
    SharedPtrCWord GetNext(POSITION& position){
#ifdef _WIN32
        SharedPtrCWord word=values.GetNext(position);
#else
        Node* node=static_cast<Node*>(position);SharedPtrCWord word=node->value;position=node->next;
#endif
        wrapReads.push_back(word->id);wrapReads.push_back(word->m_width);wrapReads.push_back(word->m_fLineBreak?1:0);return word;
    }
};
static uint64_t wrapBegins=0,wrapSteps=0,wrapFinishes=0,wrapDeclines=0;
namespace XyRust {
class ObservedWrapWidth : public WrapWidth {
public:
    ObservedWrapWidth(int full,int maximum):WrapWidth(full,maximum){++wrapBegins;if(!Valid())++wrapDeclines;}
    bool Push(int word){++wrapSteps;bool ok=WrapWidth::Push(word);if(!ok)++wrapDeclines;return ok;}
    bool Finish(int full,int style,int& out)const{bool ok=WrapWidth::Finish(full,style,out);if(ok)++wrapFinishes;else ++wrapDeclines;return ok;}
};
}
