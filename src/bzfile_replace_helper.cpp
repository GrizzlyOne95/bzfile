#include <Windows.h>
#include <shellapi.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <sstream>
#include <vector>
#include <iomanip>

namespace
{
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
				MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
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

	if (arguments.size() != 5)
	{
		return 2;
	}

	DWORD processId = static_cast<DWORD>(wcstoul(arguments[1].c_str(), nullptr, 10));
	const std::filesystem::path stagedPath(arguments[2]);
	const std::filesystem::path destinationPath(arguments[3]);
	const std::filesystem::path logPath(arguments[4]);

	AppendLogLine(logPath, L"bzfile replace helper started.");
	AppendLogLine(logPath, L"Staged: " + stagedPath.wstring());
	AppendLogLine(logPath, L"Destination: " + destinationPath.wstring());

	if (!std::filesystem::exists(stagedPath))
	{
		AppendLogLine(logPath, L"Staged file is missing before replacement.");
		return 1;
	}

	WaitForProcessExit(processId, logPath);

	if (!PromoteReplacement(stagedPath, destinationPath, logPath))
	{
		AppendLogLine(logPath, L"Replacement failed after retries.");
		return 1;
	}

	AppendLogLine(logPath, L"bzfile replace helper completed successfully.");
	return 0;
}
