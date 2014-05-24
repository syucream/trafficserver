/** @file

  Http2Common.cc

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

#include "Http2Common.h"
#include "Http2Callbacks.h"

#include <sstream>

Config HTTP2_CFG;

int
http2_config_load()
{
  HTTP2_CFG.nr_accept_threads = 1;
  HTTP2_CFG.accept_no_activity_timeout = 30;
  HTTP2_CFG.no_activity_timeout_in = 30;
  HTTP2_CFG.http2.verbose = false;
  HTTP2_CFG.http2.enable_tls = false;
  HTTP2_CFG.http2.keep_host_port = false;
  //
  // HTTP2 plugin will share the same port number with
  // http server, unless '--port' is given.
  //
  HTTP2_CFG.http2.serv_port = -1;
  HTTP2_CFG.http2.max_concurrent_streams = 1000;
  HTTP2_CFG.http2.initial_window_size = (64 << 10);

  http2_callbacks_init(&HTTP2_CFG.http2.callbacks);

  return 0;
}

nghttp2_nv
make_nv_ss(const std::string & name, const std::string & value)
{
  return {
    (uint8_t *) name.c_str(), (uint8_t *) value.c_str(), (uint16_t) name.size(), (uint16_t) value.size()
  };
}

Http2NV::Http2NV(TSFetchSM fetch_sm)
{
  const char *name, *value;
  int name_len, value_len;
  TSMLoc loc, field_loc, next_loc;
  TSMBuffer bufp;

  bufp = TSFetchRespHdrMBufGet(fetch_sm);
  loc = TSFetchRespHdrMLocGet(fetch_sm);

  //
  // Process Status
  //
  int status_code = TSHttpHdrStatusGet(bufp, loc);
  value = (char *) TSHttpHdrReasonGet(bufp, loc, &value_len);
  std::stringstream ss;
  ss << status_code  << " " << std::string(value, value_len);
  _nv.push_back(make_nv_ls(":status", ss.str()));

  //
  // Process HTTP headers
  //
  field_loc = TSMimeHdrFieldGet(bufp, loc, 0);
  while (field_loc) {
    name = TSMimeHdrFieldNameGet(bufp, loc, field_loc, &name_len);
    TSReleaseAssert(name && name_len);

    //
    // According HTTP2 spec, in RESPONSE:
    // The Connection, Keep-Alive, Proxy-Connection, and
    // Transfer-Encoding headers are not valid and MUST not be sent.
    //
    if (strncasecmp(name, "Connection", name_len) &&
        strncasecmp(name, "Keep-Alive", name_len) &&
        strncasecmp(name, "Proxy-Connection", name_len) && strncasecmp(name, "Transfer-Encoding", name_len)) {
      value = TSMimeHdrFieldValueStringGet(bufp, loc, field_loc, -1, &value_len);

      // Any HTTP headers with empty value are invalid,
      // we should ignore them.
      if (value && value_len) {
          _nv.push_back((nghttp2_nv) {
              (uint8_t *) name,
              (uint8_t *) value,
              (uint16_t) name_len,
              (uint16_t) value_len});
      }
    }

    next_loc = TSMimeHdrFieldNext(bufp, loc, field_loc);
    TSHandleMLocRelease(bufp, loc, field_loc);
    field_loc = next_loc;
  }
}
