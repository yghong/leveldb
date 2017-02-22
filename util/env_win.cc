// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <io.h>
#include <fcntl.h>
#include <mutex>
#include <deque>
#include <fstream>
#include <sstream>
#include <memory>
#include <filesystem>

#include "leveldb/env.h"
#include "leveldb/slice.h"
#include "port/port.h"
#include "util/logging.h"


#define _NS_LEVELDB_BEGIN_		namespace leveldb {
#define _NS_LEVELDB_END_		}

#define _NS_ANONYMOUS_BEGIN_	namespace {
#define _NS_ANONYMOUS_END_		}

_NS_LEVELDB_BEGIN_
_NS_ANONYMOUS_BEGIN_


namespace exp_fs = std::experimental::filesystem::v1;


Status IOError(const std::string& context, int err_number) {
	return Status::IOError(context, strerror(err_number));
}

class _WinSequentialFile : public SequentialFile
{
public:
	_WinSequentialFile(const std::string& fname, FILE* f);
	~_WinSequentialFile(void);

	Status Read(size_t n, Slice* result, char* scratch) override;
	Status Skip(uint64_t n) override;

private:
	std::string filename_;
	FILE* file_;
};

_WinSequentialFile::_WinSequentialFile(const std::string& fname, FILE* f)
	: filename_(fname)
	, file_(f)
{
}

_WinSequentialFile::~_WinSequentialFile(void)
{
	::fclose(file_);
}

Status _WinSequentialFile::Read(size_t n, Slice* result, char* scratch)
{
	size_t r = ::_fread_nolock(scratch, 1, n, file_);

	*result = Slice(scratch, r);
	if (r < n) {
		if (::feof(file_)) {
			// We leave status as ok if we hit the end of the file
		} else {
			// A partial read with an error: return a non-ok status
			return IOError(filename_, errno);
		}
	}

	return Status::OK();
}

Status _WinSequentialFile::Skip(uint64_t n)
{
	if (::fseek(file_, n, SEEK_CUR)) {
		return IOError(filename_, errno);
	}

	return Status::OK();
}


class _WinRandomAccessFile : public RandomAccessFile {
public:
	_WinRandomAccessFile(const std::string& fname, int fd);
	~_WinRandomAccessFile(void);

	Status Read(uint64_t offset, size_t n, Slice* result, char* scratch) const override;

private:
	std::string filename_;
	int fd_;
	mutable std::mutex mu_;
};


_WinRandomAccessFile::_WinRandomAccessFile(const std::string& fname, int fd)
	: filename_(fname)
	, fd_(fd)
{
}

_WinRandomAccessFile::~_WinRandomAccessFile()
{
	::close(fd_);
}

Status _WinRandomAccessFile::Read(uint64_t offset, size_t n, Slice* result, char* scratch) const
{
	std::unique_lock<std::mutex> lock(mu_);

	if (::_lseeki64(fd_, offset, SEEK_SET) == -1L) {
		return Status::IOError(filename_, strerror(errno));
	}

	int r = ::_read(fd_, scratch, n);
	*result = Slice(scratch, (r < 0) ? 0 : r);
	lock.unlock();

	if (r < 0) {
		return IOError(filename_, errno);
	}

	return Status::OK();
}

class _WinWritableFile : public WritableFile
{
public:
	explicit _WinWritableFile(const std::string& path);
	~_WinWritableFile(void);

public:
	Status Append(const Slice& data) override;
	Status Close(void) override;
	Status Flush(void) override;
	Status Sync(void) override;

private:
	void Open(void);

	exp_fs::path path_;
	uint64_t written_;
	std::ofstream file_;
};

_WinWritableFile::_WinWritableFile(const std::string& path)
	: path_(path)
	, written_(0)
{
	Open();
}

_WinWritableFile::~_WinWritableFile(void)
{
	Close();
}

void _WinWritableFile::Open(void)
{
	// we truncate the file as implemented in env_posix
	file_.open(path_.generic_string().c_str(),
		std::ios_base::trunc | std::ios_base::out | std::ios_base::binary);
	written_ = 0;
}

Status _WinWritableFile::Append(const Slice& data)
{
	file_.write(data.data(), data.size());
	
	if (!file_.good()) {
		return Status::IOError(path_.generic_string() + " Append", "cannot write");
	}

	return Status::OK();
}

Status _WinWritableFile::Close(void)
{
	try {
		if (file_.is_open()) {
			Sync();
			file_.close();
		}
	} catch (const std::exception & e) {
		return Status::IOError(path_.generic_string() + " close", e.what());
	}

	return Status::OK();
}

Status _WinWritableFile::Flush(void)
{
	file_.flush();
	return Status::OK();
}

Status _WinWritableFile::Sync(void)
{
	try {
		Flush();
	} catch (const std::exception & e) {
		return Status::IOError(path_.string() + " sync", e.what());
	}

	return Status::OK();
}

class _WinFileLock : public FileLock
{
public:
	_WinFileLock(HANDLE file);
	~_WinFileLock(void);

private:
	HANDLE file_;
};

_WinFileLock::_WinFileLock(HANDLE file)
	: file_(file)
{
}

_WinFileLock::~_WinFileLock(void)
{
	if (file_ != INVALID_HANDLE_VALUE) {
		::CloseHandle(file_);
	}
}

class _WinLogger : public Logger
{
public:
	explicit _WinLogger(FILE* f);
	~_WinLogger(void);

	void Logv(const char* format, va_list ap) override;

private:
	FILE* file_;
};

_WinLogger::_WinLogger(FILE* f)
	: file_(f)
{
	assert(file_);
}

_WinLogger::~_WinLogger(void)
{
	fclose(file_);
}

void _WinLogger::Logv(const char* format, va_list ap) {
	const uint64_t thread_id = static_cast<uint64_t>(::GetCurrentThreadId());

	// We try twice: the first time with a fixed-size stack allocated buffer,
	// and the second time with a much larger dynamically allocated buffer.
	char buffer[500];

	for (int iter = 0; iter < 2; iter++) {
		char* base;
		int bufsize;
		if (iter == 0) {
			bufsize = sizeof(buffer);
			base = buffer;
		} else {
			bufsize = 30000;
			base = new char[bufsize];
		}

		char* p = base;
		char* limit = base + bufsize;

		SYSTEMTIME st;

		// GetSystemTime returns UTC time, we want local time!
		::GetLocalTime(&st);

		p += _snprintf_s(p, limit - p, _TRUNCATE,
			"%04d/%02d/%02d-%02d:%02d:%02d.%03d %llx ",
			st.wYear,
			st.wMonth,
			st.wDay,
			st.wHour,
			st.wMinute,
			st.wSecond,
			st.wMilliseconds,
			static_cast<long long unsigned int>(thread_id));

		// Print the message
		if (p < limit) {
			va_list backup_ap = ap;
			p += vsnprintf(p, limit - p, format, backup_ap);
			va_end(backup_ap);
		}

		// Truncate to available space if necessary
		if (p >= limit) {
			if (iter == 0) {
				continue; // Try again with larger buffer
			} else {
				p = limit - 1;
			}
		}

		// Add newline if necessary
		if (p == base || p[-1] != '\n') {
			*p++ = '\n';
		}

		assert(p <= limit);
		fwrite(base, 1, p - base, file_);
		fflush(file_);
		if (base != buffer) {
			delete[] base;
		}
		break;
	}
}


#ifdef DeleteFile 
#undef DeleteFile
#endif


class _WinEnv : public Env {
public:
	_WinEnv(void);
	~_WinEnv(void);

	Status NewSequentialFile(const std::string& fname, SequentialFile** result) override;
	Status NewRandomAccessFile(const std::string& fname, RandomAccessFile** result) override;
	Status NewWritableFile(const std::string& fname, WritableFile** result) override;

	bool FileExists(const std::string& fname) override;
	Status GetChildren(const std::string& dir, std::vector<std::string>* result) override;
	Status DeleteFile(const std::string& fname) override;
	Status CreateDir(const std::string& name) override;
	Status DeleteDir(const std::string& name) override;
	Status GetFileSize(const std::string& fname, uint64_t* size) override;
	Status RenameFile(const std::string& src, const std::string& target) override;
	Status LockFile(const std::string& fname, FileLock** lock) override;
	Status UnlockFile(FileLock* lock) override;

	void Schedule(void(*function)(void*), void* arg) override;
	void StartThread(void(*function)(void* arg), void* arg) override;

	Status GetTestDirectory(std::string* result) override;

	Status NewLogger(const std::string& fname, Logger** result) override;

	uint64_t NowMicros() override;
	void SleepForMicroseconds(int micros) override;

private:
	void BGThread(void);

	std::mutex mu_;
	std::condition_variable bgsignal_;
	std::unique_ptr<std::thread> bgthread_;
	std::deque<std::function<void(void)>> queue_;
};

_WinEnv::_WinEnv(void)
{
}

_WinEnv::~_WinEnv(void)
{
	fprintf(stderr, "Destroying Env::Default()\n");
	exit(1);
}

Status _WinEnv::NewSequentialFile(const std::string& fname, SequentialFile** result) {
	FILE* f;

	if ((f = ::fopen(fname.c_str(), "rb")) == nullptr) {
		*result = nullptr;
		return IOError(fname, errno);
	}

	*result = new _WinSequentialFile(fname, f);

	return Status::OK();
}

Status _WinEnv::NewRandomAccessFile(const std::string& fname, RandomAccessFile** result) {
	int fd;

	if ((fd = ::_open(fname.c_str(), _O_RDONLY | _O_RANDOM | _O_BINARY)) < 0) {
		*result = nullptr;
		return IOError(fname, errno);
	}

	*result = new _WinRandomAccessFile(fname, fd);

	return Status::OK();
}

Status _WinEnv::NewWritableFile(const std::string& fname, WritableFile** result)
{
	try {
		*result = new _WinWritableFile(fname);
	} catch (const std::exception & e) {
		return Status::IOError(fname, e.what());
	}

	return Status::OK();
}

bool _WinEnv::FileExists(const std::string& fname)
{
	return exp_fs::exists(fname);
}

Status _WinEnv::GetChildren(const std::string& dir, std::vector<std::string>* result)
{
	result->clear();

	std::error_code ec;
	exp_fs::directory_iterator current(dir, ec);
	exp_fs::directory_iterator end;

	if (ec) {
		return Status::IOError(dir, ec.message());
	}

	for (; current != end; ++current) {
		result->push_back(current->path().filename().generic_string());
	}

	return Status::OK();
}

Status _WinEnv::DeleteFile(const std::string& fname)
{
	std::error_code ec;

	exp_fs::remove(fname, ec);
	if (ec) {
		return Status::IOError(fname, ec.message());
	}

	return Status::OK();
}

Status _WinEnv::CreateDir(const std::string& name)
{
	if (exp_fs::exists(name) &&
		exp_fs::is_directory(name)) {
		return Status::OK();
	}

	std::error_code ec;
	if (!exp_fs::create_directories(name, ec)) {
		return Status::IOError(name, ec.message());
	}

	return Status::OK();
};

Status _WinEnv::DeleteDir(const std::string& name)
{
	std::error_code ec;
	if (!exp_fs::remove_all(name, ec)) {
		return Status::IOError(name, ec.message());
	}

	return Status::OK();
};

Status _WinEnv::GetFileSize(const std::string& fname, uint64_t* size)
{
	std::error_code ec;
	*size = static_cast<uint64_t>(exp_fs::file_size(fname, ec));

	if (ec) {
		*size = 0;
		return Status::IOError(fname, ec.message());
	}

	return Status::OK();
}

Status _WinEnv::RenameFile(const std::string& src, const std::string& target)
{
	std::error_code ec;
	exp_fs::rename(src, target, ec);

	if (ec) {
		return Status::IOError(src, ec.message());
	}

	return Status::OK();
}

std::shared_ptr<char> _GetWindowErrorMessage(void)
{
	DWORD errId = ::GetLastError();
	if (errId != ERROR_SUCCESS) {
		char* errMsgBuf = nullptr;
		size_t size = ::FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr,
			errId,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPSTR)&errMsgBuf,
			0,
			nullptr);
		if (size > 0) {
			return std::shared_ptr<char>(errMsgBuf, ::LocalFree);
		}
	}

	return std::shared_ptr<char>(nullptr);
}

Status _WinEnv::LockFile(const std::string& fname, FileLock** lock)
{
	HANDLE hFile = ::CreateFile(
		fname.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		0,
		nullptr,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	if (hFile == INVALID_HANDLE_VALUE) {
		*lock = nullptr;

		auto errMsg = _GetWindowErrorMessage();
		if (errMsg) {
			return Status::IOError("lock " + fname, std::string(errMsg.get()));
		} else {
			return Status::IOError("acquiring lock " + fname + " failed");
		}
	}

	*lock = new _WinFileLock(hFile);

	return Status::OK();
}

Status _WinEnv::UnlockFile(FileLock* lock)
{
	if (lock != nullptr) {
		delete lock;
	}

	return Status::OK();
}

void _WinEnv::Schedule(void(*function)(void*), void* arg) {
	std::unique_lock<std::mutex> lock(mu_);

	if (!bgthread_) {
		bgthread_ = std::make_unique<std::thread>([=]() -> void {
			this->BGThread();
		});
	}

	queue_.push_back(std::bind(function, arg));

	lock.unlock();

	bgsignal_.notify_one();
}

void _WinEnv::BGThread()
{
	while (true) {
		std::unique_lock<std::mutex> lock(mu_);

		while (queue_.empty()) {
			bgsignal_.wait(lock);
		}

		auto f = queue_.front();
		queue_.pop_front();

		lock.unlock();

		f();
	}
}

Status _WinEnv::GetTestDirectory(std::string* result)
{
	std::ostringstream pid;
	pid << ::GetCurrentProcessId();

	std::error_code ec;
	exp_fs::path temp_dir = exp_fs::temp_directory_path(ec);
	if (ec) {
		temp_dir = "tmp";
	}

	temp_dir /= "leveldb_tests";
	temp_dir /= pid.str();

	CreateDir(temp_dir.generic_string());

	*result = temp_dir.generic_string();

	return Status::OK();
}

Status _WinEnv::NewLogger(const std::string& fname, Logger** result) {
	FILE* f;

	if ((f = ::fopen(fname.c_str(), "wt")) == nullptr) {
		*result = nullptr;
		return Status::IOError(fname, strerror(errno));
	}

	*result = new _WinLogger(f);

	return Status::OK();
}

uint64_t _WinEnv::NowMicros()
{
	auto now = std::chrono::system_clock::now();

	time_t tnow = std::chrono::system_clock::to_time_t(now);
	tm *date = std::localtime(&tnow);
	date->tm_hour = 0;
	date->tm_min = 0;
	date->tm_sec = 0;
	auto midnight = std::chrono::system_clock::from_time_t(std::mktime(date));

	return std::chrono::duration_cast<std::chrono::microseconds>(now - midnight).count();
}

void _WinEnv::SleepForMicroseconds(int micros)
{
	std::this_thread::sleep_for(std::chrono::microseconds(micros));
}

void _WinEnv::StartThread(void(*function)(void* arg), void* arg) {
	std::thread t([function, arg](void) -> void {
		function(arg);
	});
}

_NS_ANONYMOUS_END_

Env* Env::Default() {
	static std::once_flag _onceFlag;
	static Env* _theEnv;

	std::call_once(_onceFlag, [&]()->void {
		_theEnv = new _WinEnv();
	});

	return _theEnv;
}


_NS_LEVELDB_END_
