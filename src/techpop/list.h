#pragma once

/**
 * @file list.h
 * @brief CCList, a small fixed-size ordered list of ints.
 */

#define CCLIST_MAX  256

/**
 * @brief Simple fixed-size ordered list of ints (used e.g. for held note numbers).
 *
 * Holds up to CCLIST_MAX values in insertion order (oldest first). Has a single
 * built-in cursor for start()/next() enumeration that del() keeps consistent.
 * Lookup functions return -1 when the index is out of range.
 */
class CCList {
public:
    /**
     * @brief Creates an empty list.
     */
    CCList();
    virtual ~CCList(){}

    /**
     * @brief Appends a value at the end (newest position).
     *
     * Does nothing if the list already holds CCLIST_MAX entries.
     * @param val   Value to append.
     */
    void add(int val);

    /**
     * @brief Gets the number of entries.
     *
     * @return The number of entries, 0..CCLIST_MAX.
     */
    int getNum();

    /**
     * @brief Resets the enumeration cursor to the first (oldest) entry.
     *
     * @return The first entry, or -1 if the list is empty.
     */
    int start();

    /**
     * @brief Returns the entry at a position (0 = oldest).
     *
     * @param idx   Position in the list.
     * @return The entry, or -1 if the list is empty or idx >= getNum().
     */
    int getEnum(int idx);

    /**
     * @brief Advances the enumeration cursor.
     *
     * @return The next entry, or -1 at the end of the list.
     */
    int next();

    /**
     * @brief Removes the first entry equal to val and closes the gap.
     *
     * If the removed entry was at or before the enumeration cursor, the cursor is
     * moved back by one so that next() continues with the following entry.
     * Does nothing if val is not in the list.
     * @param val   Value to remove.
     */
    void del(int val);

    /**
     * @brief Returns the idx-th newest entry (0 = most recently added).
     *
     * @param idx   Age rank counted from the newest entry.
     * @return The entry, or -1 if out of range.
     */
    int getNew(int idx);

    /**
     * @brief Returns the idx-th oldest entry (0 = least recently added).
     *
     * @param idx   Age rank counted from the oldest entry.
     * @return The entry, or -1 if out of range.
     */
    int getOld(int idx);

    /**
     * @brief Overwrites the entry at a position.
     *
     * @note No range check is made; idx must be < CCLIST_MAX.
     * @param idx   Position in the list (0 = oldest).
     * @param val   New value.
     */
    void setEnum(int idx, int val);

protected:
    int m_list[CCLIST_MAX]; // +0x008
    int m_num;              // +0x408
    int m_cur;              // +0x40c
};
