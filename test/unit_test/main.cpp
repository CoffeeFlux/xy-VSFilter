#include <afx.h>
#include <gtest/gtest.h>
#include <iostream>

#define XY_UNIT_TEST
#include "test_overall.h"


int wmain(int argc, wchar_t ** argv)
{
    testing::InitGoogleTest(&argc, argv);

    // The script is only needed by OverallTest, which is skipped without one.
    if (argc > 2)
    {
        std::wcout<<argv[0]<<L" [gtest options] [script_name]"<<std::endl;
        return -1;
    }
    if (argc == 2)
    {
        char namebuf[256];
        WideCharToMultiByte(CP_UTF8, 0, argv[1], -1, namebuf, sizeof(namebuf)/sizeof(char), NULL, NULL);
        OpenTestScript(namebuf);
    }

    return RUN_ALL_TESTS();
}