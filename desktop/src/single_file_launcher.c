#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <wchar.h>
#define SKPASS_VERSION L"v1.0"

static void show_error(const wchar_t *message) {
    MessageBoxW(NULL,message,L"PN7150 SKPass " SKPASS_VERSION,MB_OK|MB_ICONERROR);
}
static BOOL run_program(const wchar_t *exe,const wchar_t *args,const wchar_t *cwd,BOOL wait,DWORD *exit_code) {
    STARTUPINFOW si={.cb=sizeof(si)};PROCESS_INFORMATION pi={0};
    wchar_t cmd[32768];
    if(swprintf(cmd,32768,L"\"%ls\" %ls",exe,args)<0)return FALSE;
    if(!CreateProcessW(exe,cmd,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,cwd,&si,&pi))return FALSE;
    CloseHandle(pi.hThread);
    if(wait){WaitForSingleObject(pi.hProcess,INFINITE);if(exit_code)GetExitCodeProcess(pi.hProcess,exit_code);}
    CloseHandle(pi.hProcess);return TRUE;
}
static BOOL expand_payload(const wchar_t *root,const wchar_t *app,const wchar_t *zip_path) {
    wchar_t ps[32768],args[32768],quoted_zip[16384],quoted_app[16384];
    DWORD n=GetWindowsDirectoryW(ps,32768);if(!n||n>=32768)return FALSE;
    if(swprintf(ps+n,32768-n,L"\\System32\\WindowsPowerShell\\v1.0\\powershell.exe")<0)return FALSE;
    size_t zi=0,ai=0;
    for(size_t i=0;zip_path[i] && zi+2<16384;i++){quoted_zip[zi++]=zip_path[i];if(zip_path[i]==L'\'')quoted_zip[zi++]=L'\'';}quoted_zip[zi]=0;
    for(size_t i=0;app[i] && ai+2<16384;i++){quoted_app[ai++]=app[i];if(app[i]==L'\'')quoted_app[ai++]=L'\'';}quoted_app[ai]=0;
    if(swprintf(args,32768,L"-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"$ErrorActionPreference='Stop';try { Expand-Archive -LiteralPath '%ls' -DestinationPath '%ls' -Force; exit 0 } catch { exit 1 }\"",quoted_zip,quoted_app)<0)return FALSE;
    DWORD code=1;if(!run_program(ps,args,root,TRUE,&code)||code!=0)return FALSE;
    DeleteFileW(zip_path);return TRUE;
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,PWSTR command_line,int show) {
    (void)instance;(void)previous;(void)show;
    wchar_t self[32768],root[32768],app[32768],zip_path[32768],exe[32768],args[32768];
    DWORD n=GetModuleFileNameW(NULL,self,32768);if(!n||n>=32768){show_error(L"无法取得启动器路径。");return 1;}
    wchar_t *slash=wcsrchr(self,L'\\');if(!slash){show_error(L"无法解析启动器目录。");return 1;}*slash=0;
    if(swprintf(root,32768,L"%ls\\SKPass-" SKPASS_VERSION L"-runtime",self)<0||swprintf(zip_path,32768,L"%ls\\runtime-" SKPASS_VERSION L".zip",root)<0){show_error(L"运行目录路径过长。");return 1;}
    if(!CreateDirectoryW(root,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS){show_error(L"无法在启动器目录创建运行文件夹，请检查写入权限。");return 1;}
    HRSRC resource=FindResourceW(NULL,MAKEINTRESOURCEW(101),RT_RCDATA);
    HGLOBAL loaded=resource?LoadResource(NULL,resource):NULL;
    DWORD size=resource?SizeofResource(NULL,resource):0;
    const unsigned char *data=loaded?LockResource(loaded):NULL;
    if(!data||!size){show_error(L"单文件运行包数据损坏。");return 2;}
    DWORD hash=2166136261u;for(DWORD i=0;i<size;i++)hash=(hash^data[i])*16777619u;
    /* Each payload gets its own directory. A running previous revision locks
     * its EXE/DLLs, so replacing files in place can silently keep old code. */
    if(swprintf(app,32768,L"%ls\\payload-%08lX",root,(unsigned long)hash)<0||swprintf(exe,32768,L"%ls\\SKPass-" SKPASS_VERSION L"-runtime.exe",app)<0){show_error(L"运行目录路径过长。");return 1;}
    if(!CreateDirectoryW(app,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS){show_error(L"无法创建运行时文件夹。");return 1;}
    wchar_t marker[32768];if(swprintf(marker,32768,L"%ls\\payload.id",app)<0)return 2;
    DWORD previousHash=0,read=0;
    HANDLE markerFile=CreateFileW(marker,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(markerFile!=INVALID_HANDLE_VALUE){ReadFile(markerFile,&previousHash,sizeof(previousHash),&read,NULL);CloseHandle(markerFile);}
    if(GetFileAttributesW(exe)==INVALID_FILE_ATTRIBUTES || read!=sizeof(previousHash) || previousHash!=hash) {
        HANDLE file=CreateFileW(zip_path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_TEMPORARY,NULL);
        DWORD written=0;BOOL saved=file!=INVALID_HANDLE_VALUE&&WriteFile(file,data,size,&written,NULL)&&written==size;
        if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
        if(!saved||!expand_payload(root,app,zip_path)){DeleteFileW(zip_path);show_error(L"首次启动解压失败。请检查PowerShell和本地磁盘空间。");return 3;}
        markerFile=CreateFileW(marker,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
        if(markerFile==INVALID_HANDLE_VALUE){show_error(L"无法保存运行时版本标记。");return 3;}
        DWORD markerWritten=0;BOOL marked=WriteFile(markerFile,&hash,sizeof(hash),&markerWritten,NULL);CloseHandle(markerFile);
        if(!marked || markerWritten!=sizeof(hash))return 3;
    }
    (void)command_line;
    int argc=0;
    LPWSTR *argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    BOOL smoke=FALSE;
    if(argv){for(int i=1;i<argc;i++)if(wcscmp(argv[i],L"--smoke-test")==0)smoke=TRUE;LocalFree(argv);}
    DWORD exit_code=0;
    if(swprintf(args,32768,L"%ls --data-dir \"%ls\"",smoke?L"--smoke-test":L"",self)<0){show_error(L"启动参数过长。");return 4;}
    if(!run_program(exe,args,app,smoke,&exit_code)){show_error(L"无法启动SKPass。请重新运行对应版本的单文件程序。");return 4;}
    if(smoke)return (int)exit_code;
    return 0;
}
