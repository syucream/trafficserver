/** @file

  A brief file description

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

#include "HttpFetchSM.h"
#include "HttpDebugNames.h"

ClassAllocator<HttpFetchSM> httpFetchSMAllocator("httpFetchSMAllocator");

HttpFetchSM::HttpFetchSM() : user_data(NULL)
{
}

void
HttpFetchSM::destroy()
{
  cleanup();
  httpFetchSMAllocator.free(this);
}

HTTPHdr*
HttpFetchSM::get_headers_ref()
{
  return &t_state.hdr_info.client_request;
}

void
HttpFetchSM::start_with_parsed_headers(Continuation * cont)
{

  // Use passed continuation's mutex for this state machine
  this->mutex = cont->mutex;
  MUTEX_LOCK(lock, this->mutex, this_ethread());

  start_sub_sm();

  // Fix ME: What should these be set to since there is not a
  //   real client
  ats_ip4_set(&t_state.client_info.addr, htonl(INADDR_LOOPBACK), 0);
  t_state.backdoor_request = 0;
  t_state.client_info.port_attribute = HttpProxyPort::TRANSPORT_DEFAULT;

  t_state.req_flavor = HttpTransact::REQ_FLAVOR_SCHEDULED_UPDATE;

  call_transact_and_set_next_state(&HttpTransact::ModifyRequest);
}

/*
 * Override HttpSM::handle_api_return()
 */
void
HttpFetchSM::handle_api_return()
{
  if (t_state.api_next_action == HttpTransact::SM_ACTION_API_SEND_RESPONSE_HDR) {
    // Call handleEvent()
    contp->handleEvent(HTTP_FETCH_EVENT_RESPONSE_HEADER, this);
    // perform_cache_write_action();
  } else {
    HttpSM::handle_api_return();
  }
  return;
}

