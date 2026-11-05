/**
 * @file test_list.cpp
 * @brief Unit tests for techpop/list.h: CCList, the fixed-size ordered int list.
 */
#include "test.h"

#include "list.h"

/**
 * @brief A new list is empty: getNum() is 0 and every lookup returns -1.
 */
TEST(list, new_list_is_empty)
{
    CCList l;
    CHECK_EQ(l.getNum(), 0);
    CHECK_EQ(l.start(), -1);
    CHECK_EQ(l.getEnum(0), -1);
    CHECK_EQ(l.getNew(0), -1);
    CHECK_EQ(l.getOld(0), -1);
    CHECK_EQ(l.next(), -1);
}

/**
 * @brief add() appends in insertion order; getEnum()/getOld() count from the oldest entry and
 *        getNew() from the newest one.
 */
TEST(list, add_keeps_insertion_order)
{
    CCList l;
    l.add(60);
    l.add(64);
    l.add(67);
    CHECK_EQ(l.getNum(), 3);
    CHECK_EQ(l.getEnum(0), 60);
    CHECK_EQ(l.getEnum(1), 64);
    CHECK_EQ(l.getEnum(2), 67);
    CHECK_EQ(l.getOld(0), 60);
    CHECK_EQ(l.getOld(2), 67);
    CHECK_EQ(l.getNew(0), 67);
    CHECK_EQ(l.getNew(1), 64);
    CHECK_EQ(l.getNew(2), 60);
}

/**
 * @brief Lookups with an index >= getNum() return -1.
 */
TEST(list, out_of_range_lookups_return_minus_one)
{
    CCList l;
    l.add(1);
    l.add(2);
    CHECK_EQ(l.getEnum(2), -1);
    CHECK_EQ(l.getEnum(100), -1);
    CHECK_EQ(l.getNew(2), -1);
    CHECK_EQ(l.getOld(2), -1);
}

/**
 * @brief Duplicate values are stored as separate entries.
 */
TEST(list, duplicates_are_kept)
{
    CCList l;
    l.add(5);
    l.add(5);
    l.start();  // m_cur is not initialized by the constructor; del() reads it
    CHECK_EQ(l.getNum(), 2);
    l.del(5);
    CHECK_EQ(l.getNum(), 1);
    CHECK_EQ(l.getEnum(0), 5);
}

/**
 * @brief start()/next() enumerate every entry from oldest to newest, then return -1.
 */
TEST(list, start_next_enumerates_all)
{
    CCList l;
    for (int i = 0; i < 5; i++) l.add(10 + i);
    int v = l.start();
    int count = 0;
    while (v != -1) {
        CHECK_EQ(v, 10 + count);
        count++;
        v = l.next();
    }
    CHECK_EQ(count, 5);
    CHECK_EQ(l.next(), -1);
    CHECK_EQ(l.start(), 10);
}

/**
 * @brief del() removes the first matching entry and closes the gap, preserving order.
 *
 * The constructor leaves the cursor uninitialized and del() reads it, so the tests call start()
 * before any del().
 */
TEST(list, del_removes_and_closes_gap)
{
    CCList l;
    l.add(1);
    l.add(2);
    l.add(3);
    l.add(2);
    l.start();
    l.del(2);
    CHECK_EQ(l.getNum(), 3);
    CHECK_EQ(l.getEnum(0), 1);
    CHECK_EQ(l.getEnum(1), 3);
    CHECK_EQ(l.getEnum(2), 2);
    l.del(1);
    CHECK_EQ(l.getNum(), 2);
    CHECK_EQ(l.getOld(0), 3);
    CHECK_EQ(l.getNew(0), 2);
    l.del(2);
    l.del(3);
    CHECK_EQ(l.getNum(), 0);
    CHECK_EQ(l.start(), -1);
}

/**
 * @brief del() of a value not in the list changes nothing.
 */
TEST(list, del_missing_value_is_noop)
{
    CCList l;
    l.add(7);
    l.add(8);
    l.start();
    l.del(99);
    CHECK_EQ(l.getNum(), 2);
    CHECK_EQ(l.getEnum(0), 7);
    CHECK_EQ(l.getEnum(1), 8);
    CHECK_EQ(l.next(), 8);
}

/**
 * @brief Deleting the current entry during enumeration lets next() continue with the following one.
 */
TEST(list, del_current_during_enumeration)
{
    CCList l;
    for (int i = 0; i < 6; i++) l.add(i);
    // delete every even value while enumerating
    int seen[6];
    int n = 0;
    for (int v = l.start(); v != -1; v = l.next()) {
        seen[n++] = v;
        if ((v & 1) == 0) l.del(v);
    }
    CHECK_EQ(n, 6);
    for (int i = 0; i < n && i < 6; i++) CHECK_EQ(seen[i], i);
    CHECK_EQ(l.getNum(), 3);
    CHECK_EQ(l.getEnum(0), 1);
    CHECK_EQ(l.getEnum(1), 3);
    CHECK_EQ(l.getEnum(2), 5);
}

/**
 * @brief Deleting an entry before the cursor moves the cursor back so no entry is skipped;
 *        deleting one after the cursor leaves it in place.
 */
TEST(list, del_adjusts_cursor)
{
    CCList l;
    l.add(10);
    l.add(20);
    l.add(30);
    l.add(40);
    CHECK_EQ(l.start(), 10);
    CHECK_EQ(l.next(), 20);    // cursor at index 1
    l.del(10);                 // before the cursor
    CHECK_EQ(l.next(), 30);
    l.del(40);                 // after the cursor
    CHECK_EQ(l.next(), -1);
    CHECK_EQ(l.getNum(), 2);
}

/**
 * @brief The list holds at most CCLIST_MAX entries; further add() calls are ignored.
 */
TEST(list, capacity_is_cclist_max)
{
    CCList l;
    for (int i = 0; i < CCLIST_MAX + 10; i++) l.add(i);
    CHECK_EQ(l.getNum(), CCLIST_MAX);
    CHECK_EQ(l.getNew(0), CCLIST_MAX - 1);
    CHECK_EQ(l.getOld(0), 0);
    l.start();
    l.del(0);
    CHECK_EQ(l.getNum(), CCLIST_MAX - 1);
    l.add(1000);
    CHECK_EQ(l.getNum(), CCLIST_MAX);
    CHECK_EQ(l.getNew(0), 1000);
}

/**
 * @brief setEnum() overwrites an entry in place without changing the count.
 */
TEST(list, set_enum_overwrites)
{
    CCList l;
    l.add(1);
    l.add(2);
    l.setEnum(1, 42);
    CHECK_EQ(l.getNum(), 2);
    CHECK_EQ(l.getEnum(1), 42);
    CHECK_EQ(l.getNew(0), 42);
}
