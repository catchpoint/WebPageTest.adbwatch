// adbwatch.cpp : Defines the entry point for the console application.
//

#include <WinSDKVer.h>
#include <SDKDDKVer.h>
#include <stdio.h>
#include <tchar.h>
#include <Windows.h>
#include <Wtsapi32.h>
#include <atlstr.h>

bool must_exit = false;
bool IsAdbHung();
void KillAdb();
void SetAdbAffinity();

BOOL CtrlHandler(DWORD fdwCtrlType) {
  printf("Exiting...\n");
  must_exit = true;
  return TRUE;
}

int _tmain(int argc, _TCHAR* argv[]) {
  SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE);
  printf("Monitoring adb for hangs...\n");
  SetAdbAffinity();
  while (!must_exit) {
    for (int seconds = 0; seconds < 60 && !must_exit; seconds++)
      Sleep(1000);
    if (IsAdbHung()) {
      KillAdb();
      IsAdbHung();
      SetAdbAffinity();
    }
  }
	return 0;
}

bool IsAdbHung() {
  bool is_hung = false;

  SECURITY_ATTRIBUTES sa; 

  // Set the bInheritHandle flag so pipe handles are inherited. 
  sa.nLength = sizeof(SECURITY_ATTRIBUTES); 
  sa.bInheritHandle = TRUE; 
  sa.lpSecurityDescriptor = NULL; 

  HANDLE stdout_read = NULL;
  HANDLE stdout_write = NULL;
  HANDLE stderr_read = NULL;
  HANDLE stderr_write = NULL;

  CStringA std_out, std_err;

  // Create a pipe for the child process's STDERR. 
  if (CreatePipe(&stderr_read, &stderr_write, &sa, 0) &&
      CreatePipe(&stdout_read, &stdout_write, &sa, 0)) {
    SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);

    // launch the adb command and wait for it to exit
    PROCESS_INFORMATION pi; 
    STARTUPINFO si;
    ZeroMemory(&pi, sizeof(pi));
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si); 
    si.hStdError = stderr_write;
    si.hStdOutput = stdout_write;
    si.dwFlags = STARTF_USESTDHANDLES;
    TCHAR command[MAX_PATH];
    lstrcpy(command, _T("adb devices"));

    if (CreateProcess(NULL, command, NULL, NULL, TRUE,
                      CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
      if (pi.hProcess)
        WaitForSingleObject(pi.hProcess, 60000);

      // read all of the stderr and stdout data
      char buff[4097];
      DWORD bytes = 0;
      while (PeekNamedPipe(stdout_read, buff, sizeof(buff), &bytes, NULL, NULL) &&
             bytes &&
             ReadFile(stdout_read, buff, sizeof(buff) - 1, &bytes, NULL) &&
             bytes) {
        buff[bytes] = 0;
        std_out += buff;
      }
      while (PeekNamedPipe(stderr_read, buff, sizeof(buff), &bytes, NULL, NULL) &&
             bytes &&
             ReadFile(stderr_read, buff, sizeof(buff) - 1, &bytes, NULL) &&
             bytes) {
        buff[bytes] = 0;
        std_err += buff;
      }

      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
    }
  }

  // Close the pipe handles
  if (stdout_read)
    CloseHandle(stdout_read);
  if (stdout_write)
    CloseHandle(stdout_write);
  if (stderr_read)
    CloseHandle(stderr_read);
  if (stderr_write)
    CloseHandle(stderr_write);

  if (std_err.GetLength())
    is_hung = true;

  return is_hung;
}

void KillAdb() {
  SYSTEMTIME t;
  GetLocalTime(&t);
  printf("%d/%d/%d %d:%02d:%02d - hang detected\n",
         t.wMonth, t.wDay, t.wYear, t.wHour, t.wMinute, t.wSecond);
  WTS_PROCESS_INFO * proc = NULL;
  DWORD count = 0;
  if (WTSEnumerateProcesses(WTS_CURRENT_SERVER_HANDLE, 0, 1, &proc ,&count)) {
    for (DWORD i = 0; i < count; i++) {
      TCHAR * process = PathFindFileName(proc[i].pProcessName);
      if (!lstrcmpi(process, _T("adb.exe"))) {
        HANDLE process_handle = OpenProcess(PROCESS_TERMINATE, FALSE, 
                                              proc[i].ProcessId);
        if (process_handle) {
          TerminateProcess(process_handle, 0);
          CloseHandle(process_handle);
        }
      }
    }
    if (proc)
      WTSFreeMemory(proc);
  }
}

void SetAdbAffinity() {
  WTS_PROCESS_INFO * proc = NULL;
  DWORD count = 0;
  if (WTSEnumerateProcesses(WTS_CURRENT_SERVER_HANDLE, 0, 1, &proc ,&count)) {
    for (DWORD i = 0; i < count; i++) {
      TCHAR * process = PathFindFileName(proc[i].pProcessName);
      if (!lstrcmpi(process, _T("adb.exe"))) {
        HANDLE process_handle = OpenProcess(PROCESS_SET_INFORMATION, FALSE, 
                                              proc[i].ProcessId);
        if (process_handle) {
          SetProcessAffinityMask(process_handle, 1);
          CloseHandle(process_handle);
        }
      }
    }
    if (proc)
      WTSFreeMemory(proc);
  }
}