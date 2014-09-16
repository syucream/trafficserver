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

#ifndef _HTTP_SM_FETCH_H_
#define _HTTP_SM_FETCH_H_

#include "P_EventSystem.h"
#include "HttpSM.h"

#define HTTP_FETCH_EVENT_RESPONSE_HEADER (HTTP_FETCH_EVENTS_START + 1)
#define HTTP_FETCH_EVENT_RESPONSE_BODY   (HTTP_FETCH_EVENTS_START + 2)

class HttpFetchSM:public HttpSM
{

public:

  HttpFetchSM();

  static HttpFetchSM *allocate();
  void init(Continuation* cp);
  void destroy();

  HTTPHdr* get_headers_ref();
  void start_with_parsed_headers(Continuation * cont);

  void set_user_data(void* data);
  void* get_user_data();

protected:
  Continuation* contp;
  void* user_data;
  
  void handle_api_return();
};

inline HttpFetchSM *
HttpFetchSM::allocate()
{
  extern ClassAllocator<HttpFetchSM> httpFetchSMAllocator;
  return httpFetchSMAllocator.alloc();
}

inline void
HttpFetchSM::init(Continuation* cp)
{
  contp = cp;
  HttpSM::init();
}

inline void
HttpFetchSM::set_user_data(void* data)
{
  user_data = data;
}

inline void*
HttpFetchSM::get_user_data()
{
  return user_data;
}

#endif
