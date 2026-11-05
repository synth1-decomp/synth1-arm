#pragma once

/**
 * @file mixdev.h
 * @brief Mixer device (CMixDevice) that sums the outputs of the voices.
 */

#include "sigdev.h"

#define MIXDEV_MAX_INPUT    32
#define MIXDEV_WORK_SIZE    (256 + 16)  // floats (one block plus room for alignment)

/**
 * @brief Sums the outputs of several signal devices (the voices).
 *
 * sizeof == 0x128 (CSynthesizer1 +0x9de0).
 */
class CMixDevice : public CSignalDevice {
public:
    /**
     * @brief Allocates the 16-byte aligned work buffer and clears the inputs.
     */
    CMixDevice();

    /**
     * @brief Frees the work buffer.
     */
    virtual ~CMixDevice();

    /**
     * @brief Sets the number of input slots used by processSignal().
     *
     * @param num  Number of slots, up to MIXDEV_MAX_INPUT.
     */
    void setChannelNum(int num);

    /**
     * @brief Connects an input device.
     *
     * @param ch    Input slot, 0..MIXDEV_MAX_INPUT-1.
     * @param pDev  Device, or NULL to leave the slot empty.
     */
    void inputMixDeviceIs(int ch, CSignalDevice* pDev);

    /**
     * @brief Sets the number of inputs used by processSignalW().
     *
     * @param num  Number of inputs (the first num slots), up to MIXDEV_MAX_INPUT.
     */
    void setEnableVoiceNum(int num);

    /**
     * @brief Mixes the mono outputs of the first m_enableVoiceNum inputs into an interleaved stereo
     *        buffer, panned by each input's getPanVal().
     *
     * A positive pan attenuates the left channel linearly, a negative one the right channel. The
     * output is cleared only once at least one input produces a signal.
     *
     * @param size              Number of frames (at most MIXDEV_WORK_SIZE samples per input).
     * @param[out] pBuf         Interleaved stereo buffer.
     * @param[out] pProcessed   Silence flag: 1 if every input was silent (pBuf is then not written).
     * @return size.
     */
    virtual int processSignalW(int size, float* pBuf, int* pProcessed);

    /**
     * @brief Sums the mono outputs of all connected inputs (slots 0..m_channelNum-1).
     *
     * At most 256 samples are processed per call. The first non-silent input renders directly into
     * pBuf; the others render into the work buffer (aligned like pBuf) and are added, using SSE when
     * available. Inputs that return fewer samples are called repeatedly until the block is full.
     *
     * @param size              Number of samples requested.
     * @param[out] pBuf         Mono output buffer.
     * @param[out] pProcessed   Silence flag: 1 if no input is connected or every input was silent.
     * @return Number of samples produced: min(size, 256).
     */
    virtual int processSignal(int size, float* pBuf, int* pProcessed);

    float* m_pWork;                             // +0x18  16 byte aligned
    CSignalDevice* m_pInputs[MIXDEV_MAX_INPUT]; // +0x20
    int m_channelNum;                           // +0x120
    int m_enableVoiceNum;                       // +0x124
};
