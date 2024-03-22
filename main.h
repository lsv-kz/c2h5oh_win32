#ifndef SERVER_H_
#define SERVER_H_
#define _WIN32_WINNT  0x0501

#include <iostream>
#include <fstream>
#include <algorithm>
#include <vector>

#include <mutex>
#include <thread>
#include <condition_variable>

#include <stdio.h>
#include <cstdlib>
#include <string.h>
#include <cassert>
#include <climits>
#include <wchar.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>

#include <io.h>
#include <sys/types.h>
#include <share.h>

#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <time.h>

#include <Winsock2.h>
#include <winsock.h>
#include <ws2tcpip.h>
#include <direct.h>
#include <process.h>

#include "String.h"
#include "range.h"

const int MAX_NAME = 256;
const int SIZE_BUF_REQUEST = 8192;
const int MAX_HEADERS = 25;
const int BUF_TMP_SIZE = 10000;
const int TRYAGAIN = -1000;

const int  MAX_WORK_THREADS = 8;
const int  LIMIT_WORK_THREADS = 16;
const int  LIMIT_PARSE_REQ_THREADS = 128;

const char boundary[] = "----------a9b5r7a4c0a2d5a1b8r3a";

enum {
    RS101 = 101,
    RS200 = 200, RS204 = 204, RS206 = 206,
    RS301 = 301, RS302,
    RS400 = 400, RS401, RS402, RS403, RS404, RS405, RS406, RS407,
    RS408, RS411 = 411, RS413 = 413, RS414, RS415, RS416, RS417, RS418,
    RS500 = 500, RS501, RS502, RS503, RS504, RS505
};

enum {
    M_GET = 1, M_HEAD, M_POST, M_OPTIONS, M_PUT,
    M_PATCH, M_DELETE, M_TRACE, M_CONNECT
};

enum { HTTP09 = 1, HTTP10, HTTP11, HTTP2 };

enum { EXIT_THR = 1 };

enum MODE_SEND { NO_CHUNK, CHUNK, CHUNK_END };
enum SOURCE_ENTITY { ENTITY_NONE, FROM_FILE, FROM_DATA_BUFFER, MULTIPART_ENTITY, };
enum OPERATION_TYPE { READ_REQUEST = 1, SEND_RESP_HEADERS, SEND_ENTITY, DYN_PAGE, };
enum MULTIPART_STATUS { SEND_HEADERS = 1, SEND_PART, SEND_END };
enum IO_STATUS { SELECT = 1, WAIT_PIPE, WORK };

enum CGI_TYPE { CGI_TYPE_NONE, CGI, PHPCGI, PHPFPM, FASTCGI, SCGI, };
enum DIRECT { FROM_CGI = 1, TO_CGI, FROM_CLIENT, TO_CLIENT };

enum CGI_STATUS  { CGI_CREATE_PROC = 1, CGI_STDIN, CGI_READ_HTTP_HEADERS, CGI_SEND_HTTP_HEADERS, CGI_SEND_ENTITY };

enum FCGI_STATUS { FASTCGI_CONNECT = 1, FASTCGI_BEGIN, FASTCGI_PARAMS, FASTCGI_STDIN,
               FASTCGI_READ_HEADER, FASTCGI_READ_HTTP_HEADERS, FASTCGI_SEND_HTTP_HEADERS, FASTCGI_SEND_ENTITY,
               FASTCGI_READ_ERROR, FASTCGI_READ_PADDING, FASTCGI_CLOSE };

enum SCGI_STATUS { SCGI_CONNECT = 1, SCGI_PARAMS, SCGI_STDIN, SCGI_READ_HTTP_HEADERS, SCGI_SEND_HTTP_HEADERS, SCGI_SEND_ENTITY, };

typedef struct fcgi_list_addr {
    std::wstring script_name;
    std::wstring addr;
    int type;
    struct fcgi_list_addr* next = NULL;
} fcgi_list_addr;

struct Param
{
    String name;
    String val;
};

void print_err(const char* format, ...);
//----------------------------------------------------------------------
typedef struct
{
    OVERLAPPED oOverlap;
    HANDLE parentPipe;
    DWORD dwState;
    BOOL fPendingIO;
} PIPENAMED;

struct Config
{
    std::string ServerSoftware = "?";
    std::string ServerAddr = "0.0.0.0";
    std::string ServerPort = "8080";
    
    std::wstring wLogDir = L"";
    std::wstring wRootDir = L"";
    std::wstring wCgiDir = L"";
    std::wstring wPerlPath = L"";
    std::wstring wPyPath = L"";
    std::string usePHP = "n";
    std::wstring wPathPHP_CGI = L"";
    std::string pathPHP_FPM = "";

    int ListenBacklog = 128;
    int SndBufSize = 16284;
    char BalancedLoad;

    int MaxAcceptConnections;
    int MaxConnectionPerThr;

    int NumWorkThreads;
    int MaxParseReqThreads;
    int MinParseReqThreads;

    int MaxCgiProc = 10;

    unsigned int MaxRanges = 10;
    long int ClientMaxBodySize = 1000000;

    int MaxRequestsPerClient = 50;
    int TimeoutKeepAlive = 5;
    int TimeOut = 30;
    int TimeoutCGI = 5;
    int TimeoutSel = 10;

    char index_html = 'n';
    char index_php = 'n';
    char index_pl = 'n';
    char index_fcgi = 'n';

    char ShowMediaFiles = 'n';

    fcgi_list_addr* fcgi_list = NULL;
    ~Config()
    {
        fcgi_list_addr* t;
        while (fcgi_list)
        {
            t = fcgi_list;
            fcgi_list = fcgi_list->next;
            if (t)
                delete t;
        }
    }
};

extern const Config* const conf;
//---------------------------------------------------------
struct hdr {
    char* ptr;
    int len;
};

union STATUS { CGI_STATUS cgi; FCGI_STATUS fcgi; SCGI_STATUS scgi; };
struct Cgi
{
    PIPENAMED Pipe;
    HANDLE hChld;

    char *bufEnv;
    size_t sizeBufEnv;
    size_t lenEnv;

    STATUS status;
    char buf[8 + 4096 + 8];
    int  size_buf = 4096;
    long len_buf;
    long len_post;
    char *p;

    Cgi();
    ~Cgi();
    int init(size_t);
    size_t param(const char* name, const char* val);
};

class Connect
{
public:
    Connect* prev;
    Connect* next;

    SOCKET serverSocket;

    int numThr;
    unsigned long numReq, numConn;

    SOCKET    clientSocket;
    int       err;
    __time64_t sock_timer;
    int timeout;

    OPERATION_TYPE operation;
    IO_STATUS io_status;
    DIRECT io_direct;

    char remoteAddr[NI_MAXHOST];
    char remotePort[NI_MAXSERV];

    struct
    {
        char buf[SIZE_BUF_REQUEST];
        int  len;
    } req;

    char* p_newline;
    char* tail;
    int   lenTail;

    int  i_arrHdrs;
    hdr  arrHdrs[MAX_HEADERS + 1];

    char  decodeUri[SIZE_BUF_REQUEST];

    std::wstring  wDecodeUri;

    char* uri;
    size_t uriLen;

    int  reqMethod;

    std::wstring wScriptName;
    CGI_TYPE scriptType;

    const char* sReqParam;
    char* sRange;
    int   httpProt;
    int   connKeepAlive;

    struct
    {
        int  iConnection;
        int  iHost;
        int  iUserAgent;
        int  iReferer;
        int  iUpgrade;
        int  iReqContentType;
        int  iReqContentLength;
        int  iAcceptEncoding;
        int  iRange;
        int  iIf_Range;

        int  countReqHeaders;
        long long reqContentLength;

        const char* Name[MAX_HEADERS + 1];
        const char* Value[MAX_HEADERS + 1];
    } req_hdrs;
    /*--------------------------*/
    struct
    {
        String s;
        const char* p;
        int len;
    } resp_headers;

    String hdrs;

    struct
    {
        String s;
        const char* p;
        int len;
    } html;

    Cgi cgi;

    struct
    {
        SOCKET fd;
        bool http_headers_received;

        int i_param;
        int size_par;
        std::vector <Param> vPar;

        unsigned char fcgi_type;
        int dataLen;
        int paddingLen;
        char buf[8];
        int len_header;
    } fcgi;

    Ranges rg;
    struct
    {
        MULTIPART_STATUS status;
        Range *rg;
        String hdr;
    } mp;

    SOURCE_ENTITY source_entity;
    MODE_SEND mode_send;

    struct {
        int  respStatus;
        std::string sTime;
        long long respContentLength;
        const char *respContentType;
        long long fileSize;
        int  countRespHeaders = 0;
        int  numPart;
        HANDLE fHnd;
        long long offset;
        long long send_bytes;
    } resp;
    //----------------------------------------
    void init()
    {
        err = 0;
        decodeUri[0] = '\0';
        sRange = NULL;
        sReqParam = NULL;

        //------------------------------------
        uri = NULL;
        p_newline = req.buf;
        tail = NULL;
        //------------------------------------
        lenTail = 0;
        req.len = 0;
        i_arrHdrs = 0;
        reqMethod = 0;
        httpProt = 0;
        connKeepAlive = 0;

        req_hdrs = { -1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 0, -1LL };
        req_hdrs.Name[0] = NULL;
        req_hdrs.Value[0] = NULL;

        resp.respStatus = 0;
        resp.fHnd = INVALID_HANDLE_VALUE;
        resp.fileSize = 0;
        resp.offset = 0;
        resp.respContentLength = -1LL;
        resp.numPart = 0;
        resp.send_bytes = 0LL;
        resp.respContentType = NULL;
        resp.countRespHeaders = 0;
        resp.sTime = "";
        hdrs = "";

        scriptType = CGI_TYPE_NONE;
        cgi.Pipe.parentPipe = INVALID_HANDLE_VALUE;
        cgi.hChld = INVALID_HANDLE_VALUE;
        fcgi.fd = -1;

        mode_send = NO_CHUNK;
    }

    int read_request_headers();
    int find_empty_line();
};
//----------------------------------------------------------------------
class EventHandlerClass
{
    std::mutex mtx_thr;
    std::condition_variable cond_thr;

    int num_thr;
    int n_work, n_select;
    int close_thr;
    unsigned long num_request;

    Connect *work_list_start;
    Connect *work_list_end;

    Connect *wait_list_start;
    Connect *wait_list_end;

    int send_entity(Connect *r);
    void del_from_list(Connect *r);
    void add_wait_list(Connect *r);
    
    void worker(Connect *r);
    int send_html(Connect *r);
    void set_part(Connect *r);
    int send_headers(Connect *r);
    void choose_worker(Connect *r);

public:

    fd_set wrfds, rdfds;
    char *snd_buf;
    int size_buf;

    void init(int n);
    int set_list();
    int select_(int n_thr);
    int wait_conn();
    void push_send_file(Connect *r);
    void push_pollin_list(Connect *r);
    void push_send_multipart(Connect *r);
    void push_send_html(Connect *r);
    void close_event_handler();
    long get_num_req();
};
//----------------------------------------------------------------------
class LinkedList
{
private:
    Connect* list_start;
    Connect* list_end;

    std::mutex mtx_list;
    std::condition_variable cond_list;

    unsigned long all_req;
    int thr_exit;

public:
    LinkedList(const LinkedList&) = delete;
    LinkedList();
    ~LinkedList();
    //-------------------------------
    void push_resp_list(Connect* r);
    Connect* pop_resp_list();
    void close_threads();
    unsigned int get_all_request()
    {
        return all_req;
    }
};
//======================================================================
int in4_aton(const char* host, struct in_addr* addr);
SOCKET create_server_socket(const Config* conf);
int send_file(SOCKET sock, HANDLE hnd, char* buf, int size);
SOCKET create_fcgi_socket(Connect *r, const char* host);
//----------------------------------------------------------------------
void parse_request_thread();
int prepare_response(Connect* r);
int options(Connect* r);
int index_dir(Connect* r, std::wstring& path);
//----------------------------------------------------------------------
int PrintError(const char* f, int line, const char* s, int err);
std::string get_time();
void get_time(std::string& s);
std::string log_time();
const char* strstr_case(const char* s1, const char* s2);
int strlcmp_case(const char* s1, const char* s2, int len);
int strcmp_case(const char* s1, const char* s2);

int get_int_method(char* s);
const char* get_str_method(int i);

int get_int_http_prot(char* s);
const char* get_str_http_prot(int i);

const char* strstr_lowercase(const char* s, const char* key);
int clean_path(char* path);

const char* content_type(const wchar_t* path);
int parse_startline_request(Connect* r, char* s, int len);
int parse_headers(Connect* r);
void path_correct(std::wstring& path);

const char *get_str_operation(OPERATION_TYPE n);
const char *get_cgi_status(CGI_STATUS n);
const char *get_fcgi_status(FCGI_STATUS n);
const char *get_scgi_status(SCGI_STATUS n);
const char *get_cgi_type(CGI_TYPE n);
const char *get_cgi_dir(DIRECT n);

int get_num_handles(FILE *f);
//----------------------------------------------------------------------
int utf16_to_utf8(const std::wstring& ws, std::string& s);
int utf16_to_utf8(const std::wstring& ws, String& s);
int utf8_to_utf16(const char* u8, std::wstring& ws);
int utf8_to_utf16(const std::string& u8, std::wstring& ws);
int utf8_to_utf16(const String& u8, std::wstring& ws);
int decode(const char* s_in, size_t len_in, char* s_out, int len);
std::string encode(const std::string& s_in);
//----------------------------------------------------------------------
int create_response_headers(Connect* r);
int send_message(Connect* r, const char* msg);
//----------------------------------------------------------------------
void print_err(Connect* r, const char* format, ...);
void print_log(Connect* r);
HANDLE GetHandleLogErr();
//----------------------------------------------------------------------
void push_resp_list(Connect *r);
void end_response(Connect* r);
//----------------------------------------------------------------------
void event_handler(int);
void push_pollin_list(Connect* r);
void push_send_file(Connect* r);
void push_send_multipart(Connect *r);
void push_send_html(Connect* r);
void close_work_threads();
void conn_decrement(int n);
int event_handler_cl_new();
void event_handler_cl_delete();
//----------------------------------------------------------------------
void cgi_handler();
void push_cgi(Connect *r);
void close_cgi_handler(void);

#endif
