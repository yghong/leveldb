// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// See port_example.h for documentation for the following types/functions.

#ifndef STORAGE_LEVELDB_PORT_PORT_WIN_H_
#define STORAGE_LEVELDB_PORT_PORT_WIN_H_

#include <stdint.h>
#include <string>
#include "port/atomic_pointer.h"

//#ifdef LITTLE_ENDIAN
//#define IS_LITTLE_ENDIAN true
//#else
//#define IS_LITTLE_ENDIAN (__BYTE_ORDER == __LITTLE_ENDIAN)
//#endif

//#if defined(OS_MACOSX) || defined(OS_SOLARIS) || defined(OS_FREEBSD) ||\
//    defined(OS_NETBSD) || defined(OS_OPENBSD) || defined(OS_DRAGONFLYBSD)
//// Use fread/fwrite/fflush on platforms without _unlocked variants
//#define fread_unlocked fread
//#define fwrite_unlocked fwrite
//#define fflush_unlocked fflush
//#endif

//#if defined(OS_MACOSX) || defined(OS_FREEBSD) ||\
//    defined(OS_OPENBSD) || defined(OS_DRAGONFLYBSD)
//// Use fsync() on platforms without fdatasync()
//#define fdatasync fsync
//#endif



#define _NS_LEVELDB_PORT_BEGIN_ namespace leveldb { namespace port {
#define _NS_LEVELDB_PORT_END_	} }


_NS_LEVELDB_PORT_BEGIN_

static const bool kLittleEndian = false;

class Mutex {
public:
	Mutex(void);
	~Mutex(void);

	void Lock(void);
	void Unlock(void);
	void AssertHeld(void) { }

	Mutex(const Mutex&) = delete;
	void operator=(const Mutex&) = delete;

private:
	CRITICAL_SECTION cs_;

	friend class CondVar;
};

class CondVar {
public:
	explicit CondVar(Mutex* mu);
	~CondVar(void);

	void Wait(void);
	void Signal(void);
	void SignalAll(void);

private:
	Mutex* mu_;
	CONDITION_VARIABLE cv_;
};

inline bool Snappy_Compress(const char* input, size_t length, std::string* output)
{
  return false;
}

inline bool Snappy_GetUncompressedLength(const char* input, size_t length, size_t* result)
{
  return false;
}

inline bool Snappy_Uncompress(const char* input, size_t length, char* output)
{
  return false;
}

inline bool GetHeapProfile(void (*func)(void*, const char*, int), void* arg)
{
  return false;
}

_NS_LEVELDB_PORT_END_

#endif  // STORAGE_LEVELDB_PORT_PORT_WIN_H_
