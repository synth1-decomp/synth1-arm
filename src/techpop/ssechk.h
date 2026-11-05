#pragma once

/**
 * @file ssechk.h
 * @brief SSE capability check (CSSEChecker).
 */

/**
 * @brief Checks whether the CPU supports SSE (always true on Intel Macs).
 *
 * A global instance (g_sseChecker in sigdev.cpp) supplies CSignalDevice::m_bSSE.
 */
class CSSEChecker {
public:
    /**
     * @brief Runs the check and stores the result in m_bSSE.
     */
    CSSEChecker();

    /**
     * @brief Does nothing.
     */
    virtual ~CSSEChecker(){}

    /**
     * @brief Checks for SSE support.
     *
     * @return 1 (SSE is always available).
     */
    int checkSSE();

    int m_bSSE; // +0x08  (read directly by sigdev.cpp)
};
