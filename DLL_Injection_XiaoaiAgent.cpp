#include <windows.h>
#include <iostream>
#include <string>

std::string GetExecutablePathFromRegistry(const std::string& subKey, const std::string& valueName) {
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, subKey.c_str(), 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
        return "";
    }

    char buffer[MAX_PATH];
    DWORD bufferSize = sizeof(buffer);
    if (RegQueryValueEx(hKey, valueName.c_str(), NULL, NULL, (LPBYTE)buffer, &bufferSize) != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return "";
    }

    RegCloseKey(hKey);
    return std::string(buffer);
}

std::string GetExecutableDirectory() {
    char buffer[MAX_PATH];
    GetModuleFileName(NULL, buffer, MAX_PATH);
    std::string::size_type pos = std::string(buffer).find_last_of("\\/");
    return std::string(buffer).substr(0, pos);
}

bool FileExists(const std::string& filename) {
    DWORD attrib = GetFileAttributes(filename.c_str());
    return (attrib != INVALID_FILE_ATTRIBUTES && !(attrib & FILE_ATTRIBUTE_DIRECTORY));
}

int main() {
    // 修改为XiaomiAICreatorClient.exe的注册表路径
    std::string regSubKey = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\XiaomiAICreatorClient.exe";
    std::string regValueName = "Path";

    // 获取XiaomiAICreatorClient.exe的安装路径
    std::string installPath = GetExecutablePathFromRegistry(regSubKey, regValueName);
    if (installPath.empty()) {
        std::cerr << "Failed to get XiaomiAICreatorClient installation path from registry." << std::endl;
        return 1;
    }

    // 构建XiaomiAICreatorClient.exe的完整路径
    std::string exePath = installPath + "\\XiaomiAICreatorClient.exe";

    // 获取可执行文件所在的目录
    std::string dllDir = GetExecutableDirectory();
    // 构建DLL路径
    std::string dllPath = dllDir + "\\wtsapi32.dll";

    // 检查DLL文件是否存在
    if (!FileExists(dllPath)) {
        std::cerr << "DLL file not found: " << dllPath << std::endl;
        return 1;
    }

    // 进程信息结构体
    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    // 初始化STARTUPINFO结构体
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    // 启动目标进程
    if (!CreateProcess(
        NULL,
        (LPSTR)exePath.c_str(),
        NULL,
        NULL,
        FALSE,
        CREATE_SUSPENDED,
        NULL,
        NULL,
        &si,
        &pi
    )) {
        std::cerr << "CreateProcess failed (" << GetLastError() << ")." << std::endl;
        return 1;
    }

    // 分配内存给目标进程中的DLL路径
    LPVOID pRemoteMem = VirtualAllocEx(
        pi.hProcess,
        NULL,
        dllPath.length() + 1,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );

    if (!pRemoteMem) {
        std::cerr << "VirtualAllocEx failed (" << GetLastError() << ")." << std::endl;
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 1;
    }

    // 将DLL路径写入目标进程内存
    if (!WriteProcessMemory(
        pi.hProcess,
        pRemoteMem,
        (LPVOID)dllPath.c_str(),
        dllPath.length() + 1,
        NULL
    )) {
        std::cerr << "WriteProcessMemory failed (" << GetLastError() << ")." << std::endl;
        VirtualFreeEx(pi.hProcess, pRemoteMem, 0, MEM_RELEASE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 1;
    }

    // 获取LoadLibraryA函数地址
    LPTHREAD_START_ROUTINE pfnStartAddr = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandle("kernel32"), "LoadLibraryA");

    if (!pfnStartAddr) {
        std::cerr << "GetProcAddress failed (" << GetLastError() << ")." << std::endl;
        VirtualFreeEx(pi.hProcess, pRemoteMem, 0, MEM_RELEASE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 1;
    }

    // 创建远程线程调用LoadLibraryA加载DLL
    HANDLE hThread = CreateRemoteThread(
        pi.hProcess,
        NULL,
        0,
        pfnStartAddr,
        pRemoteMem,
        0,
        NULL
    );

    if (!hThread) {
        std::cerr << "CreateRemoteThread failed (" << GetLastError() << ")." << std::endl;
        VirtualFreeEx(pi.hProcess, pRemoteMem, 0, MEM_RELEASE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 1;
    }

    // 等待远程线程结束
    WaitForSingleObject(hThread, INFINITE);

    // 清理资源
    CloseHandle(hThread);
    VirtualFreeEx(pi.hProcess, pRemoteMem, 0, MEM_RELEASE);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    std::cout << "DLL injected successfully into XiaomiAICreatorClient.exe." << std::endl;

    return 0;
}