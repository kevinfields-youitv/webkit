/*
 * Copyright (C) 2017 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#pragma once

#include <wtf/Condition.h>
#include <wtf/Lock.h>

namespace WTF {

// This is a traditional read-write lock implementation that enables concurrency between readers so long as
// the read critical section is long. Concurrent readers will experience contention on read().lock() and
// read().unlock() if the work inside the critical section is short. The more cores participate in reading,
// the longer the read critical section has to be for this locking scheme to be profitable.

struct ReadWriteLockBase {
    WTF_EXPORT_PRIVATE void construct();
    
    // It's easiest to read lock like this:
    // 
    //     auto locker = holdLock(rwLock.read());
    //
    // It's easiest to write lock like this:
    // 
    //     auto locker = holdLock(rwLock.write());
    //
    WTF_EXPORT_PRIVATE void readLock();
    WTF_EXPORT_PRIVATE void readUnlock();
    WTF_EXPORT_PRIVATE void writeLock();
    WTF_EXPORT_PRIVATE void writeUnlock();
    
    class ReadLock;
    class WriteLock;

    ReadLock& read();
    WriteLock& write();

private:
    // These fields must work when zero-filled, so that StaticReadWriteLock works.
    LockBase m_lock;
    ConditionBase m_cond;
    bool m_isWriteLocked;
    unsigned m_numReaders;
    unsigned m_numWaitingWriters;
};

class ReadWriteLockBase::ReadLock : public ReadWriteLockBase {
public:
    bool tryLock() { return false; }
    void lock() { readLock(); }
    void unlock() { readUnlock(); }
};

class ReadWriteLockBase::WriteLock : public ReadWriteLockBase {
public:
    bool tryLock() { return false; }
    void lock() { writeLock(); }
    void unlock() { writeUnlock(); }
};
    
inline ReadWriteLockBase::ReadLock& ReadWriteLockBase::read() { return *static_cast<ReadLock*>(this); }
inline ReadWriteLockBase::WriteLock& ReadWriteLockBase::write() { return *static_cast<WriteLock*>(this); }

class ReadWriteLock : public ReadWriteLockBase {
public:
    ReadWriteLock()
    {
        construct();
    }
};

typedef ReadWriteLockBase StaticReadWriteLock;

} // namespace WTF

using WTF::ReadWriteLock;
using WTF::StaticReadWriteLock;

