#include <Windows.h>
#include <shellapi.h>
#include <wincrypt.h>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <iomanip>

// bzfile_replace_helper.exe promotes OpenShim updates staged by bzfile.dll
// once the game has exited. Two modes, both hash-verified:
//
//   <pid> <staged> <destination> <log> <sha256> <backup> <status>
//   --suite <pid> <log> <status> then three groups of
//       <staged> <destination> <sha256> <backup>
//
// Exit codes: 0 installed, 1 failed (status says why), 2 bad arguments,
// 3 another helper already owns the update (its status is left alone).

namespace
{
	constexpr wchar_t kUpdateMutexName[] = L"Local\\BZR_OpenShim_Update";
	constexpr int kExitInstalled = 0;
	constexpr int kExitFailed = 1;
	constexpr int kExitBadArguments = 2;
	constexpr int kExitAlreadyActive = 3;

	struct CryptProvider
	{
		HCRYPTPROV handle = 0;
		~CryptProvider() { if (handle) CryptReleaseContext(handle, 0); }
	};

	struct CryptHash
	{
		HCRYPTHASH handle = 0;
		~CryptHash() { if (handle) CryptDestroyHash(handle); }
	};

	struct ScopedHandle
	{
		HANDLE handle = nullptr;
		~ScopedHandle() { if (handle != nullptr) CloseHandle(handle); }
	};

	std::string Utf8FromWide(const std::wstring& value)
	{
		if (value.empty())
		{
			return {};
		}

		int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (size <= 0)
		{
			return {};
		}

		std::string converted(static_cast<size_t>(size), '\0');
		WideCharToMultiByte(
			CP_UTF8,
			0,
			value.c_str(),
			-1,
			converted.data(),
			size,
			nullptr,
			nullptr);
		converted.pop_back();
		return converted;
	}

	std::wstring FormatWindowsError(DWORD errorCode)
	{
		LPWSTR buffer = nullptr;
		DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
		DWORD length = FormatMessageW(
			flags,
			nullptr,
			errorCode,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			reinterpret_cast<LPWSTR>(&buffer),
			0,
			nullptr);
		if (length == 0 || buffer == nullptr)
		{
			std::wstringstream fallback;
			fallback << L"error " << errorCode;
			return fallback.str();
		}

		std::wstring message(buffer, length);
		LocalFree(buffer);

		while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' '))
		{
			message.pop_back();
		}

		return message;
	}

	std::wstring TimestampNow()
	{
		SYSTEMTIME now = {};
		GetLocalTime(&now);

		std::wstringstream stamp;
		stamp
			<< std::setfill(L'0')
			<< std::setw(4) << now.wYear << L'-'
			<< std::setw(2) << now.wMonth << L'-'
			<< std::setw(2) << now.wDay << L' '
			<< std::setw(2) << now.wHour << L':'
			<< std::setw(2) << now.wMinute << L':'
			<< std::setw(2) << now.wSecond << L'.'
			<< std::setw(3) << now.wMilliseconds;
		return stamp.str();
	}

	// Non-throwing existence check; the std::filesystem::exists overload
	// without an error_code throws on anything but "not found".
	bool FileExists(const std::filesystem::path& path)
	{
		return !path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
	}

	void AppendLogLine(const std::filesystem::path& logPath, const std::wstring& message)
	{
		const std::string line = Utf8FromWide(TimestampNow() + L" " + message + L"\r\n");
		if (line.empty())
		{
			return;
		}

		HANDLE handle = CreateFileW(
			logPath.c_str(),
			FILE_APPEND_DATA,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr,
			OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);
		if (handle == INVALID_HANDLE_VALUE)
		{
			return;
		}

		DWORD bytesWritten = 0;
		WriteFile(handle, line.data(), static_cast<DWORD>(line.size()), &bytesWritten, nullptr);
		CloseHandle(handle);
	}

	void WriteStatus(
		const std::filesystem::path& statusPath,
		const std::wstring& state,
		const std::wstring& expectedHash,
		const std::wstring& detail)
	{
		if (statusPath.empty())
		{
			return;
		}

		const std::wstring content =
			L"state=" + state + L"\r\n" +
			L"expected_sha256=" + expectedHash + L"\r\n" +
			L"detail=" + detail + L"\r\n" +
			L"updated=" + TimestampNow() + L"\r\n";
		const std::string utf8 = Utf8FromWide(content);
		if (utf8.empty())
		{
			return;
		}

		HANDLE handle = CreateFileW(
			statusPath.c_str(),
			GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr,
			CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);
		if (handle == INVALID_HANDLE_VALUE)
		{
			return;
		}

		DWORD bytesWritten = 0;
		WriteFile(handle, utf8.data(), static_cast<DWORD>(utf8.size()), &bytesWritten, nullptr);
		CloseHandle(handle);
	}

	bool ComputeSha256(const std::filesystem::path& filePath, std::wstring& result, std::wstring& errorMessage)
	{
		std::ifstream input(filePath, std::ios::binary);
		if (!input.is_open())
		{
			errorMessage = L"could not open file";
			return false;
		}

		CryptProvider provider;
		if (!CryptAcquireContextW(&provider.handle, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
		{
			errorMessage = L"CryptAcquireContext failed: " + FormatWindowsError(GetLastError());
			return false;
		}

		CryptHash hash;
		if (!CryptCreateHash(provider.handle, CALG_SHA_256, 0, 0, &hash.handle))
		{
			errorMessage = L"CryptCreateHash failed: " + FormatWindowsError(GetLastError());
			return false;
		}

		std::vector<char> buffer(64 * 1024);
		for (;;)
		{
			input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
			const auto bytesRead = input.gcount();
			if (bytesRead > 0
				&& !CryptHashData(hash.handle, reinterpret_cast<const BYTE*>(buffer.data()), static_cast<DWORD>(bytesRead), 0))
			{
				errorMessage = L"CryptHashData failed: " + FormatWindowsError(GetLastError());
				return false;
			}

			if (!input)
			{
				break;
			}
		}

		// A short read ends the loop with eofbit set; anything else (badbit
		// without eof) is a read error, not the end of the file, and must not
		// produce the hash of a truncated read.
		if (!input.eof())
		{
			errorMessage = L"read error while hashing";
			return false;
		}

		DWORD hashLength = 0;
		DWORD hashLengthSize = sizeof(hashLength);
		if (!CryptGetHashParam(hash.handle, HP_HASHSIZE, reinterpret_cast<BYTE*>(&hashLength), &hashLengthSize, 0))
		{
			errorMessage = L"CryptGetHashParam(size) failed";
			return false;
		}

		std::vector<BYTE> hashBytes(hashLength);
		if (!CryptGetHashParam(hash.handle, HP_HASHVAL, hashBytes.data(), &hashLength, 0))
		{
			errorMessage = L"CryptGetHashParam(value) failed";
			return false;
		}

		std::wstringstream hex;
		hex << std::hex << std::setfill(L'0');
		for (BYTE value : hashBytes)
		{
			hex << std::setw(2) << static_cast<unsigned int>(value);
		}
		result = hex.str();
		return true;
	}

	// Puts a copy of source at destination: copy beside the destination, then
	// rename over it. The rename is atomic on one volume (and a POSIX rename
	// under Wine), so the destination is either the old file or the complete
	// new one. A direct MoveFileEx from the staging folder is not: the
	// Workshop folder and the game can sit on different drives, where
	// MOVEFILE_COPY_ALLOWED degrades to copy-then-delete. Retries cover a
	// destination the exiting game still holds open for a moment.
	bool InstallCopy(
		const std::filesystem::path& sourcePath,
		const std::filesystem::path& destinationPath,
		const std::filesystem::path& logPath)
	{
		constexpr int kMaxAttempts = 120;
		constexpr DWORD kDelayMilliseconds = 250;

		std::filesystem::path temporaryPath = destinationPath;
		temporaryPath += L".bzfile-promote";

		if (!CopyFileW(sourcePath.c_str(), temporaryPath.c_str(), FALSE))
		{
			AppendLogLine(logPath, L"Could not copy " + sourcePath.wstring() + L" beside the destination: "
				+ FormatWindowsError(GetLastError()));
			DeleteFileW(temporaryPath.c_str());
			return false;
		}
		SetFileAttributesW(temporaryPath.c_str(), FILE_ATTRIBUTE_NORMAL);

		for (int attempt = 1; attempt <= kMaxAttempts; ++attempt)
		{
			SetFileAttributesW(destinationPath.c_str(), FILE_ATTRIBUTE_NORMAL);
			if (MoveFileExW(
				temporaryPath.c_str(),
				destinationPath.c_str(),
				MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			{
				AppendLogLine(logPath, L"Installed " + destinationPath.wstring()
					+ L" on attempt " + std::to_wstring(attempt) + L".");
				return true;
			}

			const DWORD moveError = GetLastError();
			if (attempt <= 5 || attempt == kMaxAttempts || attempt % 10 == 0)
			{
				AppendLogLine(logPath, L"Attempt " + std::to_wstring(attempt) + L" to replace "
					+ destinationPath.wstring() + L" failed: " + FormatWindowsError(moveError));
			}

			Sleep(kDelayMilliseconds);
		}

		DeleteFileW(temporaryPath.c_str());
		return false;
	}

	bool PromoteReplacement(
		const std::filesystem::path& stagedPath,
		const std::filesystem::path& destinationPath,
		const std::filesystem::path& logPath)
	{
		if (!InstallCopy(stagedPath, destinationPath, logPath))
		{
			return false;
		}

		if (!DeleteFileW(stagedPath.c_str()))
		{
			AppendLogLine(logPath, L"Installed, but could not remove the staged file " + stagedPath.wstring()
				+ L": " + FormatWindowsError(GetLastError()));
		}
		return true;
	}

	// Undoes one promotion: puts the backup back if the destination existed
	// before, otherwise removes the file this update created. Restoring an
	// unrelated older backup in the second case would install stale bytes.
	bool UndoPromotion(
		bool destinationExisted,
		const std::filesystem::path& backupPath,
		const std::filesystem::path& destinationPath,
		const std::filesystem::path& logPath)
	{
		if (!destinationExisted)
		{
			SetFileAttributesW(destinationPath.c_str(), FILE_ATTRIBUTE_NORMAL);
			if (!DeleteFileW(destinationPath.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND)
			{
				AppendLogLine(logPath, L"Rollback could not remove " + destinationPath.wstring() + L": "
					+ FormatWindowsError(GetLastError()));
				return false;
			}
			AppendLogLine(logPath, L"Removed newly installed " + destinationPath.wstring() + L".");
			return true;
		}

		if (!FileExists(backupPath))
		{
			AppendLogLine(logPath, L"No backup is available to restore " + destinationPath.wstring() + L".");
			return false;
		}

		if (!InstallCopy(backupPath, destinationPath, logPath))
		{
			AppendLogLine(logPath, L"Rollback could not restore " + destinationPath.wstring() + L".");
			return false;
		}

		AppendLogLine(logPath, L"Restored previous " + destinationPath.wstring() + L".");
		return true;
	}

	bool CreateBackup(
		const std::filesystem::path& destinationPath,
		const std::filesystem::path& backupPath,
		const std::filesystem::path& logPath)
	{
		SetFileAttributesW(backupPath.c_str(), FILE_ATTRIBUTE_NORMAL);
		if (!CopyFileW(destinationPath.c_str(), backupPath.c_str(), FALSE))
		{
			AppendLogLine(logPath, L"Could not back up " + destinationPath.wstring() + L": "
				+ FormatWindowsError(GetLastError()));
			return false;
		}
		AppendLogLine(logPath, L"Backed up " + destinationPath.wstring() + L" to " + backupPath.wstring() + L".");
		return true;
	}

	bool WaitForProcessExit(DWORD processId, const std::filesystem::path& logPath)
	{
		if (processId == 0)
		{
			AppendLogLine(logPath, L"No game process id was given.");
			return false;
		}

		HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
		if (process == nullptr)
		{
			const DWORD openError = GetLastError();
			if (openError == ERROR_INVALID_PARAMETER)
			{
				AppendLogLine(logPath, L"Process already exited before helper wait began.");
				Sleep(1000); // Give some extra time for handles to release
				return true;
			}

			AppendLogLine(logPath, L"OpenProcess failed: " + FormatWindowsError(openError));
			return false;
		}

		// The game launches this helper, so the game was created first. A
		// process with the same id created after the helper is a recycled id,
		// which means the game has already gone. This is what makes the
		// unbounded wait below safe: it can only be waiting on the game.
		FILETIME targetCreated = {};
		FILETIME helperCreated = {};
		FILETIME unused[3] = {};
		if (GetProcessTimes(process, &targetCreated, &unused[0], &unused[1], &unused[2])
			&& GetProcessTimes(GetCurrentProcess(), &helperCreated, &unused[0], &unused[1], &unused[2])
			&& CompareFileTime(&targetCreated, &helperCreated) > 0)
		{
			CloseHandle(process);
			AppendLogLine(logPath, L"Process id " + std::to_wstring(processId)
				+ L" now belongs to a newer process; the game has already exited.");
			Sleep(1000);
			return true;
		}

		// No timeout. A player can keep playing for hours after an update is
		// staged; the previous 10-minute cap abandoned those updates.
		AppendLogLine(logPath, L"Waiting for process " + std::to_wstring(processId) + L" to exit.");
		const DWORD waitResult = WaitForSingleObject(process, INFINITE);
		const DWORD waitError = GetLastError();
		CloseHandle(process);

		if (waitResult == WAIT_OBJECT_0)
		{
			AppendLogLine(logPath, L"Observed target process exit.");
			Sleep(1000); // Give some extra time for handles to release
			return true;
		}

		AppendLogLine(logPath, L"WaitForSingleObject failed: " + FormatWindowsError(waitError));
		return false;
	}

	// Takes the update mutex. Returns false when another helper owns it; that
	// helper also owns the status file, so this one must not write it.
	bool AcquireUpdateMutex(ScopedHandle& mutex, const std::filesystem::path& logPath)
	{
		mutex.handle = CreateMutexW(nullptr, FALSE, kUpdateMutexName);
		if (mutex.handle != nullptr && GetLastError() == ERROR_ALREADY_EXISTS)
		{
			AppendLogLine(logPath, L"Another OpenShim update helper is already active; leaving its update alone.");
			return false;
		}
		return true;
	}

	struct SuitePayload
	{
		std::filesystem::path stagedPath;
		std::filesystem::path destinationPath;
		std::wstring expectedHash;
		std::filesystem::path backupPath;
		bool destinationExisted = false;
	};

	// Rolls back the payloads that were actually promoted, newest first.
	// Destinations that were never touched are not "restored": doing so used
	// to report a correct rollback as incomplete whenever an untouched
	// destination was locked.
	bool RollBackSuite(
		const std::vector<SuitePayload>& payloads,
		size_t promotedCount,
		const std::filesystem::path& logPath)
	{
		bool allRestored = true;
		for (size_t index = promotedCount; index > 0; --index)
		{
			const auto& payload = payloads[index - 1];
			if (!UndoPromotion(payload.destinationExisted, payload.backupPath, payload.destinationPath, logPath))
			{
				allRestored = false;
			}
		}
		return allRestored;
	}

	void RemoveStagedFiles(const std::vector<SuitePayload>& payloads)
	{
		for (const auto& payload : payloads)
		{
			SetFileAttributesW(payload.stagedPath.c_str(), FILE_ATTRIBUTE_NORMAL);
			DeleteFileW(payload.stagedPath.c_str());
		}
	}

	int RunSuiteUpdate(const std::vector<std::wstring>& arguments)
	{
		// executable, --suite/--suite-v3, pid, log, status, then three/five groups of
		// staged/destination/hash/backup.
		const size_t expectedCount = arguments[1] == L"--suite-v3" ? 25 : 17;
		if (arguments.size() != expectedCount)
		{
			if (arguments.size() >= 5)
			{
				WriteStatus(arguments[4], L"failed", L"",
					L"helper received " + std::to_wstring(arguments.size()) + L" arguments; expected " +
					std::to_wstring(expectedCount) + L" (version mismatch?)");
			}
			return kExitBadArguments;
		}

		const DWORD processId = static_cast<DWORD>(wcstoul(arguments[2].c_str(), nullptr, 10));
		const std::filesystem::path logPath(arguments[3]);
		const std::filesystem::path statusPath(arguments[4]);
		std::vector<SuitePayload> payloads;
		for (size_t index = 5; index < arguments.size(); index += 4)
		{
			payloads.push_back({
				std::filesystem::path(arguments[index]),
				std::filesystem::path(arguments[index + 1]),
				arguments[index + 2],
				std::filesystem::path(arguments[index + 3]),
				false
			});
		}
		const std::wstring suiteHash = payloads.front().expectedHash;

		ScopedHandle updateMutex;
		if (!AcquireUpdateMutex(updateMutex, logPath))
		{
			return kExitAlreadyActive;
		}

		AppendLogLine(logPath, L"OpenShim suite update helper started.");
		for (const auto& payload : payloads)
		{
			AppendLogLine(logPath, L"Validating staged payload: " + payload.stagedPath.wstring());
			if (!FileExists(payload.stagedPath))
			{
				WriteStatus(statusPath, L"failed", suiteHash, L"a staged suite payload is missing");
				RemoveStagedFiles(payloads);
				return kExitFailed;
			}

			std::wstring stagedHash;
			std::wstring hashError;
			if (!ComputeSha256(payload.stagedPath, stagedHash, hashError) || stagedHash != payload.expectedHash)
			{
				AppendLogLine(logPath, L"Staged suite payload hash validation failed: " + payload.stagedPath.wstring());
				WriteStatus(statusPath, L"failed", suiteHash, L"staged suite payload hash mismatch");
				RemoveStagedFiles(payloads);
				return kExitFailed;
			}
		}

		WriteStatus(statusPath, L"waiting_for_exit", suiteHash, L"all suite payloads verified");
		if (!WaitForProcessExit(processId, logPath))
		{
			WriteStatus(statusPath, L"failed", suiteHash, L"could not wait for game process exit");
			RemoveStagedFiles(payloads);
			return kExitFailed;
		}

		// The staged files sat in a script-writable folder while the game ran;
		// check them again now that nothing else should be touching them.
		for (const auto& payload : payloads)
		{
			std::wstring stagedHash;
			std::wstring hashError;
			if (!ComputeSha256(payload.stagedPath, stagedHash, hashError) || stagedHash != payload.expectedHash)
			{
				AppendLogLine(logPath, L"Staged suite payload changed while waiting: " + payload.stagedPath.wstring());
				WriteStatus(statusPath, L"failed", suiteHash, L"a staged suite payload changed before promotion");
				RemoveStagedFiles(payloads);
				return kExitFailed;
			}
		}

		WriteStatus(statusPath, L"promoting", suiteHash, L"backing up and installing the suite");
		for (auto& payload : payloads)
		{
			std::error_code directoryError;
			std::filesystem::create_directories(payload.destinationPath.parent_path(), directoryError);
			if (directoryError)
			{
				AppendLogLine(logPath, L"Could not create destination directory for " + payload.destinationPath.wstring());
				WriteStatus(statusPath, L"failed", suiteHash, L"could not create destination directory");
				RemoveStagedFiles(payloads);
				return kExitFailed;
			}

			payload.destinationExisted = FileExists(payload.destinationPath);
			if (payload.destinationExisted && !CreateBackup(payload.destinationPath, payload.backupPath, logPath))
			{
				WriteStatus(statusPath, L"failed", suiteHash, L"could not back up existing suite payload");
				RemoveStagedFiles(payloads);
				return kExitFailed;
			}
		}

		size_t promotedCount = 0;
		for (const auto& payload : payloads)
		{
			AppendLogLine(logPath, L"Promoting suite payload to: " + payload.destinationPath.wstring());
			if (!PromoteReplacement(payload.stagedPath, payload.destinationPath, logPath))
			{
				const bool restored = RollBackSuite(payloads, promotedCount, logPath);
				RemoveStagedFiles(payloads);
				WriteStatus(statusPath, L"failed", suiteHash,
					restored ? L"suite promotion failed; previous files restored" : L"suite promotion failed; rollback incomplete");
				return kExitFailed;
			}
			++promotedCount;

			std::wstring destinationHash;
			std::wstring hashError;
			if (!ComputeSha256(payload.destinationPath, destinationHash, hashError) || destinationHash != payload.expectedHash)
			{
				AppendLogLine(logPath, L"Installed suite payload hash validation failed: " + payload.destinationPath.wstring());
				const bool restored = RollBackSuite(payloads, promotedCount, logPath);
				RemoveStagedFiles(payloads);
				WriteStatus(statusPath, L"failed", suiteHash,
					restored ? L"installed suite hash mismatch; previous files restored" : L"installed suite hash mismatch; rollback incomplete");
				return kExitFailed;
			}
		}

		WriteStatus(statusPath, L"complete", suiteHash, L"all suite payloads installed and verified");
		AppendLogLine(logPath, L"OpenShim suite update completed successfully.");
		return kExitInstalled;
	}

	int RunSingleUpdate(const std::vector<std::wstring>& arguments)
	{
		// executable, pid, staged, destination, log, hash, backup, status.
		if (arguments.size() != 8)
		{
			return kExitBadArguments;
		}

		const DWORD processId = static_cast<DWORD>(wcstoul(arguments[1].c_str(), nullptr, 10));
		const std::filesystem::path stagedPath(arguments[2]);
		const std::filesystem::path destinationPath(arguments[3]);
		const std::filesystem::path logPath(arguments[4]);
		const std::wstring expectedHash = arguments[5];
		const std::filesystem::path backupPath(arguments[6]);
		const std::filesystem::path statusPath(arguments[7]);

		ScopedHandle updateMutex;
		if (!AcquireUpdateMutex(updateMutex, logPath))
		{
			return kExitAlreadyActive;
		}

		AppendLogLine(logPath, L"bzfile replace helper started.");
		AppendLogLine(logPath, L"Staged: " + stagedPath.wstring());
		AppendLogLine(logPath, L"Destination: " + destinationPath.wstring());

		if (!FileExists(stagedPath))
		{
			AppendLogLine(logPath, L"Staged file is missing before replacement.");
			WriteStatus(statusPath, L"failed", expectedHash, L"staged file is missing");
			return kExitFailed;
		}

		std::wstring stagedHash;
		std::wstring hashError;
		if (!ComputeSha256(stagedPath, stagedHash, hashError) || stagedHash != expectedHash)
		{
			AppendLogLine(logPath, L"Staged payload hash validation failed: " + hashError);
			WriteStatus(statusPath, L"failed", expectedHash, L"staged payload hash mismatch");
			DeleteFileW(stagedPath.c_str());
			return kExitFailed;
		}
		AppendLogLine(logPath, L"Staged payload SHA-256 verified: " + stagedHash);
		WriteStatus(statusPath, L"waiting_for_exit", expectedHash, L"payload verified");

		if (!WaitForProcessExit(processId, logPath))
		{
			WriteStatus(statusPath, L"failed", expectedHash, L"could not wait for game process exit");
			DeleteFileW(stagedPath.c_str());
			return kExitFailed;
		}

		if (!ComputeSha256(stagedPath, stagedHash, hashError) || stagedHash != expectedHash)
		{
			AppendLogLine(logPath, L"Staged payload changed while waiting.");
			WriteStatus(statusPath, L"failed", expectedHash, L"staged payload changed before promotion");
			DeleteFileW(stagedPath.c_str());
			return kExitFailed;
		}

		WriteStatus(statusPath, L"promoting", expectedHash, L"backing up and installing");
		const bool destinationExisted = FileExists(destinationPath);
		if (destinationExisted && !CreateBackup(destinationPath, backupPath, logPath))
		{
			WriteStatus(statusPath, L"failed", expectedHash, L"could not create backup");
			DeleteFileW(stagedPath.c_str());
			return kExitFailed;
		}

		if (!PromoteReplacement(stagedPath, destinationPath, logPath))
		{
			AppendLogLine(logPath, L"Replacement failed after retries.");
			WriteStatus(statusPath, L"failed", expectedHash, L"replacement failed after retries");
			DeleteFileW(stagedPath.c_str());
			return kExitFailed;
		}

		std::wstring destinationHash;
		if (!ComputeSha256(destinationPath, destinationHash, hashError) || destinationHash != expectedHash)
		{
			AppendLogLine(logPath, L"Installed payload hash validation failed: " + hashError);
			const bool restored = UndoPromotion(destinationExisted, backupPath, destinationPath, logPath);
			WriteStatus(
				statusPath,
				L"failed",
				expectedHash,
				restored ? L"installed hash mismatch; previous version restored" : L"installed hash mismatch; rollback failed");
			return kExitFailed;
		}
		AppendLogLine(logPath, L"Installed OpenShim SHA-256 verified: " + destinationHash);
		WriteStatus(statusPath, L"complete", expectedHash, L"replacement verified");
		AppendLogLine(logPath, L"bzfile replace helper completed successfully.");
		return kExitInstalled;
	}

	// Where a failure can still be reported if something throws.
	std::filesystem::path StatusPathFromArguments(const std::vector<std::wstring>& arguments)
	{
		if (arguments.size() >= 5 && (arguments[1] == L"--suite" || arguments[1] == L"--suite-v3"))
		{
			return arguments[4];
		}
		if (arguments.size() == 8)
		{
			return arguments[7];
		}
		return {};
	}
}

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argv == nullptr)
	{
		return kExitBadArguments;
	}

	std::vector<std::wstring> arguments(argv, argv + argc);
	LocalFree(argv);

	// Barrier: an escaping exception would end the helper with no status, and
	// the game would keep reporting the update as pending.
	try
	{
		if (arguments.size() > 1 && (arguments[1] == L"--suite" || arguments[1] == L"--suite-v3"))
		{
			return RunSuiteUpdate(arguments);
		}
		return RunSingleUpdate(arguments);
	}
	catch (const std::exception& exception)
	{
		const std::string what = exception.what();
		WriteStatus(StatusPathFromArguments(arguments), L"failed", L"",
			L"helper error: " + std::wstring(what.begin(), what.end()));
	}
	catch (...)
	{
		WriteStatus(StatusPathFromArguments(arguments), L"failed", L"", L"helper error");
	}
	return kExitFailed;
}
