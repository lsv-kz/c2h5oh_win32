#include "main.h"

using namespace std;
static Connect *list_start = NULL;
static Connect *list_end = NULL;

static std::mutex mtx_list, mtx_thr;
static std::condition_variable cond_list, cond_thr;
static int num_thr = 0, thr_exit = 0, max_thr = 0, work_thr = 0, size_list = 0;
static unsigned long all_req = 0;
//======================================================================
void push_resp_list(Connect *req)
{
lock_guard<mutex> lk(mtx_list);
    req->next = NULL;
    req->prev = list_end;
    if (list_start)
    {
        list_end->next = req;
        list_end = req;
    }
    else
        list_start = list_end = req;
    ++all_req;
    ++size_list;
    cond_list.notify_one();
    if ((num_thr - work_thr) == size_list)
        cond_thr.notify_one();
}
//======================================================================
static Connect *pop_resp_list()
{
unique_lock<mutex> lk(mtx_list);
    while ((list_start == NULL) && !thr_exit)
    {
        cond_list.wait(lk);
    }
    if (thr_exit)
    {
        print_err("<%s:%d> *** Exit parse_request_thread, num=%d ***\n", __func__, __LINE__, num_thr);
        --num_thr;
        return NULL;
    }

    Connect *req = list_start;
    if (list_start->next)
    {
        list_start->next->prev = NULL;
        list_start = list_start->next;
    }
    else
        list_start = list_end = NULL;
    ++work_thr;
    --size_list;
    return req;
}
//======================================================================
void close_threads_manager()
{
lock_guard<mutex> lk(mtx_list);
    thr_exit = 1;
    cond_list.notify_all();
    cond_thr.notify_one();
}
//======================================================================
unsigned long get_all_request()
{
    return all_req;
}
//======================================================================
int get_max_thr()
{
    return max_thr;
}
//======================================================================
int is_maxthr()
{
unique_lock<mutex> lk(mtx_thr);
    while (((num_thr >= conf->MaxParseReqThreads) || ((num_thr - work_thr) > size_list)) && 
        !(num_thr < conf->MinParseReqThreads) && !thr_exit)
    {
        cond_thr.wait(lk);
    }
    
    if (thr_exit)
        return thr_exit;
    num_thr++;
    if (num_thr > max_thr)
        max_thr = num_thr;

    return thr_exit;
}
//======================================================================
int exit_thread(Connect *req)
{
    int ret = 0;
    if (req)
        end_response(req);
mtx_thr.lock();
    --work_thr;
    if ((num_thr > conf->MinParseReqThreads) && ((num_thr - work_thr) >= 1))
    {
        num_thr--;
        ret = 1;
    }
mtx_thr.unlock();
    return ret;
}
//======================================================================
void threads_manager()
{
    print_err("<%s:%d> --- run threads_manager ---max_thr=%d\n", __func__, __LINE__, max_thr);
    thread t;
    int i = 0;
    for (; i < conf->MinParseReqThreads; ++i)
    {
        try
        {
            t = thread(parse_request_thread);
        }
        catch(...)
        {
            print_err("<%s:%d> Error create thread: %s\n", __func__, __LINE__, strerror(errno));
            abort();
        }

        t.detach();
        ++num_thr;
    }
    printf("<%s:%d> ParseReqThreads=%d\n", __func__, __LINE__, num_thr);
    while (1)
    {
        if (is_maxthr())
            break;

        try
        {
            t = thread(parse_request_thread);
        }
        catch(...)
        {
            print_err("<%s:%d> Error create thread: %s; num_thr=%d\n", __func__, __LINE__, strerror(errno), num_thr);
            abort();
        }

        t.detach();
    }
    print_err("<%s:%d> ***** exit threads_manager [max_thr=%d]*****\n", __func__, __LINE__, max_thr);
}
//======================================================================
void parse_request_thread()
{
    while (1)
    {
        Connect* req = pop_resp_list();
        if (!req)
        {
            return;
        }
        //--------------------------------------------------------------
        get_time(req->resp.sTime);
        req->err = parse_startline_request(req, req->arrHdrs[0].ptr, req->arrHdrs[0].len);
        if (req->err)
        {
            print_err(req, "<%s:%d>  Error parse_startline_request(): %d\n", __func__, __LINE__, req->err);
            if (exit_thread(req))
                return;
            continue;
        }
        //--------------------------------------------------------------
        req->err = parse_headers(req);
        if (req->err < 0)
        {
            print_err(req, "<%s:%d>  Error parse_headers(): %d\n", __func__, __LINE__, req->err);
            if (exit_thread(req))
                return;
            continue;
        }
        /*--------------------------------------------------------*/
        if ((req->httpProt != HTTP10) && (req->httpProt != HTTP11))
        {
            req->connKeepAlive = 0;
            req->err = -RS505;
            if (exit_thread(req))
                return;
            continue;
        }

        if (req->numReq >= (unsigned int)conf->MaxRequestsPerClient || (req->httpProt == HTTP10))
        {
            print_err(req, "<%s:%d>  conf->MaxRequestsPerClient: %d\n", __func__, __LINE__, conf->MaxRequestsPerClient);
            req->connKeepAlive = 0;
        }
        else if (req->req_hdrs.iConnection == -1)
            req->connKeepAlive = 1;

        const char* p;
        if ((p = strchr(req->uri, '?')))
        {
            req->uriLen = p - req->uri;
            req->sReqParam = req->uri + req->uriLen + 1;
        }
        else
        {
            req->sReqParam = NULL;
            req->uriLen = strlen(req->uri);
        }

        int len = decode(req->uri, req->uriLen, req->decodeUri, sizeof(req->decodeUri));
        if (len <= 0)
        {
            print_err(req, "<%s:%d> Error: decode URI\n", __func__, __LINE__);
            req->err = -RS404;
            if (exit_thread(req))
                return;
            continue;
        }

        len = clean_path(req->decodeUri, len);
        if (len <= 0)
        {
			print_err(req, "<%s:%d> Error URI=%s\n", __func__, __LINE__, req->decodeUri);
            req->lenDecodeUri = 0;
            if (len == 0)
                req->err = -RS400;
            else
                req->err = len;
            if (exit_thread(req))
                return;
            continue;
		}
		req->lenDecodeUri = len;
        //--------------------------------------------------------------
        int n = utf8_to_utf16(req->decodeUri, req->wDecodeUri);
        if (n)
        {
            print_err(req, "<%s:%d> utf8_to_utf16()=%d\n", __func__, __LINE__, n);
            req->err = -RS500;
            if (exit_thread(req))
                return;
            continue;
        }
        //--------------------------------------------------------------
        if ((req->reqMethod != M_GET) &&
            (req->reqMethod != M_HEAD) &&
            (req->reqMethod != M_POST) &&
            (req->reqMethod != M_OPTIONS))
        {
            req->err = -RS501;
            if (exit_thread(req))
                return;
            continue;
        }

        int err = prepare_response(req);
        if (err == 1)
        {
            if (exit_thread(NULL))
                return;
            continue;
        }
        else if (err < 0)
            req->err = err;
        else
        {
            print_err(req, "<%s:%d> !!! prepare_response()=%d\n", __func__, __LINE__, req->err);
            req->err = -1;
        }

        if (exit_thread(req))
            return;
    }
}
//======================================================================
int send_file(Connect *req);
int send_multypart(Connect *req);
//======================================================================
long long file_size(const wchar_t* s)
{
    struct _stati64 st;

    if (!_wstati64(s, &st))
        return st.st_size;
    else
        return -1;
}
//======================================================================
int fastcgi(Connect* req, const wchar_t* wPath)
{
    const wchar_t* p = wcsrchr(wPath, '/');
    if (!p) return -RS404;
    fcgi_list_addr* i = conf->fcgi_list;
    for (; i; i = i->next)
    {
        if (i->script_name[0] == L'~')
        {
            if (!wcscmp(p, i->script_name.c_str() + 1))
                break;
        }
        else
        {
            if (i->script_name == wPath)
                break;
        }
    }

    if (!i)
        return -RS404;

    if (i->type == FASTCGI)
        req->scriptType = FASTCGI;
    else if (i->type == SCGI)
        req->scriptType = SCGI;
    else
        return -RS404;

    req->wScriptName = i->script_name;
    push_cgi(req);
    return 1;
}
//======================================================================
int prepare_response(Connect* req)
{
    if ((strstr(req->decodeUri, ".php")))
    {
        if ((conf->usePHP != "php-cgi") && (conf->usePHP != "php-fpm"))
        {
            print_err(req, "<%s:%d> Not found: %s\n", __func__, __LINE__, req->decodeUri);
            return -RS404;
        }
        struct _stat st;
        if (_wstat(req->wDecodeUri.c_str() + 1, &st) == -1)
        {
            print_err(req, "<%s:%d> script (%s) not found\n", __func__, __LINE__, req->decodeUri);
            return -RS404;
        }

        req->wScriptName = req->wDecodeUri;
        if (conf->usePHP == "php-cgi")
        {
            req->scriptType = PHPCGI;
            push_cgi(req);
            return 1;
        }
        else if (conf->usePHP == "php-fpm")
        {
            req->scriptType = PHPFPM;
            push_cgi(req);
            return 1;
        }
    }

    if (!strncmp(req->decodeUri, "/cgi-bin/", 9)
        || !strncmp(req->decodeUri, "/cgi/", 5))
    {
        req->wScriptName = req->wDecodeUri;
        req->scriptType = CGI;
        push_cgi(req);
        return 1;
    }
    //-------------------------- get path ------------------------------
    wstring wPath;
    wPath.reserve(conf->wRootDir.size() + req->wDecodeUri.size() + 256);
    wPath += conf->wRootDir;
    wPath += req->wDecodeUri;
    if (wPath[wPath.size() - 1] == L'/')
        wPath.resize(wPath.size() - 1);
    //------------------------------------------------------------------
    struct _stati64 st64;
    if (_wstati64(wPath.c_str(), &st64) == -1)
    {
        int ret = fastcgi(req, req->wDecodeUri.c_str());
        if (ret < 0)
        {
            String sTmp;
            utf16_to_utf8(req->wDecodeUri, sTmp);
            print_err(req, "<%s:%d> Error not found (%d) [%s]\n", __func__, __LINE__, ret, sTmp.c_str());
        }
        return ret;
    }
    else
    {
        if ((!(st64.st_mode & _S_IFDIR)) && (!(st64.st_mode & _S_IFREG)))
        {
            print_err(req, "<%s:%d> Error: file (!S_ISDIR && !S_ISREG) \n", __func__, __LINE__);
            return -RS404;
        }
    }
    //------------------------------------------------------------------
    DWORD attr = GetFileAttributesW(wPath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES)
    {
        DWORD err = GetLastError();
        PrintError(__func__, __LINE__, "GetFileAttributesW", err);
        if (err == ERROR_FILE_NOT_FOUND)
			return -RS404;
        return -RS500;
    }
    else if (attr & FILE_ATTRIBUTE_HIDDEN)
    {
        print_err(req, "<%s:%d> Hidden\n", __func__, __LINE__);
        return -RS404;
    }

    if (st64.st_mode & _S_IFDIR)
    {
        if (req->reqMethod == M_POST)
            return -RS404;
        else if (req->reqMethod == M_OPTIONS)
        {
            req->resp.respContentType = "text/html; charset=utf-8";
            return options(req);
        }

        if (req->decodeUri[req->lenDecodeUri - 1] != '/')
        {
            req->uri[req->uriLen] = '/';
            req->uri[req->uriLen + 1] = '\0';
            req->resp.respStatus = RS301;

            req->hdrs.reserve(127);
            req->hdrs << "Location: " << req->uri << "\r\n";
            if (req->hdrs.error())
            {
                print_err(req, "<%s:%d> Error create_header()\n", __func__, __LINE__);
                return -RS500;
            }

            String s(256);
            s << "The document has moved <a href=\"" << req->uri << "\">here</a>";
            if (s.error())
            {
                print_err(req, "<%s:%d> Error create_header()\n", __func__, __LINE__);
                return -1;
            }

            return send_message(req, s.c_str());
        }
        //--------------------------------------------------------------
        size_t len = wPath.size();
        wPath += L"/index.html";
        struct _stat st;
        if ((_wstat(wPath.c_str(), &st) == -1) || (conf->index_html != 'y'))
        {
            wPath.resize(len);
            if ((conf->usePHP != "n") && (conf->index_php == 'y'))
            {
                wPath += L"/index.php";
                if (_wstat(wPath.c_str(), &st) == 0)
                {
                    int ret;
                    req->wScriptName = req->wDecodeUri + L"index.php";
                    if (conf->usePHP == "php-fpm")
                    {
                        req->scriptType = PHPFPM;
                        push_cgi(req);
                        return 1;
                    }
                    else if (conf->usePHP == "php-cgi")
                    {
                        req->scriptType = PHPCGI;
                        push_cgi(req);
                        return 1;
                    }
                    else
                        ret = -1;

                    req->wScriptName = L"";
                    return ret;
                }
                wPath.resize(len);
            }

            if (conf->index_pl == 'y')
            {
                req->scriptType = CGI;
                req->wScriptName = L"/cgi-bin/index.pl";
                push_cgi(req);
                return 1;
            }
            else if (conf->index_fcgi == 'y')
            {
                req->scriptType = FASTCGI;
                req->wScriptName = L"/index.fcgi";
                push_cgi(req);
                return 1;
            }

            return index_dir(req, wPath);
        }
    }

    if (req->reqMethod == M_POST)
        return -RS405;
    //-------------------------------------------------------------------
    req->resp.fileSize = file_size(wPath.c_str());
    req->resp.numPart = 0;
    req->resp.respContentType = content_type(wPath.c_str());
    if (req->reqMethod == M_OPTIONS)
        return options(req);
    //------------------------------------------------------------------
    req->resp.fHnd = CreateFileW(wPath.c_str(), 
                                GENERIC_READ, 
                                FILE_SHARE_READ, 
                                NULL, 
                                OPEN_EXISTING, 
                                FILE_ATTRIBUTE_NORMAL, 
                                NULL);
    if (req->resp.fHnd == INVALID_HANDLE_VALUE)
    {
        int err = GetLastError();
        PrintError(__func__, __LINE__, "Error CreateFileW", err);
        String sTmp;
        utf16_to_utf8(wPath, sTmp);
        print_err(req, "<%s:%d> Error CreateFileW(%s)\n", __func__, __LINE__, sTmp.c_str());
        if (err == ERROR_ACCESS_DENIED)
            return -RS403;
        else
            return -RS500;
    }

    wPath.clear();
    wPath.reserve(0);

    int ret = send_file(req);
    if (ret != 1)
        CloseHandle(req->resp.fHnd);
    return ret;
}
//======================================================================
int send_file(Connect *req)
{
    if (req->req_hdrs.iRange >= 0)
    {
        int err;

        req->rg.init(req->sRange, req->resp.fileSize);
        if ((err = req->rg.error()))
        {
            print_err(req, "<%s:%d> Error init Ranges\n", __func__, __LINE__);
            return err;
        }

        req->resp.numPart = req->rg.size();
        req->resp.respStatus = RS206;
        if (req->resp.numPart > 1)
        {
            int n = send_multypart(req);
            return n;
        }
        else if (req->resp.numPart == 1)
        {
            Range *pr = req->rg.get();
            if (pr)
            {
                req->resp.offset = pr->start;
                req->resp.respContentLength = pr->len;
            }
            else
                return -RS500;
        }
        else
        {
            print_err(req, "<%s:%d> ???\n", __func__, __LINE__);
            return -RS416;
        }
    }
    else
    {
        req->resp.respStatus = RS200;
        req->resp.offset = 0;
        req->resp.respContentLength = req->resp.fileSize;
    }

    if (create_response_headers(req))
        return -1;

    push_send_file(req);

    return 1;
}
//======================================================================
int create_multipart_head(Connect *req);
//======================================================================
int send_multypart(Connect *req)
{
    long long send_all_bytes = 0;
    char buf[1024];

    for ( ; (req->mp.rg = req->rg.get()); )
    {
        send_all_bytes += (req->mp.rg->len);
        send_all_bytes += create_multipart_head(req);
    }
    send_all_bytes += snprintf(buf, sizeof(buf), "\r\n--%s--\r\n", boundary);
    req->resp.respContentLength = send_all_bytes;
    req->resp.send_bytes = 0;

    req->hdrs.reserve(256);
    req->hdrs << "Content-Type: multipart/byteranges; boundary=" << boundary << "\r\n";
    req->hdrs << "Content-Length: " << send_all_bytes << "\r\n";
    if (req->hdrs.error())
    {
        print_err(req, "<%s:%d> Error create response headers\n", __func__, __LINE__);
        return -1;
    }

    req->rg.set_index();

    if (create_response_headers(req))
        return -1;

    push_send_multipart(req);

    return 1;
}
//======================================================================
int create_multipart_head(Connect *req)
{
    req->mp.hdr = "";
    req->mp.hdr << "\r\n--" << boundary << "\r\n";

    if (req->resp.respContentType)
        req->mp.hdr << "Content-Type: " << req->resp.respContentType << "\r\n";
    else
        return 0;

    req->mp.hdr << "Content-Range: bytes " << req->mp.rg->start << "-" << req->mp.rg->end << "/" << req->resp.fileSize << "\r\n\r\n";

    return req->mp.hdr.size();
}
//======================================================================
int options(Connect* r)
{
    r->resp.respStatus = RS200;
    r->resp.respContentLength = 0;
    if (create_response_headers(r))
        return -1;

    r->resp_headers.p = r->resp_headers.s.c_str();
    r->resp_headers.len = r->resp_headers.s.size();
    r->html.len = 0;
    push_send_html(r);
    return 1;
}
