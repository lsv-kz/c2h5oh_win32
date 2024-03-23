#include "main.h"

using namespace std;
//======================================================================
static mutex mtx_conn;
static condition_variable cond_close_conn;

static int num_conn = 0;
static int *conn_count;
//======================================================================
int get_light_thread_number()
{
    int n_thr = 0, n_conn;
mtx_conn.lock();
    if (conf->BalancedLoad == 'y')
    {
        n_conn = conn_count[0];
        for (int i = 1; i < conf->NumWorkThreads; ++i)
        {
            if (n_conn > conn_count[i])
            {
                n_thr = i;
                n_conn = conn_count[i];
            }
        }

        if (conn_count[n_thr] >= conf->MaxConnectionPerThr)
        {
            print_err("<%s:%d> !!!!!\n", __func__, __LINE__);
            n_thr = -1;
        }
    }
    else
    {
        n_thr = -1;
        for (int i = 0; i < conf->NumWorkThreads; ++i)
        {
            if (conn_count[i] < conf->MaxConnectionPerThr)
            {
                n_thr = i;
                break;
            }
        }
    }

mtx_conn.unlock();
    return n_thr;
}
//======================================================================
void end_response(Connect* req)
{
    if ((req->connKeepAlive == 0) || req->err < 0)
    {
        if (req->err <= -RS101) // err < -100
        {
            req->resp.respStatus = -req->err;
            req->err = -1;
            req->connKeepAlive = 0;
            if (send_message(req, NULL) == 1)
                return;
        }

        if (req->operation != READ_REQUEST)
        {
            print_log(req);
        }
        //----------------- close connect ------------------------------
        shutdown(req->clientSocket, SD_BOTH);
        closesocket(req->clientSocket);

        int n = req->numThr;
        delete req;

    mtx_conn.lock();
        conn_count[n]--;
        --num_conn;
    mtx_conn.unlock();
        cond_close_conn.notify_all();
    }
    else
    {
        print_log(req);
        req->init();
        req->timeout = conf->TimeoutKeepAlive;
        ++req->numReq;
        push_pollin_list(req);
    }
}
//======================================================================
void start_conn(int n)
{
mtx_conn.lock();
    conn_count[n]++;
    ++num_conn;
mtx_conn.unlock();
}
//======================================================================
void is_maxconn()
{
unique_lock<mutex> lk(mtx_conn);
    while (num_conn >= conf->MaxAcceptConnections)
    {
        cond_close_conn.wait(lk);
    }
}
//======================================================================
static unsigned long allConn = 0;
static SOCKET sock;
Connect* create_req();
int event_handler_cl_new();
void event_handler_cl_delete();
void close_threads_manager();
void threads_manager();
int get_max_thr();
//======================================================================
void manager(SOCKET sockServer)
{
    num_conn = 0;
    setbuf(stderr, NULL);
    sock = sockServer;
    //------------------------------------------------------------------
    if (_wchdir(conf->wRootDir.c_str()))
    {
        print_err("<%s:%d> Error root_dir: %d\n", __func__, __LINE__, errno);
        exit(1);
    }
    //------------------------------------------------------------------
    if (event_handler_cl_new())
    {
        exit(1);
    }

    conn_count = new(nothrow) int [conf->NumWorkThreads];
    if (!conn_count)
    {
        print_err("<%s:%d> Error create array conn_count: %s\n", __func__, __LINE__, strerror(errno));
        event_handler_cl_delete();
        exit(1);
    }

    memset(conn_count, 0, sizeof(int) * conf->NumWorkThreads);
    //------------------------------------------------------------------
    thread CgiHandler;
    try
    {
        CgiHandler = thread(cgi_handler);
    }
    catch (...)
    {
        print_err("<%s:%d> Error create thread(cgi_handler)\n", __func__, __LINE__);
        exit(1);
    }
    //------------------------------------------------------------------
    thread *work_thr = new(nothrow) thread [conf->NumWorkThreads];
    if (!work_thr)
    {
        print_err("<%s:%d> Error create array thread: %s\n", __func__, __LINE__, strerror(errno));
        exit(errno);
    }

    for (int i = 0; i < conf->NumWorkThreads; ++i)
    {
        try
        {
            work_thr[i] = thread(event_handler, i);
        }
        catch (...)
        {
            print_err("<%s:%d> Error create thread(event_handler)\n", __func__, __LINE__);
            exit(1);
        }
    }
    //------------------------------------------------------------------
    thread thr;
    try
    {
        thr = thread(threads_manager);
    }
    catch (...)
    {
        print_err("<%s:%d> Error create thread: errno=%d\n", __func__, __LINE__, errno);
        exit(errno);
    }
    //------------------------------------------------------------------
    print_err(" +++++ pid=%u +++++\n", getpid());
    //------------------------------------------------------------------
    int run = 1;

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sockServer, &readfds);

    while (run)
    {
        SOCKET clientSocket;
        struct sockaddr_storage clientAddr;
        socklen_t addrSize = sizeof(struct sockaddr_storage);

        is_maxconn();

        FD_SET(sockServer, &readfds);
        int ret_sel = select(sockServer + 1, &readfds, NULL, NULL, NULL);
        if (ret_sel <= 0)
        {
            PrintError(__func__, __LINE__, "Error select()", WSAGetLastError());
            break;
        }

        if (!FD_ISSET(sockServer, &readfds))
            break;
        clientSocket = accept(sockServer, (struct sockaddr*)&clientAddr, &addrSize);
        if (clientSocket == INVALID_SOCKET)
        {
            int err = WSAGetLastError();
            PrintError(__func__, __LINE__, "Error accept()", err);
            if (err == WSAEMFILE)
                continue;
            else
                break;
        }

        Connect* req;
        req = create_req();
        if (!req)
        {
            shutdown(clientSocket, SD_BOTH);
            closesocket(clientSocket);
            continue;
        }

        if (!SetHandleInformation((HANDLE)clientSocket, HANDLE_FLAG_INHERIT, 0))
        {
            print_err("<%s:%d> Error: SetHandleInformation(): %d\n", __func__, __LINE__, WSAGetLastError());
        }

        u_long iMode = 1;
        if (ioctlsocket(clientSocket, FIONBIO, &iMode) == SOCKET_ERROR)
        {
            print_err("<%s:%d> Error ioctlsocket(): %d\n", __func__, __LINE__, WSAGetLastError());
        }

        req->init();
        req->numThr = get_light_thread_number();
        if (req->numThr < 0)
        {
            print_err("<%s:%d> Error get_light_thread_number()\n", __func__, __LINE__);
            shutdown(clientSocket, SD_BOTH);
            closesocket(clientSocket);
            delete req;
            continue;
        }
        req->numConn = ++allConn;
        req->numReq = 1;
        req->serverSocket = sockServer;
        req->clientSocket = clientSocket;
        req->timeout = conf->TimeOut;
        req->remoteAddr[0] = '\0';
        req->remotePort[0] = '\0';
        getnameinfo((struct sockaddr*)&clientAddr,
            addrSize,
            req->remoteAddr,
            sizeof(req->remoteAddr),
            req->remotePort,
            sizeof(req->remotePort),
            NI_NUMERICHOST | NI_NUMERICSERV);

        start_conn(req->numThr);
        push_pollin_list(req);
    }

    print_err("<%s:%d> allConn=%u\n", __func__, __LINE__, allConn);
    close_work_threads();
    for (int i = 0; i < conf->NumWorkThreads; ++i)
    {
        work_thr[i].join();
    }

    delete [] work_thr;

    close_cgi_handler();
    event_handler_cl_delete();
    delete [] conn_count;

    CgiHandler.join();
    
    close_threads_manager();
    thr.join();

    print_err("<%s:%d> *** Exit  ***\n", __func__, __LINE__);
}
//======================================================================
Connect* create_req()
{
    Connect* req = new(nothrow) Connect;
    if (!req)
    {
        print_err("<%s:%d> Error malloc()\n", __func__, __LINE__);
    }
    return req;
}
