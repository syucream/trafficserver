/** @file

  Http2Callbacks.cc

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

#include "Http2Callbacks.h"
#include "Http2ClientSession.h"
#include <arpa/inet.h>

void
http2_callbacks_init(nghttp2_session_callbacks * callbacks)
{
  memset(callbacks, 0, sizeof(nghttp2_session_callbacks));

  callbacks->send_callback = http2_send_callback;
  callbacks->recv_callback = http2_recv_callback;
  callbacks->on_frame_recv_callback = http2_on_frame_recv_callback;
  callbacks->on_invalid_frame_recv_callback = http2_on_invalid_frame_recv_callback;
  callbacks->on_data_chunk_recv_callback = http2_on_data_chunk_recv_callback;
  callbacks->before_frame_send_callback = http2_before_frame_send_callback;
  callbacks->on_frame_send_callback = http2_on_frame_send_callback;
  callbacks->on_frame_not_send_callback = http2_on_frame_not_send_callback;
  callbacks->on_stream_close_callback = http2_on_stream_close_callback;
  callbacks->on_unknown_frame_recv_callback = http2_on_unknown_frame_recv_callback;
  callbacks->on_begin_headers_callback = http2_on_begin_headers_callback;
  callbacks->on_header_callback = http2_on_header_callback;
  callbacks->select_padding_callback = http2_select_padding_callback;
}

void
http2_prepare_status_response(Http2ClientSession * sm, int stream_id, const char *status)
{
  Http2Request *req = sm->req_map[stream_id];
  char date_str[32];
  mime_format_date(date_str, time(0));

  std::vector < nghttp2_nv > nv;
  nv.push_back(make_nv_lc(":status", status));
  nv.push_back(make_nv_ls("server", HTTP2D_SERVER));
  nv.push_back(make_nv_lc("date", date_str));
  for (size_t i = 0; i < req->headers.size(); ++i) {
    nv.push_back(make_nv_ss(req->headers[i].first, req->headers[i].second));
  }

  int r = nghttp2_submit_response(sm->session, stream_id, nv.data(), nv.size(), NULL);
  TSAssert(r == 0);

  TSVIOReenable(sm->write_vio);
}

static void
http2_show_data_frame(const char *head_str, nghttp2_session * /*session */ , uint8_t flags,
                      int32_t stream_id, int32_t length, void *user_data)
{
  if (!is_debug_tag_set("http2"))
    return;

  Http2ClientSession *sm = (Http2ClientSession *) user_data;

  Debug("http2", "%s DATA frame (sm_id:%" PRIu64 ", stream_id:%d, flag:%d, length:%d)\n",
        head_str, sm->sm_id, stream_id, flags, length);
}

static void
http2_show_ctl_frame(const char *head_str, nghttp2_session * /*session */ , nghttp2_frame_type type,
                     const nghttp2_frame * frame, void *user_data)
{
  if (!is_debug_tag_set("http2"))
    return;

  Http2ClientSession *sm = (Http2ClientSession *) user_data;
  switch (type) {
  case NGHTTP2_WINDOW_UPDATE:{
      nghttp2_window_update *f = (nghttp2_window_update *) frame;
      Debug("http2", "%s WINDOW_UPDATE (sm_id:%" PRIu64 ", stream_id:%d, flag:%d, delta_window_size:%d)\n",
            head_str, sm->sm_id, f->hd.stream_id, f->hd.flags, f->window_size_increment);
    }
    break;
  case NGHTTP2_SETTINGS:{
      nghttp2_settings *f = (nghttp2_settings *) frame;
      Debug("http2", "%s SETTINGS frame (sm_id:%" PRIu64 ", flag:%d, length:%zd, niv:%zu)\n",
            head_str, sm->sm_id, f->hd.flags, f->hd.length, f->niv);
      for (size_t i = 0; i < f->niv; i++) {
        Debug("http2", "    (%d:%d)\n", f->iv[i].settings_id, f->iv[i].value);
      }
    }
    break;
  case NGHTTP2_HEADERS:{
      nghttp2_headers *f = (nghttp2_headers *) frame;
      Debug("http2", "%s HEADERS frame (sm_id:%" PRIu64 ", stream_id:%d, flag:%d, length:%zd)\n",
            head_str, sm->sm_id, f->hd.stream_id, f->hd.flags, f->hd.length);
    }
    break;
  case NGHTTP2_RST_STREAM:{
      nghttp2_rst_stream *f = (nghttp2_rst_stream *) frame;
      Debug("http2", "%s RST_STREAM (sm_id:%" PRIu64 ", stream_id:%d, flag:%d, length:%zd, code:%d)\n",
            head_str, sm->sm_id, f->hd.stream_id, f->hd.flags, f->hd.length, f->error_code);
    }
    break;
  case NGHTTP2_GOAWAY:{
      nghttp2_goaway *f = (nghttp2_goaway *) frame;
      Debug("http2", "%s GOAWAY frame (sm_id:%" PRIu64 ", last_stream_id:%d, flag:%d, length:%zd\n",
            head_str, sm->sm_id, f->last_stream_id, f->hd.flags, f->hd.length);
    }
  default:
    break;
  }
  return;
}

static int
http2_fetcher_launch(Http2Request * req, TSFetchMethod method)
{
  string url;
  int fetch_flags;
  const sockaddr *client_addr;
  Http2ClientSession *sm = req->http2_sm;

  url = req->scheme + "://" + req->host + req->path;
  client_addr = TSNetVConnRemoteAddrGet(reinterpret_cast<TSVConn>(sm->vc));

  req->url = url;
  Debug("http2", "++++Request[%" PRIu64 ":%d] %s\n", sm->sm_id, req->stream_id, req->url.c_str());

  //
  // HTTP content should be dechunked before packed into HTTP2.
  //
  fetch_flags = TS_FETCH_FLAGS_DECHUNK;
  req->fetch_sm = TSFetchCreate((TSCont)sm, method, url.c_str(), req->version.c_str(), client_addr, fetch_flags);
  TSFetchUserDataSet(req->fetch_sm, req);

  //
  // Set header list
  //
  for (size_t i = 0; i < req->headers.size(); i++) {

    if (*req->headers[i].first.c_str() == ':')
      continue;

    TSFetchHeaderAdd(req->fetch_sm,
                     req->headers[i].first.c_str(), req->headers[i].first.size(),
                     req->headers[i].second.c_str(), req->headers[i].second.size());
  }

  TSFetchLaunch(req->fetch_sm);
  return 0;
}

ssize_t
http2_send_callback(nghttp2_session * /*session */ , const uint8_t * data, size_t length,
                    int /*flags */ , void *user_data)
{
  Http2ClientSession *sm = (Http2ClientSession *) user_data;

  sm->total_size += length;
  TSIOBufferWrite(sm->resp_buffer, data, length);

  Debug("http2", "----http2_send_callback, length:%zu\n", length);

  return length;
}

ssize_t
http2_recv_callback(nghttp2_session * /*session */ , uint8_t * buf, size_t length,
                    int /*flags */ , void *user_data)
{
  Http2ClientSession *sm = (Http2ClientSession *) user_data;

  int64_t already = 0;
  TSIOBufferBlock blk = TSIOBufferReaderStart(sm->req_reader);

  while (blk) {
    int64_t wavail = length - already;

    TSIOBufferBlock next_blk = TSIOBufferBlockNext(blk);
    int64_t blk_len;
    const char *start = TSIOBufferBlockReadStart(blk, sm->req_reader, &blk_len);

    int64_t need = std::min(wavail, blk_len);

    memcpy(&buf[already], start, need);

    already += need;

    if (already >= (int64_t) length)
      break;

    blk = next_blk;
  }

  TSIOBufferReaderConsume(sm->req_reader, already);

  if (sm->read_vio) {
    TSVIOReenable(sm->read_vio);
  }

  if (!already)
    return NGHTTP2_ERR_WOULDBLOCK;

  return already;
}

static void
http2_process_headers_frame_as_request(Http2ClientSession * sm, Http2Request * req)
{
  // validate request headers
  for (size_t i = 0; i < req->headers.size(); ++i) {
    const std::string & field = req->headers[i].first;
    const std::string & value = req->headers[i].second;

    if (field == ":path")
      req->path = value;
    else if (field == ":method")
      req->method = value;
    else if (field == ":scheme")
      req->scheme = value;
    else if (field == ":authority")
      req->host = value;
  }
  req->version = "HTTP/2";

  if (!req->path.size() || !req->method.size() || !req->scheme.size()
      || !req->host.size()) {
    http2_prepare_status_response(sm, req->stream_id, STATUS_400);
    return;
  }

  if (req->method == "GET")
    http2_fetcher_launch(req, TS_FETCH_METHOD_GET);
  else if (req->method == "POST")
    http2_fetcher_launch(req, TS_FETCH_METHOD_POST);
  else if (req->method == "PURGE")
    http2_fetcher_launch(req, TS_FETCH_METHOD_PURGE);
  else if (req->method == "PUT")
    http2_fetcher_launch(req, TS_FETCH_METHOD_PUT);
  else if (req->method == "HEAD")
    http2_fetcher_launch(req, TS_FETCH_METHOD_HEAD);
  else if (req->method == "CONNECT")
    http2_fetcher_launch(req, TS_FETCH_METHOD_CONNECT);
  else if (req->method == "DELETE")
    http2_fetcher_launch(req, TS_FETCH_METHOD_DELETE);
  else if (req->method == "LAST")
    http2_fetcher_launch(req, TS_FETCH_METHOD_LAST);
  else
    http2_prepare_status_response(sm, req->stream_id, STATUS_405);

}

int
http2_on_frame_recv_callback(nghttp2_session * session, const nghttp2_frame * frame, void *user_data)
{
  Http2Request *req;
  Http2ClientSession *sm = (Http2ClientSession *) user_data;

  http2_show_ctl_frame("++++RECV", session, (nghttp2_frame_type) frame->hd.type, frame, user_data);

  switch (frame->hd.type) {
  case NGHTTP2_DATA:
  case NGHTTP2_HEADERS:
    if (frame->headers.cat == NGHTTP2_HCAT_REQUEST) {
      req = sm->req_map[frame->hd.stream_id];
      http2_process_headers_frame_as_request(sm, req);
    }
    break;

  case NGHTTP2_SETTINGS:
    if (sm->write_vio) TSVIOReenable(sm->write_vio);
    break;

  case NGHTTP2_WINDOW_UPDATE:
    if (sm->write_vio) TSVIOReenable(sm->write_vio);
    break;

  default:
    break;
  }
  return 0;
}

int
http2_on_invalid_frame_recv_callback(nghttp2_session * /*session */ ,
                                     const nghttp2_frame * /*frame */ ,
                                     nghttp2_error_code /*status_code */ ,
                                     void * /*user_data */ )
{
  //TODO
  return 1;
}

int
http2_on_data_chunk_recv_callback(nghttp2_session * /*session */ , uint8_t /*flags */ ,
                                  int32_t stream_id, const uint8_t * data, size_t len, void *user_data)
{
  Http2ClientSession *sm = (Http2ClientSession *) user_data;
  Http2Request *req = sm->req_map[stream_id];

  //
  // Http2Request has been deleted on error, drop this data;
  //
  if (!req)
    return 1;

  Debug("http2", "++++Fetcher Append Data, len:%zu\n", len);
  TSFetchWriteData(req->fetch_sm, data, len);

  return 0;
}

void
http2_on_data_recv_callback(nghttp2_session * session, uint8_t flags,
                            int32_t stream_id, int32_t length, void *user_data)
{
  Http2ClientSession *sm = (Http2ClientSession *) user_data;
  Http2Request *req = sm->req_map[stream_id];

  http2_show_data_frame("++++RECV", session, flags, stream_id, length, user_data);

  //
  // After Http2Request has been deleted on error, the corresponding
  // client might continue to send POST data, We should reenable
  // sm->write_vio so that WINDOW_UPDATE has a chance to be sent.
  //
  if (!req) {
    TSVIOReenable(sm->write_vio);
    return;
  }

  req->delta_window_size += length;

  Debug("http2", "----sm_id:%" PRId64 ", stream_id:%d, delta_window_size:%d\n",
        sm->sm_id, stream_id, req->delta_window_size);

  if (req->delta_window_size >= HTTP2_CFG.http2.initial_window_size / 2) {
    Debug("http2", "----Reenable write_vio for WINDOW_UPDATE frame, delta_window_size:%d\n", req->delta_window_size);

    //
    // Need not to send WINDOW_UPDATE frame here, what we should
    // do is to reenable sm->write_vio, and than nghttp2_session_send()
    // will be triggered and it'll send WINDOW_UPDATE frame automatically.
    //
    TSVIOReenable(sm->write_vio);

    req->delta_window_size = 0;
  }

  return;
}

int
http2_before_frame_send_callback(nghttp2_session * /*session */ ,
                                 const nghttp2_frame * /*frame */ ,
                                 void * /*user_data */ )
{
  //TODO
  return 0;
}

int
http2_on_frame_send_callback(nghttp2_session * session, const nghttp2_frame * frame, void *user_data)
{
  http2_show_ctl_frame("----SEND", session, (nghttp2_frame_type) (frame->hd.type), frame, user_data);

  return 0;
}

int
http2_on_frame_not_send_callback(nghttp2_session * /*session */ ,
                                 const nghttp2_frame * /*frame */ ,
                                 int /*error_code */ ,
                                 void * /*user_data */ )
{
  //TODO
  return 0;
}

void
http2_on_data_send_callback(nghttp2_session * session, uint8_t flags,
                            int32_t stream_id, int32_t length, void *user_data)
{
  Http2ClientSession *sm = (Http2ClientSession *) user_data;

  http2_show_data_frame("----SEND", session, flags, stream_id, length, user_data);

  TSVIOReenable(sm->read_vio);
  return;
}

int
http2_on_stream_close_callback(nghttp2_session * /*session */ ,
                               int32_t /*stream_id */ ,
                               nghttp2_error_code /*status_code */ ,
                               void * /*user_data */ )
{
  //TODO
  return 0;
}

int
http2_on_unknown_frame_recv_callback(nghttp2_session * /*session */ ,
                                     const uint8_t * /*head */ ,
                                     size_t /*headlen */ ,
                                     const uint8_t * /*payload */ ,
                                     size_t /*payloadlen */ ,
                                     void * /*user_data */ )
{
  //TODO
  return 1;
}

int
http2_on_begin_headers_callback(nghttp2_session * /*session */ ,
                                const nghttp2_frame * frame, void *user_data)
{
  Http2Request *req;
  Http2ClientSession *sm = (Http2ClientSession *) user_data;

  if (frame->hd.type == NGHTTP2_HEADERS && 
      frame->headers.cat == NGHTTP2_HCAT_REQUEST) {
    req = http2RequestAllocator.alloc();
    req->init(sm, frame->hd.stream_id);
    sm->req_map[frame->hd.stream_id] = req;
  }
  return 0;
}

int
http2_on_header_callback(nghttp2_session * /*session */ ,
                         const nghttp2_frame * frame,
                         const uint8_t * name, size_t namelen, const uint8_t * value, size_t valuelen, uint8_t /*flags*/ , void *user_data)
{
  int stream_id = frame->hd.stream_id;
  Http2ClientSession *sm = (Http2ClientSession *) user_data;
  std::string n((char *) name, namelen);
  std::string v((char *) value, valuelen);

  sm->req_map[stream_id]->headers.push_back(make_pair(n, v));
  return 0;
}

ssize_t
http2_select_padding_callback(nghttp2_session * /*session */ ,
                              const nghttp2_frame * frame, size_t /*max_payloadlen */ ,
                              void * /*user_data */ )
{
  // Don't insert padding
  return frame->hd.length;
}
