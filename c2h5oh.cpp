#include "main.h"
#include <sstream>

using namespace std;
//======================================================================
static SOCKET sockServer = INVALID_SOCKET;
//======================================================================
int read_conf_file(const char* path_conf);
int main_proc(const char* name_proc);
void manager(SOCKET sock);
int get_mem_avail();
//======================================================================
BOOL WINAPI sigHandler(DWORD signal)
{
    if (signal == CTRL_C_EVENT)
    {
        printf("<%s> signal: Ctrl-C\n", __func__);
        if (sockServer != INVALID_SOCKET)
        {
            shutdown(sockServer, SD_BOTH);
            closesocket(sockServer);
            sockServer = INVALID_SOCKET;
        }
        else
        {
            exit(1);
        }
    }

    return TRUE;
}
//======================================================================
int main(int argc, char* argv[])
{
    if (read_conf_file(".") < 0)
    {
        cin.get();
        exit(1);
    }
    //------------------------------------------------------------------
    //setlocale(LC_CTYPE, "");
    printf(" LC_CTYPE: %s\n", setlocale(LC_CTYPE, ""));

    if (!SetConsoleCtrlHandler(sigHandler, TRUE))
    {
        printf("\nError: Could not set control handler\n");
        return 1;
    }
    main_proc(argv[0]);
    return 0;
}
//======================================================================
void create_logfiles(const wchar_t* log_dir, HANDLE* h, HANDLE* hErr);
//======================================================================
int main_proc(const char* name_proc)
{
    DWORD pid = GetCurrentProcessId();
    HANDLE hLog, hLogErr;
    create_logfiles(conf->wLogDir.c_str(), &hLog, &hLogErr);
    //------------------------------------------------------------------
    sockServer = create_server_socket(conf);
    if (sockServer == INVALID_SOCKET)
    {
        cout << "<" << __LINE__ << "> server: failed to bind" << "\n";
        cin.get();
        exit(1);
    }

    cerr << " [" << get_time().c_str() << "] - server \"" << conf->ServerSoftware.c_str() << "\" run\n"
        << "\n   hardware_concurrency = " << thread::hardware_concurrency()
        << "\n   pid = " << pid
        << "\n   memory free = " << get_mem_avail()
        << "\n   ip = " << conf->ServerAddr.c_str()
        << "\n   port = " << conf->ServerPort.c_str()
        << "\n   SndBufSize = " << conf->SndBufSize
        << "\n\n   MaxParseReqThreads = " << conf->MaxParseReqThreads
         << "\n   MinParseReqThreads = " << conf->MinParseReqThreads
        << "\n\n   ListenBacklog = " << conf->ListenBacklog
        << "\n   MaxAcceptConnections = " << conf->MaxAcceptConnections
        << "\n\n   MaxRequestsPerClient " << conf->MaxRequestsPerClient
        << "\n   TimeoutKeepAlive = " << conf->TimeoutKeepAlive
        << "\n   TimeOut = " << conf->TimeOut
        << "\n   TimeoutSel = " << conf->TimeoutSel
        << "\n   TimeoutCGI = " << conf->TimeoutCGI
        << "\n\n   php: " << conf->usePHP.c_str()
        << "\n\n   path_php-fpm: " << conf->pathPHP_FPM.c_str();
    wcerr << L"\n   path_php-cgi: " << conf->wPathPHP_CGI
        << L"\n   pyPath: " << conf->wPyPath
        << L"\n   PerlPath: " << conf->wPerlPath
        << L"\n   root_dir = " << conf->wRootDir
        << L"\n   cgi_dir = " << conf->wCgiDir
        << L"\n   log_dir = " << conf->wLogDir
        << L"\n   ShowMediaFiles = " << conf->ShowMediaFiles
        << L"\n   ClientMaxBodySize = " << conf->ClientMaxBodySize
        << L"\n\n";
    //------------------------------------------------------------------
    manager(sockServer);
    if (sockServer != INVALID_SOCKET)
    {
        shutdown(sockServer, SD_BOTH);
        closesocket(sockServer);
    }
    WSACleanup();
    print_err("<%s:%d> ***** Close main_proc *****\n", __func__, __LINE__);

    return 0;
}
