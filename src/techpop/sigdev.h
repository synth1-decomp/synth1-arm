#pragma once

/**
 * @file sigdev.h
 * @brief CSignalDevice, the base class of the signal-processing devices, plus
 *        the StereoData sample pair and a denormal-flush helper.
 */

/**
 * @brief Interleaved stereo sample pair (left, right).
 */
#ifndef TAGSTEREODATA_DEFINED
#define TAGSTEREODATA_DEFINED
typedef struct tagStereoData {
    float l;
    float r;
} StereoData;
#endif

/**
 * @brief Copies the result of the global SSE check into CSignalDevice::m_bSSE.
 *
 * C-linkage entry point so that code outside the engine (the Cocoa view) can
 * enable the SSE paths of the static array helpers before any device exists.
 */
extern "C" void CheckSSEFunc();

/**
 * @brief Base class of the signal-processing devices.
 *
 * A device produces or transforms a block of float samples in place. Devices
 * are chained through m_pInput, and each one knows the current sampling rate.
 * The class also provides static SSE helpers for common array operations;
 * they fall back to scalar code when SSE is unavailable (m_bSSE == 0).
 *
 * The @p pProcessed flag used by the processSignal family is a silence flag:
 * nonzero means the block is silent and the buffer was not filled (callers
 * then treat the output as zeros). Some devices also read it on entry to
 * learn that their input is silent.
 *
 * sizeof(CSignalDevice) == 0x18; derived classes start their fields at +0x14
 * (tail padding of the base is reused).
 *   vtable: ~CSignalDevice, ~CSignalDevice(deleting), processSignal (pure),
 *           processSignalForced, processSignalW, processSignalWForced,
 *           onChangeSamplingFreq (inline), getPanVal (inline)
 */
class CSignalDevice {
public:
    /**
     * @brief Constructs a device with no input at 44100 Hz.
     *
     * Also refreshes m_bSSE from the global SSE check.
     */
    CSignalDevice();

    /**
     * @brief Destroys the device; nothing to release.
     */
    virtual ~CSignalDevice(){}

    /**
     * @brief Processes one mono block in place.
     *
     * @param size              Number of samples requested.
     * @param[in,out] pBuf      Sample buffer of at least @p size floats.
     * @param[in,out] pProcessed Silence flag; set nonzero when the output is
     *                          silent and @p pBuf was not written.
     * @return Number of samples produced.
     */
    virtual int processSignal(int size, float* pBuf, int* pProcessed) = 0;

    /**
     * @brief Processes one mono block even when the device would normally skip work.
     *
     * The base version just calls processSignal().
     *
     * @param size              Number of samples requested.
     * @param[in,out] pBuf      Sample buffer.
     * @param[in,out] pProcessed Silence flag, as for processSignal().
     * @return Number of samples produced.
     */
    virtual int processSignalForced(int size, float* pBuf, int* pProcessed);

    /**
     * @brief Processes one block into an interleaved stereo buffer.
     *
     * The base version runs processSignal() and, unless the result is silent,
     * expands the mono output in place to stereo with mono2Stereo().
     *
     * @param size              Number of sample frames requested.
     * @param[in,out] pBuf      Buffer of at least 2 * @p size floats (L, R, L, R, ...).
     * @param[in,out] pProcessed Silence flag, as for processSignal().
     * @return Number of frames produced.
     */
    virtual int processSignalW(int size, float* pBuf, int* pProcessed);

    /**
     * @brief Stereo counterpart of processSignalForced().
     *
     * The base version just calls processSignalW().
     *
     * @param size              Number of sample frames requested.
     * @param[in,out] pBuf      Interleaved stereo buffer.
     * @param[in,out] pProcessed Silence flag, as for processSignal().
     * @return Number of frames produced.
     */
    virtual int processSignalWForced(int size, float* pBuf, int* pProcessed);

    /**
     * @brief Called by setSamplingFreq() after m_samplingFreq changes; the base version does nothing.
     */
    virtual void onChangeSamplingFreq(){}

    /**
     * @brief Gets the pan position of the device.
     *
     * @return Pan position (-1 left .. 0 center .. +1 right); the base version returns 0.
     */
    virtual float getPanVal(){ return 0.f; }

    /**
     * @brief Sets the sampling rate and notifies the device.
     *
     * @param fs    Sampling rate in Hz.
     * @see onChangeSamplingFreq()
     */
    void setSamplingFreq(int fs);

    /**
     * @brief Sets the device that feeds this one (m_pInput).
     *
     * @param pDev  Input device, or NULL for none.
     */
    void inputDeviceIs(CSignalDevice* pDev);

    /**
     * @brief Copies the result of the global SSE check into m_bSSE.
     */
    void checkSSEFunc();

    /**
     * @brief Multiplies an array by a constant in place: p[i] *= v.
     *
     * @param[in,out] p     Array to scale.
     * @param v             Factor.
     * @param size          Number of elements.
     */
    static void mulArray2SSE(float* p, float v, int size);

    /**
     * @brief Adds one array to another: d[i] += s[i].
     *
     * The SSE path is skipped when @p d starts less than 16 bytes after @p s
     * (overlap that would change the result).
     *
     * @param[in,out] d     Destination and first operand.
     * @param s             Second operand.
     * @param size          Number of elements.
     */
    static void addArraySSE(float* d, float* s, int size);

    /**
     * @brief Scales an array into another: d[i] = s[i] * v.
     *
     * When @p v is 0 the destination is simply cleared.
     *
     * @param[out] d        Destination.
     * @param s             Source.
     * @param v             Factor.
     * @param size          Number of elements.
     */
    static void mulArraySSE(float* d, float* s, float v, int size);

    /**
     * @brief Multiply-accumulate into a third array: d[i] = s1[i] + s2[i] * v.
     *
     * When @p v is 0, @p s1 is copied to @p d.
     *
     * @param[out] d        Destination (may alias @p s1).
     * @param s1            Addend.
     * @param s2            Array to scale.
     * @param v             Factor applied to @p s2.
     * @param size          Number of elements.
     */
    static void addMulArraySSE(float* d, float* s1, float* s2, float v, int size);

    /**
     * @brief Expands a mono buffer in place to interleaved stereo.
     *
     * Works from the end backwards so the expansion can be done in place.
     *
     * @param[in,out] p     Buffer holding @p size mono samples on entry and
     *                      2 * @p size interleaved samples on return.
     * @param size          Number of mono samples.
     */
    static void mono2Stereo(float* p, int size);

    static int m_bSSE;

    CSignalDevice* m_pInput;    // +0x08
    int m_samplingFreq;         // +0x10  (44100)
};

/**
 * @brief Flushes a denormal float to zero.
 *
 * @param[in,out] v     Value to check; set to 0.0f if it is denormal.
 */
inline void killDenormal(float& v)
{
    union { float f; unsigned int i; } u;
    u.f = v;
    unsigned int b = u.i << 1;  // sign bit dropped: exponent in the top 8 bits
    if (b < 0x1000000 && (b & 0xfffffe)) v = 0.0f;
}
