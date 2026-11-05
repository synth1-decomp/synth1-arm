#pragma once

/**
 * @file myasiodev.h
 * @brief Sound device (ASIO) setup.  The Mac AU has no device of its own,
 *        so these are empty stubs.
 */

/**
 * @brief Opens the sound device; a stub that does nothing.
 *
 * @return 0.
 */
int initSoundDevice();

/**
 * @brief Closes the sound device; a stub that does nothing.
 *
 * @return 0.
 */
int freeSoundDevice();
