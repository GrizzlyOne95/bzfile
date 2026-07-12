#include <Windows.h>
#include <shellapi.h>
#include <wincrypt.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <iomanip>

namespace
{
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
		while (input.good())
		{
			input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
			const auto bytesRead = input.gcount();
			if (bytesRead <= 0)
			{
				break;
			}

			if (!CryptHashData(hash.handle, reinterpret_cast<const BYTE*>(buffer.data()), static_cast<DWORD>(bytesRead), 0))
			{
				errorMessage = L"CryptHashData failed: " + FormatWindowsError(GetLastError());
				return false;
			}
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

	bool RestoreBackup(
		const std::filesystem::path& backupPath,
		const std::filesystem::path& destinationPath,
		const std::filesystem::path& logPath)
	{
		if (backupPath.empty() || !std::filesystem::exists(backupPath))
		{
			AppendLogLine(logPath, L"No backup is available for rollback.");
			return false;
		}

		SetFileAttributesW(backupPath.c_str(), FILE_ATTRIBUTE_NORMAL);
		SetFileAttributesW(destinationPath.c_str(), FILE_ATTRIBUTE_NORMAL);
		if (!CopyFileW(backupPath.c_str(), destinationPath.c_str(), FALSE))
		{
			AppendLogLine(logPath, L"Rollback copy failed: " + FormatWindowsError(GetLastError()));
			return false;
		}

		AppendLogLine(logPath, L"Restored previous OpenShim backup.");
		return true;
	}

	bool WaitForProcessExit(DWORD processId, const std::filesystem::path& logPath)
	{
		if (processId == 0)
		{
			AppendLogLine(logPath, L"Skipping wait because process id was 0.");
			return true;
		}

		HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, processId);
		if (process == nullptr)
		{
			DWORD openError = GetLastError();
			if (openError == ERROR_INVALID_PARAMETER)
			{
				AppendLogLine(logPath, L"Process already exited before helper wait began.");
				Sleep(1000); // Give some extra time for handles to release
				return true;
			}

			AppendLogLine(logPath, L"OpenProcess failed; continuing anyway: " + FormatWindowsError(openError));
			return false;
		}

		AppendLogLine(logPath, L"Waiting for process " + std::to_wstring(processId) + L" to exit.");
		DWORD waitResult = WaitForSingleObject(process, INFINITE);
		CloseHandle(process);

		if (waitResult == WAIT_OBJECT_0)
		{
			AppendLogLine(logPath, L"Observed target process exit.");
			Sleep(1000); // Give some extra time for handles to release
			return true;
		}

		AppendLogLine(logPath, L"WaitForSingleObject failed: " + FormatWindowsError(GetLastError()));
		return false;
	}

	bool PromoteReplacement(
		const std::filesystem::path& stagedPath,
		const std::filesystem::path& destinationPath,
		const std::filesystem::path& logPath)
	{
		constexpr int kMaxAttempts = 120;
		constexpr DWORD kDelayMilliseconds = 250;

		for (int attempt = 1; attempt <= kMaxAttempts; ++attempt)
		{
			SetFileAttributesW(stagedPath.c_str(), FILE_ATTRIBUTE_NORMAL);
			SetFileAttributesW(destinationPath.c_str(), FILE_ATTRIBUTE_NORMAL);

			if (MoveFileExW(
				stagedPath.c_str(),
				destinationPath.c_str(),
				MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH))
			{
				if (std::filesystem::exists(stagedPath))
				{
					AppendLogLine(logPath, L"MoveFileExW returned success but staged file still exists.");
					Sleep(kDelayMilliseconds);
					continue;
				}

				AppendLogLine(logPath, L"Replacement succeeded on attempt " + std::to_wstring(attempt) + L".");
				return true;
			}

			DWORD moveError = GetLastError();
			if (attempt <= 5 || attempt == kMaxAttempts || attempt % 10 == 0)
			{
				std::wstringstream message;
				message
					<< L"Attempt " << attempt
					<< L" failed: " << FormatWindowsError(moveError);
				AppendLogLine(logPath, message.str());
			}

			Sleep(kDelayMilliseconds);
		}

		return false;
	}
}

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argv == nullptr)
	{
		return 2;
	}

	std::vector<std::wstring> arguments(argv, argv + argc);
	LocalFree(argv);

	const bool hardenedMode = arguments.size() == 8;
	if (arguments.size() != 5 && !hardenedMode)
	{
		return 2;
	}

	DWORD processId = static_cast<DWORD>(wcstoul(arguments[1].c_str(), nullptr, 10));
	const std::filesystem::path stagedPath(arguments[2]);
	const std::filesystem::path destinationPath(arguments[3]);
	const std::filesystem::path logPath(arguments[4]);
	const std::wstring expectedHash = hardenedMode ? arguments[5] : L"";
	const std::filesystem::path backupPath = hardenedMode ? std::filesystem::path(arguments[6]) : std::filesystem::path();
	const std::filesystem::path statusPath = hardenedMode ? std::filesystem::path(arguments[7]) : std::filesystem::path();

	ScopedHandle updateMutex;
	if (hardenedMode)
	{
		updateMutex.handle = CreateMutexW(nullptr, FALSE, L"Local\\BZR_OpenShim_Update");
		if (updateMutex.handle == nullptr)
		{
			WriteStatus(statusPath, L"failed", expectedHash, L"could not create update mutex");
			return 1;
		}
		if (GetLastError() == ERROR_ALREADY_EXISTS)
		{
			AppendLogLine(logPath, L"Another OpenShim update helper is already active.");
			WriteStatus(statusPath, L"already_staged", expectedHash, L"another helper is active");
			return 0;
		}
	}

	AppendLogLine(logPath, L"bzfile replace helper started.");
	AppendLogLine(logPath, L"Staged: " + stagedPath.wstring());
	AppendLogLine(logPath, L"Destination: " + destinationPath.wstring());

	if (!std::filesystem::exists(stagedPath))
	{
		AppendLogLine(logPath, L"Staged file is missing before replacement.");
		WriteStatus(statusPath, L"failed", expectedHash, L"staged file is missing");
		return 1;
	}

	if (hardenedMode)
	{
		std::wstring stagedHash;
		std::wstring hashError;
		if (!ComputeSha256(stagedPath, stagedHash, hashError) || stagedHash != expectedHash)
		{
			AppendLogLine(logPath, L"Staged payload hash validation failed: " + hashError);
			WriteStatus(statusPath, L"failed", expectedHash, L"staged payload hash mismatch");
			return 1;
		}
		AppendLogLine(logPath, L"Staged payload SHA-256 verified: " + stagedHash);
		WriteStatus(statusPath, L"waiting_for_exit", expectedHash, L"payload verified");
	}

	if (!WaitForProcessExit(processId, logPath))
	{
		WriteStatus(statusPath, L"failed", expectedHash, L"could not wait for game process exit");
		return 1;
	}

	if (hardenedMode && std::filesystem::exists(destinationPath))
	{
		SetFileAttributesW(destinationPath.c_str(), FILE_ATTRIBUTE_NORMAL);
		SetFileAttributesW(backupPath.c_str(), FILE_ATTRIBUTE_NORMAL);
		if (!CopyFileW(destinationPath.c_str(), backupPath.c_str(), FALSE))
		{
			const std::wstring backupError = FormatWindowsError(GetLastError());
			AppendLogLine(logPath, L"Could not create OpenShim backup: " + backupError);
			WriteStatus(statusPath, L"failed", expectedHash, L"could not create backup: " + backupError);
			return 1;
		}
		AppendLogLine(logPath, L"Backed up current OpenShim to: " + backupPath.wstring());
	}

	if (!PromoteReplacement(stagedPath, destinationPath, logPath))
	{
		AppendLogLine(logPath, L"Replacement failed after retries.");
		WriteStatus(statusPath, L"failed", expectedHash, L"replacement failed after retries");
		return 1;
	}

	if (hardenedMode)
	{
		std::wstring destinationHash;
		std::wstring hashError;
		if (!ComputeSha256(destinationPath, destinationHash, hashError) || destinationHash != expectedHash)
		{
			AppendLogLine(logPath, L"Installed payload hash validation failed: " + hashError);
			const bool restored = RestoreBackup(backupPath, destinationPath, logPath);
			WriteStatus(
				statusPath,
				L"failed",
				expectedHash,
				restored ? L"installed hash mismatch; previous version restored" : L"installed hash mismatch; rollback failed");
			return 1;
		}
		AppendLogLine(logPath, L"Installed OpenShim SHA-256 verified: " + destinationHash);
		WriteStatus(statusPath, L"complete", expectedHash, L"replacement verified");
	}

	AppendLogLine(logPath, L"bzfile replace helper completed successfully.");
	return 0;
}
