// adbwatch.cpp : Defines the entry point for the console application.
//

#include <WinSDKVer.h>
#include <SDKDDKVer.h>
#include <stdio.h>
#include <tchar.h>
#include <Windows.h>
#include <Wtsapi32.h>
#include <atlstr.h>
#include <atlcoll.h>
#include <Shlwapi.h>

int startup_delay = 60;
bool must_exit = false;
bool IsAdbHung();
void KillAdb();
void SetAdbAffinity();
CAtlArray<CString>  launch_processes; 

BOOL CtrlHandler(DWORD fdwCtrlType) {
  printf("Exiting...\n");
  must_exit = true;
  return TRUE;
}

void LoadSettings() {
  wchar_t launch_section[32767];
  wchar_t ini_file[MAX_PATH];
  GetModuleFileName(NULL, ini_file, MAX_PATH);
  lstrcpy(PathFindExtension(ini_file), L".ini");
  if (GetPrivateProfileSection(L"Launch", launch_section,
      sizeof(launch_section) / sizeof(launch_section[0]), ini_file)) {
    wchar_t * line = launch_section;
    while (line && lstrlen(line)) {
      CString trimmed(line);
      trimmed.Trim();
      if (trimmed.GetLength())
        launch_processes.Add(line);
      line += lstrlen(line) + 1;
    }
  }
  startup_delay = GetPrivateProfileInt(L"General", L"Startup Delay",
                                       startup_delay, ini_file);
}

void LaunchAndWait(LPWSTR command_line, LPWSTR label) {
  PROCESS_INFORMATION pi;
  STARTUPINFO si;
  memset(&si, 0, sizeof(si));
  memset(&pi, 0, sizeof(pi));
  si.cb = sizeof(si);
  if (label && lstrlen(label))
    si.lpTitle = label;

  if (CreateProcess(NULL, command_line, NULL, NULL, FALSE, CREATE_NEW_CONSOLE,
                    NULL, NULL, &si, &pi)) {
    if (pi.hThread)
      CloseHandle(pi.hThread);
    if (pi.hProcess) {
      WaitForSingleObject(pi.hProcess, INFINITE);
      CloseHandle(pi.hProcess);
    }
  }
}

DWORD WINAPI LaunchProcess(LPVOID param) {
  int index = (int)param;
  wchar_t label[100];
  memset(label, 0, sizeof(label));
  CString cmd;
  if (index >= 0 && index < (int)launch_processes.GetCount()) {
    cmd = launch_processes.GetAt(index);
    int separator = cmd.Find(L"=");
    if (separator > -1) {
      lstrcpy(label, (LPCWSTR)cmd.Left(separator).Trim());
      cmd = cmd.Mid(separator + 1).Trim();
    }
  }
  if (cmd.GetLength()) {
    wchar_t command_line[10000];
    lstrcpy(command_line, (LPCWSTR)cmd);
    while (!must_exit) {
      LaunchAndWait(command_line, label);
      if (!must_exit) {
        SYSTEMTIME t;
        GetLocalTime(&t);
        wprintf(L"%d/%d/%d %d:%02d:%02d - %s exited, restarting...\n",
            t.wMonth, t.wDay, t.wYear, t.wHour, t.wMinute, t.wSecond, label);
      }
    }
  }
  return 0;
}

int _tmain(int argc, _TCHAR* argv[]) {
  SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE);
  LoadSettings();
  if (startup_delay) {
    printf("Waiting for statup delay...\n");
    for (int seconds = 0; seconds < startup_delay && !must_exit; seconds++)
      Sleep(1000);
  }
  int thread_count = launch_processes.GetCount();
  if (thread_count) {
    printf("Spawning %d processes...\n", thread_count);
    for (int i = 0; i < thread_count; i++) {
      HANDLE thread = CreateThread(NULL, 0, LaunchProcess, (LPVOID)i, 0, NULL);
      CloseHandle(thread);
    }
  }
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