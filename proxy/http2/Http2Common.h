/** @file

  Http2Common.h

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

#ifndef __P_HTTP2_COMMON_H__
#define __P_HTTP2_COMMON_H__

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <limits.h>
#include <string.h>
#include <string>
#include <vector>
#include <map>

#include "P_Net.h"
#include "ts/ts.h"
#include "ts/libts.h"
#include "ts/experimental.h"
#include <nghttp2/nghttp2.h>
using namespace std;

#define STATUS_200      "200 OK"
#define STATUS_304      "304 Not Modified"
#define STATUS_400      "400 Bad Request"
#define STATUS_404      "404 Not Found"
#define STATUS_405      "405 Method Not Allowed"
#define STATUS_500      "500 Internal Server Error"
#define DEFAULT_HTML    "index.html"
#define HTTP2D_SERVER    "ATS Nghttp2/" NGHTTP2_VERSION

#define atomic_fetch_and_add(a, b)  __sync_fetch_and_add(&a, b)
#define atomic_fetch_and_sub(a, b)  __sync_fetch_and_sub(&a, b)
#define atomic_inc(a)   atomic_fetch_and_add(a, 1)
#define atomic_dec(a)   atomic_fetch_and_sub(a, 1)

struct Http2Config
{
  bool verbose;
  bool enable_tls;
  bool keep_host_port;
  int serv_port;
  int max_concurrent_streams;
  int initial_window_size;
  nghttp2_session_callbacks callbacks;
};

struct Config
{
  Http2Config http2;
  int nr_accept_threads;
  int accept_no_activity_timeout;
  int no_activity_timeout_in;
};

// Http2 Name/Value pairs
class Http2NV
{
public:

  Http2NV(TSFetchSM fetch_sm);

  std::vector < nghttp2_nv > &nv()
  {
    return _nv;
  }

private:

    Http2NV();

  std::vector < nghttp2_nv > _nv;
};

int http2_config_load();


nghttp2_nv make_nv_ss(const std::string & name, const std::string & value);
template < size_t N > nghttp2_nv make_nv_lc(const char (&name)[N], const char *value)
{
  return {
    (uint8_t *) name, (uint8_t *) value, (uint16_t) (N - 1), (uint16_t) strlen(value)
  };
}

template < size_t N > nghttp2_nv make_nv_ls(const char (&name)[N], const std::string & value)
{
  return {
    (uint8_t *) name, (uint8_t *) value.c_str(), (uint16_t) (N - 1), (uint16_t) value.size()
  };
}

extern Config HTTP2_CFG;
#endif
