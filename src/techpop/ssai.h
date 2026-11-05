#pragma once

/**
 * @file ssai.h
 * @brief Audio interface that renders the master device in chunks synchronized with
 *        a sequencer (step voice).
 *
 * Also declares two small sample helpers (bit masking and foldback).
 */

class CSignalDevice;

/**
 * @brief Sequencer interface (implemented by CStepVox).
 *
 * vtable: isPlaying, incCurTime, getNextWaitTime, processNote
 */
class CSequencer {
public:
    /**
     * @brief Tells whether the sequencer is running.
     *
     * @return Non-zero while the sequencer is running.
     */
    virtual int isPlaying() = 0;

    /**
     * @brief Advances the sequencer position.
     *
     * @param time  Number of samples rendered.
     */
    virtual void incCurTime(int time) = 0;

    /**
     * @brief Gets the time to the next sequencer event.
     *
     * @return The number of samples until the next sequencer event.
     */
    virtual int getNextWaitTime() = 0;

    /**
     * @brief Handles the sequencer event that is due now.
     */
    virtual void processNote() = 0;
};

/**
 * @brief Abstract source of rendered audio for an output driver.
 */
class CAudioInterface {
public:
    /**
     * @brief Renders audio into the caller's buffers.
     *
     * @param nCh          Number of output buffers (1 or 2).
     * @param[out] ppBuf   Output buffers.
     * @param bMix         With one buffer: 1 to mix L+R into it, otherwise write interleaved stereo.
     * @param size         Number of frames.
     * @return The number of bytes rendered per channel.
     */
    virtual int getSoundData(int nCh, float** ppBuf, int bMix, int size) = 0;
};

/**
 * @brief Audio interface that renders the master output device, splitting the buffer at sequencer
 *        event boundaries so events land on the exact sample.
 */
class CSeqSyncAudioInterface : public CAudioInterface {
public:
    /**
     * @brief Starts with no master device and no sequencer.
     */
    CSeqSyncAudioInterface();

    /**
     * @brief Sets the device whose stereo output is rendered.
     *
     * @param pDev  Master output device (not owned).
     */
    void setMasterOutDevice(CSignalDevice* pDev);

    /**
     * @brief Sets the sequencer that drives the chunking.
     *
     * @param pSeq  Sequencer, or NULL for none.
     */
    void setSequencer(CSequencer* pSeq);

    /**
     * @brief Renders size frames, calling the sequencer's processNote() whenever an event is due.
     *
     * Without a running sequencer the whole buffer is rendered in one go; with neither a sequencer
     * nor a master device nothing is written.
     *
     * @param nCh          Number of output buffers (1 or 2).
     * @param[out] ppBuf   Output buffers.
     * @param bMix         With one buffer: 1 to mix L+R into it, otherwise write interleaved stereo.
     * @param size         Number of frames.
     * @return size * sizeof(float).
     */
    virtual int getSoundData(int nCh, float** ppBuf, int bMix, int size);

    /**
     * @brief Renders a device's stereo output in blocks of up to 256 frames and writes it to the
     *        output buffers.
     *
     * Silent blocks are written as zeros.
     *
     * @param pDev         Device to pull from (processSignalW()).
     * @param[in,out] ppBuf  Output buffers; the pointers in the array are not advanced.
     * @param nCh          1 (mono or interleaved, see bMix) or 2 (separate L and R buffers).
     * @param bMix         With one buffer: 1 to mix L+R, otherwise interleaved stereo.
     * @param size         Number of frames.
     */
    void outputMaster(CSignalDevice* pDev, float** ppBuf, int nCh, int bMix, int size);

    CSignalDevice* m_pMasterOut;    // +0x08
    CSequencer* m_pSequencer;       // +0x10
};

/**
 * @brief Keeps the top keepBits bits of a 16-bit sample and clears the rest.
 *
 * @param input     16-bit sample.
 * @param keepBits  Number of most significant bits to keep (1..16).
 * @return The masked sample.
 */
short keep_bits_from_16(short input, int keepBits);

/**
 * @brief Folds a signal back into -threshold..threshold (foldback distortion).
 *
 * @param in         Input sample.
 * @param threshold  Folding level (> 0).
 * @return in unchanged if it is within the threshold, otherwise the folded value.
 */
float foldback(float in, float threshold);
