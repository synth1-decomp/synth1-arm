/**
 * @file list.cpp
 * @brief Implementation of CCList, the fixed-size ordered int list.
 */
#include "list.h"

CCList::CCList()
{
    m_num = 0;
}

void CCList::add(int val)
{
    if(m_num < CCLIST_MAX){
        m_list[m_num] = val;
        m_num++;
    }
}

int CCList::getNum()
{
    return m_num;
}

int CCList::start()
{
    m_cur = 0;
    if(m_cur < m_num) return m_list[m_cur];
    return -1;
}

int CCList::getEnum(int idx)
{
    if(m_num > 0 && idx < m_num) return m_list[idx];
    return -1;
}

int CCList::next()
{
    m_cur++;
    return getEnum(m_cur);
}

void CCList::del(int val)
{
    int i;
    for(i = 0; i < m_num; i++){
        if(m_list[i] == val){
            if(m_cur >= i) m_cur--;
            for(; i < m_num - 1; i++){
                m_list[i] = m_list[i + 1];
            }
            m_num--;
            return;
        }
    }
}

// idx-th newest entry
int CCList::getNew(int idx)
{
    if(m_num > 0 && idx < m_num) return m_list[m_num - 1 - idx];
    return -1;
}

// idx-th oldest entry
int CCList::getOld(int idx)
{
    if(m_num > 0 && idx < m_num) return m_list[idx];
    return -1;
}

void CCList::setEnum(int idx, int val)
{
    m_list[idx] = val;
}
