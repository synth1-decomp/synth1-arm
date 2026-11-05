/**
 * @file ssechk.cpp
 * @brief SSE capability check (CSSEChecker); always reports SSE as available.
 */
#include "common.h"
#include "ssechk.h"

CSSEChecker::CSSEChecker()
{
    m_bSSE = checkSSE();
}

int CSSEChecker::checkSSE()
{
    return 1;
}
