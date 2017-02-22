// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "port/port_win.h"

_NS_LEVELDB_PORT_BEGIN_

Mutex::Mutex(void)
{
	::InitializeCriticalSection(&cs_);
}

Mutex::~Mutex(void)
{
	::DeleteCriticalSection(&cs_);
}

void Mutex::Lock(void)
{
	::EnterCriticalSection(&cs_);
}

void Mutex::Unlock(void)
{
	::LeaveCriticalSection(&cs_);
}

CondVar::CondVar(Mutex* mu)
	: mu_(mu)
{
	::InitializeConditionVariable(&cv_);
}

CondVar::~CondVar(void)
{
}

void CondVar::Wait(void)
{
	::SleepConditionVariableCS(&cv_, &mu_->cs_, INFINITE);
}

void CondVar::Signal()
{
	::WakeConditionVariable(&cv_);
}

void CondVar::SignalAll()
{
	::WakeAllConditionVariable(&cv_);
}

_NS_LEVELDB_PORT_END_