// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Common/switch/FileStream_switch.h"
#include <cstdarg>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static constexpr uint64 MAX_PENDING_BYTES = 8 * 1024 * 1024;
static constexpr auto MAX_DIRTY_TIME = std::chrono::seconds(1);

struct HorizonHostFile
{
	std::string hostPath;
	std::string key;
	uint32 streamCount{0};

	std::mutex mutex;
	int fd{-1};
	bool fdWriteable{false};
	uint64 size{0};
	uint64 hostSize{0};
	uint64 hostValidSize{0};
	uint64 hostPosition{0};
	bool hostPositionValid{false};
	std::map<uint64, std::vector<uint8>> pending;
	uint64 pendingBytes{0};
	bool dirty{false};
	std::chrono::steady_clock::time_point dirtySince{};
	bool flushFailed{false};

	bool Reopen(bool writeable);
	bool SeekHost(uint64 offset);
	bool WriteHost(uint64 offset, const uint8* data, uint64 count);
	void MarkDirty();
	uint32 Read(uint64 offset, uint8* out, uint32 count);
	void Write(uint64 offset, const uint8* data, uint32 count);
	void Truncate(uint64 newSize);
	bool Flush();
	void FlushIfStale();
};

struct HorizonHostFileRegistry
{
	std::mutex mutex;
	std::unordered_map<std::string, std::unique_ptr<HorizonHostFile>> files;
};

static HorizonHostFileRegistry& GetRegistry()
{
	static HorizonHostFileRegistry s_registry;
	return s_registry;
}

static std::string MakeRegistryKey(const fs::path& path)
{
	std::string key = _pathToUtf8(path.lexically_normal());
	for (char& c : key)
		if (c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
	return key;
}

bool HorizonHostFile::Reopen(bool writeable)
{
	if (fd >= 0)
		close(fd);
	hostPositionValid = false;
	fd = open(hostPath.c_str(), writeable ? O_RDWR : O_RDONLY);
	if (fd >= 0)
	{
		fdWriteable = writeable;
		return true;
	}
	if (writeable)
		fd = open(hostPath.c_str(), O_RDONLY);
	fdWriteable = false;
	return false;
}

bool HorizonHostFile::SeekHost(uint64 offset)
{
	if (hostPositionValid && hostPosition == offset)
		return true;
	if (lseek(fd, (off_t)offset, SEEK_SET) < 0)
	{
		hostPositionValid = false;
		return false;
	}
	hostPosition = offset;
	hostPositionValid = true;
	return true;
}

bool HorizonHostFile::WriteHost(uint64 offset, const uint8* data, uint64 count)
{
	if (!SeekHost(offset))
		return false;
	while (count != 0)
	{
		ssize_t written = write(fd, data, (size_t)std::min<uint64>(count, 0x7FFFFFFF));
		if (written <= 0)
		{
			hostPositionValid = false;
			return false;
		}
		data += written;
		count -= (uint64)written;
		hostPosition += (uint64)written;
	}
	return true;
}

void HorizonHostFile::MarkDirty()
{
	if (dirty)
		return;
	dirty = true;
	dirtySince = std::chrono::steady_clock::now();
}

uint32 HorizonHostFile::Read(uint64 offset, uint8* out, uint32 count)
{
	if (offset >= size)
		return 0;
	uint32 length = (uint32)std::min<uint64>(count, size - offset);
	uint64 end = offset + length;

	uint32 hostLength = offset < hostValidSize ? (uint32)(std::min(end, hostValidSize) - offset) : 0;
	uint32 hostRead = 0;
	if (hostLength != 0 && SeekHost(offset))
	{
		while (hostRead < hostLength)
		{
			ssize_t r = read(fd, out + hostRead, hostLength - hostRead);
			if (r <= 0)
			{
				hostPositionValid = false;
				break;
			}
			hostRead += (uint32)r;
			hostPosition += (uint64)r;
		}
	}
	if (hostRead != hostLength)
	{
		length = hostRead;
		end = offset + length;
	}
	else
		std::memset(out + hostLength, 0, length - hostLength);

	auto it = pending.upper_bound(offset);
	if (it != pending.begin())
		--it;
	for (; it != pending.end() && it->first < end; ++it)
	{
		uint64 copyStart = std::max(offset, it->first);
		uint64 copyEnd = std::min(end, it->first + it->second.size());
		if (copyStart < copyEnd)
			std::memcpy(out + (copyStart - offset), it->second.data() + (copyStart - it->first), copyEnd - copyStart);
	}
	return length;
}

void HorizonHostFile::Write(uint64 offset, const uint8* data, uint32 count)
{
	if (count == 0)
		return;
	uint64 end = offset + count;

	if (count >= MAX_PENDING_BYTES && Flush() && offset <= hostSize)
	{
		if (WriteHost(offset, data, count))
		{
			hostSize = std::max(hostSize, end);
			hostValidSize = hostSize;
			size = hostSize;
			return;
		}
		cemuLog_log(LogType::Force, "FileStream: Direct write to {} failed", hostPath);
	}

	MarkDirty();
	auto first = pending.upper_bound(offset);
	if (first != pending.begin())
	{
		auto prev = std::prev(first);
		if (prev->first + prev->second.size() >= offset)
			first = prev;
	}
	auto last = first;
	while (last != pending.end() && last->first <= end)
		++last;

	if (first == last)
	{
		pending.emplace(offset, std::vector<uint8>(data, data + count));
		pendingBytes += count;
	}
	else
	{
		uint64 mergedStart = std::min(offset, first->first);
		auto finalExtent = std::prev(last);
		uint64 mergedEnd = std::max(end, finalExtent->first + finalExtent->second.size());

		std::vector<uint8> merged;
		auto it = first;
		if (first->first == mergedStart)
		{
			merged = std::move(first->second);
			pendingBytes -= merged.size();
			++it;
		}
		merged.resize(mergedEnd - mergedStart);
		for (; it != last; ++it)
		{
			std::copy(it->second.begin(), it->second.end(), merged.begin() + (it->first - mergedStart));
			pendingBytes -= it->second.size();
		}
		std::copy(data, data + count, merged.begin() + (offset - mergedStart));

		pending.erase(first, last);
		pendingBytes += merged.size();
		pending.emplace(mergedStart, std::move(merged));
	}

	size = std::max(size, end);
	if (pendingBytes >= MAX_PENDING_BYTES)
		Flush();
	else
		FlushIfStale();
}

void HorizonHostFile::Truncate(uint64 newSize)
{
	MarkDirty();
	if (newSize < size)
	{
		hostValidSize = std::min(hostValidSize, newSize);
		auto first = pending.lower_bound(newSize);
		for (auto it = first; it != pending.end(); ++it)
			pendingBytes -= it->second.size();
		pending.erase(first, pending.end());
		if (!pending.empty())
		{
			auto& [extentStart, extent] = *std::prev(pending.end());
			if (extentStart + extent.size() > newSize)
			{
				pendingBytes -= extent.size() - (newSize - extentStart);
				extent.resize(newSize - extentStart);
			}
		}
	}
	size = newSize;
}

bool HorizonHostFile::Flush()
{
	if (!dirty)
		return true;

	bool ok = fdWriteable;
	if (ok && hostValidSize < hostSize)
		ok = ftruncate(fd, (off_t)hostValidSize) == 0;
	if (ok && size != hostValidSize)
		ok = ftruncate(fd, (off_t)size) == 0;
	for (auto it = pending.begin(); ok && it != pending.end(); ++it)
		ok = WriteHost(it->first, it->second.data(), it->second.size());
	ok = ok && fsync(fd) == 0;

	if (!ok)
	{
		if (!flushFailed)
			cemuLog_log(LogType::Force, "FileStream: Failed to write back {}: {}", hostPath, strerror(errno));
		flushFailed = true;
		dirtySince = std::chrono::steady_clock::now();
		return false;
	}

	pending.clear();
	pendingBytes = 0;
	hostSize = size;
	hostValidSize = size;
	dirty = false;
	flushFailed = false;
	return true;
}

void HorizonHostFile::FlushIfStale()
{
	if (dirty && std::chrono::steady_clock::now() - dirtySince >= MAX_DIRTY_TIME)
		Flush();
}

FileStream* FileStream::Open(const fs::path& path, bool allowWrite, bool truncate)
{
	bool writeable = allowWrite || truncate;
	std::string key = MakeRegistryKey(path);
	HorizonHostFileRegistry& registry = GetRegistry();
	std::lock_guard registryLock(registry.mutex);

	if (auto it = registry.files.find(key); it != registry.files.end())
	{
		HorizonHostFile* hostFile = it->second.get();
		std::lock_guard fileLock(hostFile->mutex);
		if (writeable && !hostFile->fdWriteable && !hostFile->Reopen(true))
			return nullptr;
		if (truncate)
			hostFile->Truncate(0);
		hostFile->streamCount++;
		return new FileStream(hostFile, writeable);
	}

	std::string hostPath = _pathToUtf8(path);
	int fd = open(hostPath.c_str(), truncate ? (O_RDWR | O_CREAT | O_TRUNC) : (writeable ? O_RDWR : O_RDONLY), 0666);
	if (fd < 0)
		return nullptr;
	struct stat st;
	if (fstat(fd, &st) != 0 || S_ISDIR(st.st_mode))
	{
		close(fd);
		return nullptr;
	}

	auto hostFile = std::make_unique<HorizonHostFile>();
	hostFile->hostPath = std::move(hostPath);
	hostFile->key = key;
	hostFile->streamCount = 1;
	hostFile->fd = fd;
	hostFile->fdWriteable = writeable;
	hostFile->size = (uint64)st.st_size;
	hostFile->hostSize = hostFile->size;
	hostFile->hostValidSize = hostFile->size;
	FileStream* stream = new FileStream(hostFile.get(), writeable);
	registry.files.emplace(std::move(key), std::move(hostFile));
	return stream;
}

FileStream::~FileStream()
{
	HorizonHostFileRegistry& registry = GetRegistry();
	std::lock_guard registryLock(registry.mutex);
	if (--m_hostFile->streamCount != 0)
		return;
	if (!m_hostFile->Flush())
		cemuLog_log(LogType::Force, "FileStream: Closing {} with unwritten data", m_hostFile->hostPath);
	if (m_hostFile->fd >= 0)
		close(m_hostFile->fd);
	std::string key = std::move(m_hostFile->key);
	registry.files.erase(key);
}

FileStream* FileStream::openFile(std::string_view path)
{
	return openFile2(path, false);
}

FileStream* FileStream::openFile(const wchar_t* path, bool allowWrite)
{
	return openFile2(path, allowWrite);
}

FileStream* FileStream::openFile2(const fs::path& path, bool allowWrite)
{
	return Open(path, allowWrite, false);
}

FileStream* FileStream::createFile(const wchar_t* path)
{
	return createFile2(path);
}

FileStream* FileStream::createFile(std::string_view path)
{
	return createFile2(path);
}

FileStream* FileStream::createFile2(const fs::path& path)
{
	return Open(path, true, true);
}

std::optional<std::vector<uint8>> FileStream::LoadIntoMemory(const fs::path& path)
{
	FileStream* fs = openFile2(path);
	if (!fs)
		return std::nullopt;
	uint64 fileSize = fs->GetSize();
	if (fileSize > 0xFFFFFFFFull)
	{
		delete fs;
		return std::nullopt;
	}
	std::optional<std::vector<uint8>> v(fileSize);
	if (fs->readData(v->data(), (uint32)fileSize) != (uint32)fileSize)
	{
		delete fs;
		return std::nullopt;
	}
	delete fs;
	return v;
}

std::optional<uint64> FileStream::GetOpenFileSize(const fs::path& path)
{
	HorizonHostFileRegistry& registry = GetRegistry();
	std::lock_guard registryLock(registry.mutex);
	auto it = registry.files.find(MakeRegistryKey(path));
	if (it == registry.files.end())
		return std::nullopt;
	std::lock_guard fileLock(it->second->mutex);
	return it->second->size;
}

void FileStream::FlushStaleWrites()
{
	HorizonHostFileRegistry& registry = GetRegistry();
	std::lock_guard registryLock(registry.mutex);
	for (auto& [key, hostFile] : registry.files)
	{
		std::lock_guard fileLock(hostFile->mutex);
		hostFile->FlushIfStale();
	}
}

void FileStream::FlushAllWrites()
{
	HorizonHostFileRegistry& registry = GetRegistry();
	std::lock_guard registryLock(registry.mutex);
	for (auto& [key, hostFile] : registry.files)
	{
		std::lock_guard fileLock(hostFile->mutex);
		hostFile->Flush();
	}
}

void FileStream::SetPosition(uint64 pos)
{
	m_position = pos;
}

uint64 FileStream::GetSize()
{
	std::lock_guard lock(m_hostFile->mutex);
	return m_hostFile->size;
}

bool FileStream::SetEndOfFile()
{
	if (!m_isWriteable)
		return false;
	std::lock_guard lock(m_hostFile->mutex);
	m_hostFile->Truncate(m_position);
	return true;
}

void FileStream::extract(std::vector<uint8>& data)
{
	uint64 fileSize = GetSize();
	SetPosition(0);
	data.resize(fileSize);
	readData(data.data(), fileSize);
}

void FileStream::Flush()
{
	std::lock_guard lock(m_hostFile->mutex);
	m_hostFile->Flush();
}

uint32 FileStream::readData(void* data, uint32 length)
{
	std::lock_guard lock(m_hostFile->mutex);
	uint32 bytesRead = m_hostFile->Read(m_position, (uint8*)data, length);
	m_position += bytesRead;
	return bytesRead;
}

bool FileStream::readU64(uint64& v)
{
	return readData(&v, sizeof(uint64)) == sizeof(uint64);
}

bool FileStream::readU32(uint32& v)
{
	return readData(&v, sizeof(uint32)) == sizeof(uint32);
}

bool FileStream::readU16(uint16& v)
{
	return readData(&v, sizeof(uint16)) == sizeof(uint16);
}

bool FileStream::readU8(uint8& v)
{
	return readData(&v, sizeof(uint8)) == sizeof(uint8);
}

bool FileStream::readLine(std::string& line)
{
	line.clear();
	uint8 c;
	bool isEOF = true;
	while (readU8(c))
	{
		isEOF = false;
		if (c == '\r')
			continue;
		if (c == '\n')
			break;
		line.push_back((char)c);
	}
	return !isEOF;
}

sint32 FileStream::writeData(const void* data, sint32 length)
{
	if (!m_isWriteable || length <= 0)
		return 0;
	std::lock_guard lock(m_hostFile->mutex);
	m_hostFile->Write(m_position, (const uint8*)data, (uint32)length);
	m_position += (uint64)length;
	return length;
}

void FileStream::writeU64(uint64 v)
{
	writeData(&v, sizeof(uint64));
}

void FileStream::writeU32(uint32 v)
{
	writeData(&v, sizeof(uint32));
}

void FileStream::writeU16(uint16 v)
{
	writeData(&v, sizeof(uint16));
}

void FileStream::writeU8(uint8 v)
{
	writeData(&v, sizeof(uint8));
}

void FileStream::writeStringFmt(const char* format, ...)
{
	char buffer[2048];
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	writeData(buffer, (sint32)strlen(buffer));
}

void FileStream::writeString(const char* str)
{
	writeData(str, (sint32)strlen(str));
}

void FileStream::writeLine(const char* str)
{
	writeData(str, (sint32)strlen(str));
	writeData("\r\n", 2);
}

bool FileStream::WriteFileAtomic(const fs::path& path, std::span<uint8> fileData, bool allowTargetFileRename)
{
	std::error_code ec;
	fs::path tempPath = path;
	tempPath.replace_extension(_utf8ToPath(_pathToUtf8(tempPath.extension()).append("_tmp")));
	FileStream* tempFile = FileStream::createFile2(tempPath);
	if (!tempFile)
		return false;
	bool written = tempFile->writeData(fileData.data(), (sint32)fileData.size()) == (sint32)fileData.size();
	{
		std::lock_guard lock(tempFile->m_hostFile->mutex);
		written = written && tempFile->m_hostFile->Flush();
	}
	delete tempFile;
	if (!written)
	{
		fs::remove(tempPath, ec);
		return false;
	}

	fs::rename(tempPath, path, ec);
	if (!ec)
		return true;
	std::error_code removeError;
	if (!fs::remove(path, removeError))
	{
		cemuLog_log(LogType::Force, "Failed to replace file {} with error {}", _pathToUtf8(path), ec.message());
		fs::remove(tempPath, removeError);
		return false;
	}
	ec.clear();
	fs::rename(tempPath, path, ec);
	if (ec)
	{
		cemuLog_log(LogType::Force, "Failed to replace file {} with error {}. The new contents are in {}", _pathToUtf8(path), ec.message(), _pathToUtf8(tempPath));
		return false;
	}
	return true;
}
