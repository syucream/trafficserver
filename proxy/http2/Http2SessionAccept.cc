/** @file

  Http2NetAccept

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

#include "Http2SessionAccept.h"
#include "Error.h"

#if TS_HAS_HTTP2
#include "Http2ClientSession.h"
#endif

Http2SessionAccept::Http2SessionAccept(Continuation * ep)
:SessionAccept(new_ProxyMutex()), endpoint(ep)
{
#if TS_HAS_HTTP2
  http2_config_load();
#endif
  SET_HANDLER(&Http2SessionAccept::mainEvent);
}

int
Http2SessionAccept::mainEvent(int event, void * edata)
{
  if (event == NET_EVENT_ACCEPT) {
    NetVConnection * netvc = static_cast<NetVConnection *>(edata);
#if TS_HAS_HTTP2
    http2_sm_create(netvc, NULL, NULL);
#else
    Error("accepted a HTTP2 session, but HTTP2 support is not available");
    netvc->do_io_close();
#endif
    return EVENT_CONT;
  }

  MachineFatal("HTTP2 accept received fatal error: errno = %d", -((int)(intptr_t)edata));
  return EVENT_CONT;
}

void
Http2SessionAccept::accept(NetVConnection * netvc, MIOBuffer * iobuf, IOBufferReader * reader)
{
#if TS_HAS_HTTP2
  http2_sm_create(netvc, iobuf, reader);
#else
  (void)netvc;
  (void)iobuf;
  (void)reader;
  ink_release_assert(0);
#endif

}
