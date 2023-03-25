#pragma once

#ifdef __UVA_WIN__
    #define CCOV_BEGIN_COVERAGE() OutputDebugStringW(L"begin coverage");
    #define CCOV_END_COVERAGE() OutputDebugStringW(L"end coverage");
#else
    
#endif