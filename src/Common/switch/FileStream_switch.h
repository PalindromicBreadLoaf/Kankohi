// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "Common/precompiled.h"

struct HorizonHostFile;

// Every open path shares one host handle because Horizon refuses a second one. Yay.
// (I mean it makes sense why this wouldn't be an issue for them, but it's an issue
// for me and thus I'm going to complain about it.)
class FileStream
{
 public:
	static FileStream* openFile(std::string_view path);
	static FileStream* openFile(const wchar_t* path, bool allowWrite = false);
	static FileStream* openFile2(const fs::path& path, bool allowWrite = false);

	static FileStream* createFile(const wchar_t* path);
	static FileStream* createFile(std::string_view path);
	static FileStream* createFile2(const fs::path& path);

	static std::optional<std::vector<uint8>> LoadIntoMemory(const fs::path& path);

	void SetPosition(uint64 pos);

	uint64 GetSize();
	bool SetEndOfFile();
	void extract(std::vector<uint8>& data);

	void Flush();

	uint32 readData(void* data, uint32 length);
	bool readU64(uint64& v);
	bool readU32(uint32& v);
	bool readU16(uint16& v);
	bool readU8(uint8& v);
	bool readLine(std::string& line);

	sint32 writeData(const void* data, sint32 length);
	void writeU64(uint64 v);
	void writeU32(uint32 v);
	void writeU16(uint16 v);
	void writeU8(uint8 v);

	void writeStringFmt(const char* format, ...);
	void writeString(const char* str);
	void writeLine(const char* str);

	~FileStream();

	static bool WriteFileAtomic(const fs::path& path, std::span<uint8> fileData, bool allowTargetFileRename = false);

	static std::optional<uint64> GetOpenFileSize(const fs::path& path);
	static void FlushStaleWrites();
	static void FlushAllWrites();

 private:
	FileStream(HorizonHostFile* hostFile, bool isWriteable) : m_hostFile(hostFile), m_isWriteable(isWriteable) {};
	static FileStream* Open(const fs::path& path, bool allowWrite, bool truncate);

	HorizonHostFile* m_hostFile;
	uint64 m_position{0};
	bool m_isWriteable{false};
};
