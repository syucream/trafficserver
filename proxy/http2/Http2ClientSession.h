/** @file

  Http2ClientSession.h

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

#ifndef __P_HTTP2_SM_H__
#define __P_HTTP2_SM_H__

#include "Http2Common.h"
#include "Http2Callbacks.h"
#include <openssl/md5.h>

class Http2ClientSession;
typedef int (*Http2ClientSessionHandler) (TSCont contp, TSEvent event, void *data);

class Http2Request
{
public:
  Http2Request():http2_sm(NULL), stream_id(-1), fetch_sm(NULL),
    has_submitted_data(false), need_resume_data(false),
    fetch_data_len(0), delta_window_size(0), fetch_body_completed(false)
  {
  }

  Http2Request(Http2ClientSession * sm, int id):http2_sm(NULL), stream_id(-1), fetch_sm(NULL),
    has_submitted_data(false), need_resume_data(false),
    fetch_data_len(0), delta_window_size(0), fetch_body_completed(false)
  {
    init(sm, id);
  }

  ~Http2Request() {
    clear();
  }

  void init(Http2ClientSession * sm, int id)
  {
    http2_sm = sm;
    stream_id = id;
    headers.clear();

    MD5_Init(&recv_md5);
    start_time = TShrtime();
  }

  void clear();

  void append_nv(char **nv)
  {
    for (int i = 0; nv[i]; i += 2) {
      headers.push_back(make_pair(nv[i], nv[i + 1]));
    }
  }

public:
  int event;
  Http2ClientSession *http2_sm;
  int stream_id;
  TSHRTime start_time;
  TSFetchSM fetch_sm;
  bool has_submitted_data;
  bool need_resume_data;
  int fetch_data_len;
  int delta_window_size;
  bool fetch_body_completed;
  vector < pair < string, string > >headers;

  string url;
  string host;
  string path;
  string scheme;
  string method;
  string version;

  MD5_CTX recv_md5;
};

class Http2ClientSession : public Continuation
{

public:

  Http2ClientSession() : Continuation(NULL) {
  }

  ~Http2ClientSession() {
    clear();
  }

  void init(NetVConnection * netvc);
  void clear();

public:

  int64_t sm_id;
  uint64_t total_size;
  TSHRTime start_time;

  NetVConnection * vc;

  TSIOBuffer req_buffer;
  TSIOBufferReader req_reader;

  TSIOBuffer resp_buffer;
  TSIOBufferReader resp_reader;

  TSVIO read_vio;
  TSVIO write_vio;

  int event;
  nghttp2_session *session;

  map < int32_t, Http2Request * >req_map;

private:
  int state_session_start(int event, void * edata);
  int state_session_readwrite(int event, void * edata);
};

void http2_sm_create(NetVConnection * netvc, MIOBuffer * iobuf, IOBufferReader * reader);

extern ClassAllocator < Http2Request > http2RequestAllocator;

#endif
