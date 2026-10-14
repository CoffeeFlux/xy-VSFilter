// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ssf_stream_shell.h"
static bool ssfWideDecline=false;
static uint64_t ssfWideOwners=0,ssfWideReleases=0,ssfWideReads=0,ssfWideDeclines=0;
static void* ssfWideCreate(const uint16_t* input,int32_t length){
    if(length>0&&ssfWideDecline){++ssfWideDeclines;return NULL;}
    void* owner=xy_vsfilter_ssf_wide_create(input,length);if(owner)++ssfWideOwners;return owner;
}
static int32_t ssfWideRead(const void* owner,int32_t position){++ssfWideReads;return xy_vsfilter_ssf_wide_read(owner,position);}
static void ssfWideRelease(void* owner){if(owner)++ssfWideReleases;xy_vsfilter_ssf_wide_release(owner);}
