/** @file

  SSLNextProtocolSet

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

#include "ink_config.h"
#include "apidefs.h"
#include "libts.h"
#include "P_SSLNextProtocolSet.h"

// For currently defined protocol strings, see
// http://technotes.googlecode.com/git/nextprotoneg.html. The OpenSSL
// documentation tells us to return a string in "wire format". The
// draft NPN RFC helpfuly refuses to document the wire format. The
// above link says we need to send length-prefixed strings, but does
// not say how many bytes the length is. For the record, it's 1.

unsigned char *
append_protocol(const char * proto, unsigned char * buf)
{
  size_t sz = strlen(proto);
  *buf++ = (unsigned char)sz;
  memcpy(buf, proto, sz);
  return buf + sz;
}

static bool
create_npn_advertisement(
  const SSLNextProtocolSet::NextProtocolEndpoint::list_type& endpoints,
  unsigned char ** npn, size_t * len)
{
  const SSLNextProtocolSet::NextProtocolEndpoint * ep;
  unsigned char * advertised;

  *npn = NULL;
  *len = 0;

  for (ep = endpoints.head; ep != NULL; ep = endpoints.next(ep)) {
    *len += (strlen(ep->protocol) + 1);
  }

  *npn = advertised = (unsigned char *)ats_malloc(*len);
  if (!(*npn)) {
    goto fail;
  }

  for (ep = endpoints.head; ep != NULL; ep = endpoints.next(ep)) {
    advertised = append_protocol(ep->protocol, advertised);
  }

  return true;

fail:
  ats_free(*npn);
  *npn = NULL;
  *len = 0;
  return false;
}

bool
SSLNextProtocolSet::advertiseProtocols(const unsigned char ** out, unsigned * len) const
{
  if (!npn && !this->endpoints.empty()) {
    create_npn_advertisement(this->endpoints, &npn, &npnsz);
  }

  if (npn && npnsz) {
    *out = npn;
    *len = npnsz;
    Debug("ssl", "advertised NPN set %.*s", (int)npnsz, npn);
    return true;
  }

  return false;
}

bool
SSLNextProtocolSet::registerEndpoint(const char * proto, Continuation * ep)
{
  // Once we start advertising, the set is closed. We need to hand an immutable
  // string down into OpenSSL, and there is no mechanism to tell us when it's
  // done with it so we have to keep it forever.
  if (this->npn) {
    return false;
  }

  if (strlen(proto) > 255) {
    return false;
  }

  if (findEndpoint(proto) == NULL) {
    this->endpoints.push(NEW(new NextProtocolEndpoint(proto, ep)));
    return true;
  }

  return false;
}

bool
SSLNextProtocolSet::unregisterEndpoint(const char * proto, Continuation * ep)
{

  for (NextProtocolEndpoint * e = this->endpoints.head;
        e; e = this->endpoints.next(e)) {
    if (strcmp(proto, e->protocol) == 0 && e->endpoint == ep) {
      // Protocol must be registered only once; no need to remove
      // any more entries.
      this->endpoints.remove(e);
      return true;
    }
  }

  return false;
}

Continuation *
SSLNextProtocolSet::findEndpoint(
  const unsigned char * proto, unsigned len,
  TSClientProtoStack *proto_stack, const char **selected_protocol) const
{
  for (const NextProtocolEndpoint * ep = this->endpoints.head;
        ep != NULL; ep = this->endpoints.next(ep)) {
    size_t sz = strlen(ep->protocol);
    if (sz == len && memcmp(ep->protocol, proto, len) == 0) {
      if (proto_stack) {
        *proto_stack = ep->proto_stack;
      }

      if (selected_protocol) {
        *selected_protocol = ep->protocol;
      }

      return ep->endpoint;
    }
  }

  return NULL;
}

Continuation *
SSLNextProtocolSet::findEndpoint(const char * proto) const
{
  for (const NextProtocolEndpoint * ep = this->endpoints.head;
        ep != NULL; ep = this->endpoints.next(ep)) {
    if (strcmp(proto, ep->protocol) == 0) {
      return ep->endpoint;
    }
  }

  return NULL;
}

SSLNextProtocolSet::SSLNextProtocolSet()
  : npn(0), npnsz(0)
{
}

SSLNextProtocolSet::~SSLNextProtocolSet()
{
  ats_free(this->npn);

  for (NextProtocolEndpoint * ep; (ep = this->endpoints.pop());) {
    delete ep;
  }
}

SSLNextProtocolSet::NextProtocolEndpoint::NextProtocolEndpoint(
        const char * proto, Continuation * ep)
  : protocol(proto), endpoint(ep)
{
  if (proto == TS_NPN_PROTOCOL_HTTP_1_1 ||
      proto == TS_NPN_PROTOCOL_HTTP_1_0) {
    proto_stack = ((1u << TS_PROTO_TLS) | (1u << TS_PROTO_HTTP));
  } else if (proto == TS_NPN_PROTOCOL_SPDY_3_1 ||
             proto == TS_NPN_PROTOCOL_SPDY_3 ||
             proto == TS_NPN_PROTOCOL_SPDY_2 ||
             proto == TS_NPN_PROTOCOL_SPDY_1) {
    proto_stack = ((1u << TS_PROTO_TLS) | (1u << TS_PROTO_SPDY));
  } else {
    proto_stack = (1u << TS_PROTO_TLS);
  }
}

SSLNextProtocolSet::NextProtocolEndpoint::~NextProtocolEndpoint()
{
}
