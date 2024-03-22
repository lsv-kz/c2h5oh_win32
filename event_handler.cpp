#include "main.h"

using namespace std;
//======================================================================
int create_multipart_head(Connect *req);
int get_mem_avail();
//======================================================================
static EventHandlerClass *event_handler_cl;
//======================================================================
void EventHandlerClass::init(int n)
{
    num_thr = n;
    num_request = 0;
    close_thr = n_work = n_select = 0;
    work_list_start = work_list_end = wait_list_start = wait_list_end = NULL;
    size_buf = conf->SndBufSize;
    snd_buf = NULL;
}
//----------------------------------------------------------------------
long EventHandlerClass::get_num_req()
{
    return num_request;
}
//----------------------------------------------------------------------
int EventHandlerClass::send_entity(Connect* req)
{
    int ret;
    int len;

    if (req->resp.respContentLength >= (long long)size_buf)
        len = size_buf;
    else
    {
        len = (int)req->resp.respContentLength;
        if (len == 0)
            return 0;
    }

    ret = send_file(req->clientSocket, req->resp.fHnd, snd_buf, len);
    if (ret < 0)
    {
        if (ret == -1)
            print_err(req, "<%s:%d> Error: Sent %lld bytes\n", __func__, __LINE__, req->resp.send_bytes);
        return ret;
    }

    req->resp.send_bytes += ret;
    req->resp.respContentLength -= ret;
    if (req->resp.respContentLength == 0)
        ret = 0;

    return ret;
}
//----------------------------------------------------------------------
void EventHandlerClass::del_from_list(Connect *r)
{
    if ((r->source_entity == FROM_FILE) || (r->source_entity == MULTIPART_ENTITY))
        CloseHandle(r->resp.fHnd);

    if (r->prev && r->next)
    {
        r->prev->next = r->next;
        r->next->prev = r->prev;
    }
    else if (r->prev && !r->next)
    {
        r->prev->next = r->next;
        work_list_end = r->prev;
    }
    else if (!r->prev && r->next)
    {
        r->next->prev = r->prev;
        work_list_start = r->next;
    }
    else if (!r->prev && !r->next)
        work_list_start = work_list_end = NULL;
}
//----------------------------------------------------------------------
int EventHandlerClass::set_list()
{
mtx_thr.lock();
    if (wait_list_start)
    {
        if (work_list_end)
            work_list_end->next = wait_list_start;
        else
            work_list_start = wait_list_start;
    
        wait_list_start->prev = work_list_end;
        work_list_end = wait_list_end;
        wait_list_start = wait_list_end = NULL;
    }
mtx_thr.unlock();

    FD_ZERO(&rdfds);
    FD_ZERO(&wrfds);
    __time64_t t = _time64(NULL);
    n_work = n_select = 0;
    Connect* r = work_list_start, *next;

    for (; r; r = next)
    {
        next = r->next;

        if (r->sock_timer == 0)
            r->sock_timer = t;

        if ((int)(t - r->sock_timer) >= r->timeout)
        {
            print_err(r, "<%s:%d> Timeout = %ld\n", __func__, __LINE__, (long)(t - r->sock_timer));
            if (r->operation != READ_REQUEST)
            {
                r->req_hdrs.iReferer = MAX_HEADERS - 1;
                r->req_hdrs.Value[r->req_hdrs.iReferer] = "Timeout";
            }
            r->err = -1;
            del_from_list(r);
            end_response(r);
        }
        else
        {
            if (r->io_status == WORK)
            {
                ++n_work;
            }
            else
            {
                if (r->io_direct == FROM_CLIENT)
                {
                    //r->timeout = conf->TimeOut;
                    FD_SET(r->clientSocket, &rdfds);
                    ++n_select;
                }
                else if (r->io_direct == TO_CLIENT)
                {
                    r->timeout = conf->TimeOut;
                    FD_SET(r->clientSocket, &wrfds);
                    ++n_select;
                }
                else
                {
                    print_err(r, "<%s:%d> Error: io_direct=%d, operation=%d\n", __func__, __LINE__,
                                r->io_direct, r->operation);
                    r->err = -1;
                    del_from_list(r);
                    end_response(r);
                }
            }
        }
    }

    return 1;
}
//----------------------------------------------------------------------
int EventHandlerClass::select_(int n_thr)
{
    int ret = 0;
    if (n_select > 0)
    {
        int time_sel = conf->TimeoutSel*1000;
        if (n_work > 0)
            time_sel = 0;
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = time_sel;

        ret = select(0, &rdfds, &wrfds, NULL, &tv);
        if (ret == SOCKET_ERROR)
        {
            PrintError(__func__, __LINE__, "Error select()", WSAGetLastError());
            return -1;
        }
        else if (ret == 0)
        {
            if (n_work == 0)
                return 0;
        }
    }
    else
    {
        if (n_work == 0)
            return 0;
    }

    int all = ret + n_work;
    Connect* r = work_list_start, *next = NULL;
    for ( ; (all > 0) && r; r = next)
    {
        next = r->next;

        if (r->io_status == WORK)
        {
            --all;
            worker(r);
            continue;
        }

        if (FD_ISSET(r->clientSocket, &rdfds))
        {
            --all;
            r->io_status = WORK;
            worker(r);
        }
        else if (FD_ISSET(r->clientSocket, &wrfds))
        {
            --all;
            //r->io_status = WORK;
            worker(r);
        }
    }

    return 1;
}
//----------------------------------------------------------------------
int EventHandlerClass::wait_conn()
{
unique_lock<mutex> lk(mtx_thr);
    while ((!work_list_start) && (!wait_list_start) && (!close_thr))
    {
        cond_thr.wait(lk);
    }

    if ((close_thr) && (!work_list_start) && (!wait_list_start))
        return 1;
    return 0;
}
//----------------------------------------------------------------------
void EventHandlerClass::add_wait_list(Connect *r)
{
    r->io_status = SELECT;
    r->sock_timer = 0;
    r->next = NULL;
mtx_thr.lock();
    r->prev = wait_list_end;
    if (wait_list_start)
    {
        wait_list_end->next = r;
        wait_list_end = r;
    }
    else
        wait_list_start = wait_list_end = r;
mtx_thr.unlock();
    cond_thr.notify_one();
}
//----------------------------------------------------------------------
void EventHandlerClass::push_send_file(Connect* r)
{
    r->io_direct = TO_CLIENT;
    r->source_entity = FROM_FILE;
    r->operation = SEND_RESP_HEADERS;
    r->resp_headers.p = r->resp_headers.s.c_str();
    r->resp_headers.len = r->resp_headers.s.size();

    LARGE_INTEGER offset;
    offset.QuadPart = r->resp.offset;
    if (!SetFilePointerEx(r->resp.fHnd, offset, NULL, FILE_BEGIN))
    {
        PrintError(__func__, __LINE__, "Error SetFilePointerEx()", GetLastError());
        r->err = -1;
        CloseHandle(r->resp.fHnd);
        end_response(r);
    }
    else
        add_wait_list(r);
}
//----------------------------------------------------------------------
void EventHandlerClass::push_pollin_list(Connect* r)
{
    r->io_direct = FROM_CLIENT;
    r->operation = READ_REQUEST;
    add_wait_list(r);
}
//----------------------------------------------------------------------
void EventHandlerClass::push_send_multipart(Connect *r)
{
    r->io_direct = TO_CLIENT;
    r->resp_headers.p = r->resp_headers.s.c_str();
    r->resp_headers.len = r->resp_headers.s.size();

    r->source_entity = MULTIPART_ENTITY;
    r->operation = SEND_RESP_HEADERS;
    add_wait_list(r);
}
//----------------------------------------------------------------------
void EventHandlerClass::push_send_html(Connect* r)
{
    r->io_direct = TO_CLIENT;
    r->source_entity = FROM_DATA_BUFFER;
    r->operation = SEND_RESP_HEADERS;
    add_wait_list(r);
}
//----------------------------------------------------------------------
void EventHandlerClass::close_event_handler(void)
{
    close_thr = 1;
    cond_thr.notify_one();
}
//----------------------------------------------------------------------
int EventHandlerClass::send_html(Connect* r)
{
    int ret = send(r->clientSocket, r->html.p, r->html.len, 0);
    if (ret == SOCKET_ERROR)
    {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK)
            return TRYAGAIN;
        return -1;
    }

    r->html.p += ret;
    r->html.len -= ret;
    r->resp.send_bytes += ret;
    if (r->html.len == 0)
        ret = 0;

    return ret;
}
//----------------------------------------------------------------------
void EventHandlerClass::set_part(Connect *r)
{
    r->mp.status = SEND_HEADERS;

    r->resp_headers.len = create_multipart_head(r);
    r->resp_headers.p = r->mp.hdr.c_str();

    r->resp.offset = r->mp.rg->start;
    r->resp.respContentLength = r->mp.rg->len;
    LARGE_INTEGER offset;
    offset.QuadPart = r->resp.offset;
    if (!SetFilePointerEx(r->resp.fHnd, offset, NULL, FILE_BEGIN))
    {
        PrintError(__func__, __LINE__, "Error SetFilePointerEx()", GetLastError());
        r->err = -1;
        del_from_list(r);
        end_response(r);
    }
}
//----------------------------------------------------------------------
void EventHandlerClass::worker(Connect* r)
{
    if (r->operation == SEND_ENTITY)
    {
        if (r->source_entity == FROM_FILE)
        {
            int wr = send_entity(r);
            if (wr == 0)
            {
                del_from_list(r);
                end_response(r);
            }
            else if (wr < 0)
            {
                if (wr == TRYAGAIN)
                    r->io_status = SELECT;
                else
                {
                    r->err = wr;
                    r->req_hdrs.iReferer = MAX_HEADERS - 1;
                    r->req_hdrs.Value[r->req_hdrs.iReferer] = "Connection reset by peer";
                    del_from_list(r);
                    end_response(r);
                }
            }
            else
                r->sock_timer = 0;
        }
        else if (r->source_entity == MULTIPART_ENTITY)
        {
            if (r->mp.status == SEND_HEADERS)
            {
                int wr = send(r->clientSocket, r->resp_headers.p, r->resp_headers.len, 0);
                if (wr == SOCKET_ERROR)
                {
                    int err = WSAGetLastError();
                    if (err == WSAEWOULDBLOCK)
                        r->io_status = SELECT;
                    else
                    {
                        r->err = -1;
                        r->req_hdrs.iReferer = MAX_HEADERS - 1;
                        r->req_hdrs.Value[r->req_hdrs.iReferer] = "Connection reset by peer";
                        del_from_list(r);
                        end_response(r);
                    }
                }
                else if (wr > 0)
                {
                    r->resp_headers.p += wr;
                    r->resp_headers.len -= wr;
                    r->resp.send_bytes += wr;
                    if (r->resp_headers.len == 0)
                    {
                        r->mp.status = SEND_PART;
                    }
                    r->sock_timer = 0;
                }
            }
            else if (r->mp.status == SEND_PART)
            {
                int wr = send_entity(r);
                if (wr == 0)
                {
                    r->sock_timer = 0;
                    r->mp.rg = r->rg.get();
                    if (r->mp.rg)
                    {
                        set_part(r);
                    }
                    else
                    {
                        r->mp.status = SEND_END;
                        r->mp.hdr = "";
                        r->mp.hdr << "\r\n--" << boundary << "--\r\n";
                        r->resp_headers.len = r->mp.hdr.size();
                        r->resp_headers.p = r->mp.hdr.c_str();
                    }
                }
                else if (wr < 0)
                {
                    if (wr == TRYAGAIN)
                        r->io_status = SELECT;
                    else
                    {
                        r->err = wr;
                        r->req_hdrs.iReferer = MAX_HEADERS - 1;
                        r->req_hdrs.Value[r->req_hdrs.iReferer] = "Connection reset by peer";
                        del_from_list(r);
                        end_response(r);
                    }
                }
            }
            else if (r->mp.status == SEND_END)
            {
                int wr = send(r->clientSocket, r->resp_headers.p, r->resp_headers.len, 0);
                if (wr == SOCKET_ERROR)
                {
                    int err = WSAGetLastError();
                    if (err == WSAEWOULDBLOCK)
                        r->io_status = SELECT;
                    else
                    {
                        r->err = -1;
                        r->req_hdrs.iReferer = MAX_HEADERS - 1;
                        r->req_hdrs.Value[r->req_hdrs.iReferer] = "Connection reset by peer";
                        del_from_list(r);
                        end_response(r);
                    }
                }
                else if (wr > 0)
                {
                    r->resp_headers.p += wr;
                    r->resp_headers.len -= wr;
                    r->resp.send_bytes += wr;
                    if (r->resp_headers.len == 0)
                    {
                        del_from_list(r);
                        end_response(r);
                    }
                    else
                        r->sock_timer = 0;
                }
            }
        }
        else if (r->source_entity == FROM_DATA_BUFFER)
        {
            int wr = send_html(r);
            if (wr == 0)
            {
                del_from_list(r);
                end_response(r);
            }
            else if (wr < 0)
            {
                if (wr == TRYAGAIN)
                    r->io_status = SELECT;
                else
                {
                    r->err = -1;
                    r->req_hdrs.iReferer = MAX_HEADERS - 1;
                    r->req_hdrs.Value[r->req_hdrs.iReferer] = "Connection reset by peer";
                    del_from_list(r);
                    end_response(r);
                }
            }
            else // (wr > 0)
                r->sock_timer = 0;
        }
    }
    else if (r->operation == SEND_RESP_HEADERS)
    {
        int wr = send(r->clientSocket, r->resp_headers.p, r->resp_headers.len, 0);
        if (wr == SOCKET_ERROR)
        {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK)
                r->io_status = SELECT;
            else
            {
                r->err = -1;
                r->req_hdrs.iReferer = MAX_HEADERS - 1;
                r->req_hdrs.Value[r->req_hdrs.iReferer] = "Connection reset by peer";
                del_from_list(r);
                end_response(r);
            }
        }
        else if (wr > 0)
        {
            r->resp_headers.p += wr;
            r->resp_headers.len -= wr;
            if (r->resp_headers.len == 0)
            {
                if (r->reqMethod == M_HEAD)
                {
                    del_from_list(r);
                    end_response(r);
                }
                else
                {
                    r->sock_timer = 0;
                    if (r->source_entity == FROM_DATA_BUFFER)
                    {
                        if (r->html.len == 0)
                        {
                            del_from_list(r);
                            end_response(r);
                        }
                        else
                        {
                            r->operation = SEND_ENTITY;
                        }
                    }
                    else if (r->source_entity == FROM_FILE)
                    {
                        r->operation = SEND_ENTITY;
                    }
                    else if (r->source_entity == MULTIPART_ENTITY)
                    {
                        if ((r->mp.rg = r->rg.get()))
                        {
                            r->operation = SEND_ENTITY;
                            set_part(r);
                        }
                        else
                        {
                            r->err = -1;
                            del_from_list(r);
                            end_response(r);
                        }
                    }
                }
            }
            else
                r->sock_timer = 0;
        }
    }
    else if (r->operation == READ_REQUEST)
    {
        int ret = r->read_request_headers();
        if (ret < 0)
        {
            if (ret == TRYAGAIN)
                r->io_status = SELECT;
            else
            {
                r->err = -1;
                del_from_list(r);
                end_response(r);
            }
        }
        else if (ret > 0)
        {
            del_from_list(r);
            push_resp_list(r);
        }
        else
            r->sock_timer = 0;
    }
    else
    {
        print_err("<%s:%d> ? operation=%s\n", __func__, __LINE__, get_str_operation(r->operation));
        del_from_list(r);
        end_response(r);
    }
}
//======================================================================
void event_handler(int n_thr)
{
    event_handler_cl[n_thr].init(n_thr);
    print_err("<%s:%d> +++++ worker thread %d run +++++\n", __func__, __LINE__, n_thr);

    event_handler_cl[n_thr].snd_buf = new(nothrow) char[event_handler_cl[n_thr].size_buf];
    if (!event_handler_cl[n_thr].snd_buf)
    {
        print_err("[%d]<%s:%d> Error malloc()\n", n_thr, __func__, __LINE__);
        exit(1);
    }

    while (1)
    {
        if (event_handler_cl[n_thr].wait_conn())
            break;

        event_handler_cl[n_thr].set_list();
        int ret = event_handler_cl[n_thr].select_(n_thr);
        if (ret < 0)
        {
            print_err("[%d]<%s:%d> Error poll_()\n", n_thr, __func__, __LINE__);
            break;
        }
    }

    if (event_handler_cl[n_thr].snd_buf)
        delete [] event_handler_cl[n_thr].snd_buf;
     print_err("<%s:%d> ***** close thread %d\n", __func__, __LINE__, n_thr);
}
//======================================================================
void push_pollin_list(Connect *r)
{
    event_handler_cl[r->numThr].push_pollin_list(r);
}
//======================================================================
void push_send_file(Connect *r)
{
    event_handler_cl[r->numThr].push_send_file(r);
}
//======================================================================
void push_send_multipart(Connect *r)
{
    event_handler_cl[r->numThr].push_send_multipart(r);
}
//======================================================================
void push_send_html(Connect *r)
{
    event_handler_cl[r->numThr].push_send_html(r);
}
//======================================================================
int event_handler_cl_new()
{
    event_handler_cl = new(nothrow) EventHandlerClass [conf->NumWorkThreads];
    if (!event_handler_cl)
    {
        print_err("<%s:%d> Error create array EventHandlerClass: %s\n", __func__, __LINE__, strerror(errno));
        return -1;
    }

    return 0;
}
//======================================================================
void event_handler_cl_delete()
{
    if (event_handler_cl)
        delete [] event_handler_cl;
}
//======================================================================
void close_work_threads()
{
    for (int i = 0; i < conf->NumWorkThreads; ++i)
    {
        event_handler_cl[i].close_event_handler();
    }
}
