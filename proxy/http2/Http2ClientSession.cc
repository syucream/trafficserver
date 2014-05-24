/** @file

  Http2ClientSession.cc

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include "Http2ClientSession.h"
#include "I_Net.h"

static ClassAllocator < Http2ClientSession > http2ClientSessionAllocator("Http2ClientSessionAllocator");
ClassAllocator < Http2Request > http2RequestAllocator("Http2RequestAllocator");

static int http2_process_read(TSEvent event, Http2ClientSession * sm);
static int http2_process_write(TSEvent event, Http2ClientSession * sm);
static int http2_process_fetch(TSEvent event, Http2ClientSession * sm, void *edata);
static int http2_process_fetch_header(TSEvent event, Http2ClientSession * sm, TSFetchSM fetch_sm);
static int http2_process_fetch_body(TSEvent event, Http2ClientSession * sm, TSFetchSM fetch_sm);
static int http2_skip_client_connection_preface(TSIOBufferReader req_reader);
static uint64_t g_sm_id;
static uint64_t g_sm_cnt;

void
Http2Request::clear()
{
  if (fetch_sm)
    TSFetchDestroy(fetch_sm);

  vector<pair<string, string> >().swap(headers);

  std::string().swap(url);
  std::string().swap(host);
  std::string().swap(path);
  std::string().swap(scheme);
  std::string().swap(method);
  std::string().swap(version);

  Debug("http2", "****Delete Request[%" PRIu64 ":%d]\n", http2_sm->sm_id, stream_id);
}

void
Http2ClientSession::init(NetVConnection * netvc)
{
  atomic_inc(g_sm_cnt);

  this->mutex = new_ProxyMutex();
  this->vc = netvc;
  this->req_map.clear();

  int r = nghttp2_session_server_new(&session, &HTTP2_CFG.http2.callbacks, this);

  ink_release_assert(r == 0);
  sm_id = atomic_inc(g_sm_id);
  total_size = 0;
  start_time = TShrtime();

  this->vc->set_inactivity_timeout(HRTIME_SECONDS(HTTP2_CFG.accept_no_activity_timeout));
  SET_HANDLER(&Http2ClientSession::state_session_start);
}

void
Http2ClientSession::clear()
{
  uint64_t nr_pending;
  int last_event = event;
  //
  // Http2Request depends on Http2ClientSession,
  // we should delete it firstly to avoid race.
  //
  map < int, Http2Request * >::iterator iter = req_map.begin();
  map < int, Http2Request * >::iterator endIter = req_map.end();
  for (; iter != endIter; ++iter) {
    Http2Request *req = iter->second;
    req->clear();
    http2RequestAllocator.free(req);
  }
  req_map.clear();

  this->mutex = NULL;

  if (vc) {
    TSVConnClose(reinterpret_cast<TSVConn>(vc));
    vc = NULL;
  }

  if (req_reader) {
    TSIOBufferReaderFree(req_reader);
    req_reader = NULL;
  }

  if (req_buffer) {
    TSIOBufferDestroy(req_buffer);
    req_buffer = NULL;
  }

  if (resp_reader) {
    TSIOBufferReaderFree(resp_reader);
    resp_reader = NULL;
  }

  if (resp_buffer) {
    TSIOBufferDestroy(resp_buffer);
    resp_buffer = NULL;
  }

  if (session) {
    nghttp2_session_del(session);
    session = NULL;
  }

  nr_pending = atomic_dec(g_sm_cnt);
  Debug("http2-free", "****Delete Http2ClientSession[%" PRIu64 "], last event:%d, nr_pending:%" PRIu64 "\n",
        sm_id, last_event, --nr_pending);
}

void
http2_sm_create(NetVConnection * netvc, MIOBuffer * iobuf, IOBufferReader * reader)
{
  Http2ClientSession *sm;

  sm = http2ClientSessionAllocator.alloc();
  sm->init(netvc);

  sm->req_buffer = iobuf ? reinterpret_cast<TSIOBuffer>(iobuf) : TSIOBufferCreate();
  sm->req_reader = reader ? reinterpret_cast<TSIOBufferReader>(reader) : TSIOBufferReaderAlloc(sm->req_buffer);

  sm->resp_buffer = TSIOBufferCreate();
  sm->resp_reader = TSIOBufferReaderAlloc(sm->resp_buffer);

  eventProcessor.schedule_imm(sm, ET_NET);
}

int
Http2ClientSession::state_session_start(int /* event */ , void * /* edata */ )
{
  if (TSIOBufferReaderAvail(this->req_reader) > 0) {
    http2_process_read(TS_EVENT_VCONN_WRITE_READY, this);
  }

  this->read_vio = (TSVIO)this->vc->do_io_read(this, INT64_MAX, reinterpret_cast<MIOBuffer *>(this->req_buffer));
  this->write_vio = (TSVIO)this->vc->do_io_write(this, INT64_MAX, reinterpret_cast<IOBufferReader *>(this->resp_reader));

  SET_HANDLER(&Http2ClientSession::state_session_readwrite);

  TSVIOReenable(this->read_vio);
  return EVENT_CONT;
}

int
Http2ClientSession::state_session_readwrite(int event, void * edata)
{
  int ret = 0;
  bool from_fetch = false;
 
  this->event = event;

  if (edata == this->read_vio) {
    Debug("http2", "++++[READ EVENT]\n");
    if (event != TS_EVENT_VCONN_READ_READY && event != TS_EVENT_VCONN_READ_COMPLETE) {
      ret = -1;
      goto out;
    }
    ret = http2_process_read((TSEvent)event, this);
  } else if (edata == this->write_vio) {
    Debug("http2", "----[WRITE EVENT]\n");
    if (event != TS_EVENT_VCONN_WRITE_READY && event != TS_EVENT_VCONN_WRITE_COMPLETE) {
      ret = -1;
      goto out;
    }
    ret = http2_process_write((TSEvent)event, this);
  } else {
    from_fetch = true;
    ret = http2_process_fetch((TSEvent)event, this, edata);
  }

  Debug("http2-event", "++++Http2ClientSession[%" PRIu64 "], EVENT:%d, ret:%d, nr_pending:%" PRIu64 "\n",
        this->sm_id, event, ret, g_sm_cnt);
out:
  if (ret) {
    this->clear();
    http2ClientSessionAllocator.free(this);
  } else if (!from_fetch) {
    this->vc->set_inactivity_timeout(HRTIME_SECONDS(HTTP2_CFG.no_activity_timeout_in));
  }

  return EVENT_CONT;
}

static int
http2_process_read(TSEvent /* event ATS_UNUSED */ , Http2ClientSession * sm)
{
  http2_skip_client_connection_preface(sm->req_reader);
  return nghttp2_session_recv(sm->session);
}

static int
http2_process_write(TSEvent /* event ATS_UNUSED */ , Http2ClientSession * sm)
{
  int ret;

  ret = nghttp2_session_send(sm->session);

  if (TSIOBufferReaderAvail(sm->resp_reader) > 0)
    TSVIOReenable(sm->write_vio);
  else {
    Debug("http2", "----TOTAL SEND (sm_id:%" PRIu64 ", total_size:%" PRIu64 ", total_send:%" PRId64 ")\n",
          sm->sm_id, sm->total_size, TSVIONDoneGet(sm->write_vio));

    //
    // We should reenable read_vio when no data to be written,
    // otherwise it could lead to hang issue when client POST
    // data is waiting to be read.
    //
    TSVIOReenable(sm->read_vio);
  }

  return ret;
}

static int
http2_process_fetch(TSEvent event, Http2ClientSession * sm, void *edata)
{
  int ret = -1;
  TSFetchSM fetch_sm = (TSFetchSM) edata;
  Http2Request *req = (Http2Request *) TSFetchUserDataGet(fetch_sm);

  switch ((int) event) {

  case TS_FETCH_EVENT_EXT_HEAD_DONE:
    Debug("http2", "----[FETCH HEADER DONE]\n");
    ret = http2_process_fetch_header(event, sm, fetch_sm);
    break;

  case TS_FETCH_EVENT_EXT_BODY_READY:
    Debug("http2", "----[FETCH BODY READY]\n");
    ret = http2_process_fetch_body(event, sm, fetch_sm);
    break;

  case TS_FETCH_EVENT_EXT_BODY_DONE:
    Debug("http2", "----[FETCH BODY DONE]\n");
    req->fetch_body_completed = true;
    ret = http2_process_fetch_body(event, sm, fetch_sm);
    break;

  default:
    Debug("http2", "----[FETCH ERROR]\n");
    if (req->fetch_body_completed)
      ret = 0;                  // Ignore fetch errors after FETCH BODY DONE
    else
      req->fetch_sm = NULL;
    break;
  }

  if (ret) {
    http2_prepare_status_response(sm, req->stream_id, STATUS_500);
    sm->req_map.erase(req->stream_id);
    req->clear();
    http2RequestAllocator.free(req);
  }

  return 0;
}

static int
http2_process_fetch_header(TSEvent /*event */ , Http2ClientSession * sm, TSFetchSM fetch_sm)
{
  int ret;
  Http2Request *req = (Http2Request *) TSFetchUserDataGet(fetch_sm);
  Http2NV http2_nv(fetch_sm);

  Debug("http2", "----nghttp2_submit_headers\n");
  ret = nghttp2_submit_headers(sm->session, NGHTTP2_FLAG_NONE,
                               req->stream_id, 0,
                               (const nghttp2_nv *) http2_nv.nv().data(), http2_nv.nv().size(), NULL);

  TSVIOReenable(sm->write_vio);
  return ret;
}

static ssize_t
http2_read_fetch_body_callback(nghttp2_session * /*session */ , int32_t stream_id,
                               uint8_t * buf, size_t length, uint32_t *eof, nghttp2_data_source * source, void *user_data)
{

  static int g_call_cnt;
  int64_t already;

  Http2ClientSession *sm = (Http2ClientSession *) user_data;
  Http2Request *req = (Http2Request *) source->ptr;

  //
  // req has been deleted, ignore this data.
  //
  if (req != sm->req_map[stream_id]) {
    Debug("http2", "    stream_id:%d, call:%d, req has been deleted, return 0\n", stream_id, g_call_cnt);
    *eof = 1;
    return 0;
  }

  already = TSFetchReadData(req->fetch_sm, buf, length);

  Debug("http2", "    stream_id:%d, call:%d, length:%ld, already:%ld\n", stream_id, g_call_cnt, length, already);
  if (HTTP2_CFG.http2.verbose)
    MD5_Update(&req->recv_md5, buf, already);

  TSVIOReenable(sm->write_vio);
  g_call_cnt++;

  req->fetch_data_len += already;
  if (already < (int64_t) length) {
    if (req->event == TS_FETCH_EVENT_EXT_BODY_DONE) {
      TSHRTime end_time = TShrtime();
      Debug("http2", "----Request[%" PRIu64 ":%d] %s %lld %d\n", sm->sm_id, req->stream_id,
            req->url.c_str(), (end_time - req->start_time) / TS_HRTIME_MSECOND, req->fetch_data_len);
      unsigned char digest[MD5_DIGEST_LENGTH];
      if (HTTP2_CFG.http2.verbose) {
        MD5_Final(digest, &req->recv_md5);
        Debug("http2", "----recv md5sum: ");
        for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
          Debug("http2", "%02x", digest[i]);
        }
        Debug("http2", "\n");
      }
      *eof = 1;
      sm->req_map.erase(stream_id);
      req->clear();
      http2RequestAllocator.free(req);
    } else if (already == 0) {
      req->need_resume_data = true;
      return NGHTTP2_ERR_DEFERRED;
    }
  }

  return already;
}

static int
http2_process_fetch_body(TSEvent event, Http2ClientSession * sm, TSFetchSM fetch_sm)
{
  int ret = 0;
  nghttp2_data_provider data_prd;
  Http2Request *req = (Http2Request *) TSFetchUserDataGet(fetch_sm);
  req->event = event;

  data_prd.source.ptr = (void *) req;
  data_prd.read_callback = http2_read_fetch_body_callback;

  if (!req->has_submitted_data) {
    req->has_submitted_data = true;
    Debug("http2", "----nghttp2_submit_data\n");
    ret = nghttp2_submit_data(sm->session, NGHTTP2_FLAG_END_STREAM, req->stream_id, &data_prd);
  } else if (req->need_resume_data) {
    Debug("http2", "----nghttp2_session_resume_data\n");
    ret = nghttp2_session_resume_data(sm->session, req->stream_id);
    if (ret == NGHTTP2_ERR_INVALID_ARGUMENT)
      ret = 0;
  }

  TSVIOReenable(sm->write_vio);
  return ret;
}

static int
http2_skip_client_connection_preface(TSIOBufferReader req_reader)
{
  const char *start;
  TSIOBufferBlock blk;
  int64_t blk_len;

  blk = TSIOBufferReaderStart(req_reader);
  if (!blk)
    return 0;
  start = TSIOBufferBlockReadStart(blk, req_reader, &blk_len);

  if (blk_len >= NGHTTP2_CLIENT_CONNECTION_PREFACE_LEN) {
    std::string ccheader(start, NGHTTP2_CLIENT_CONNECTION_PREFACE_LEN);

    if (ccheader == NGHTTP2_CLIENT_CONNECTION_PREFACE) {
      // Consume client connection preface
      TSIOBufferReaderConsume(req_reader, NGHTTP2_CLIENT_CONNECTION_PREFACE_LEN);
    }
  }
  return 0;
}
