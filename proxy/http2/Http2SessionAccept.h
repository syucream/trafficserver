/** @file

  Http2SessionAccept

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

#ifndef Http2AcceptCont_H_
#define Http2AcceptCont_H_

#include "P_Net.h"
#include "P_EventSystem.h"
#include "P_UnixNet.h"
#include "I_IOBuffer.h"

class Http2SessionAccept:public SessionAccept
{
public:
  Http2SessionAccept(Continuation * ep);
  ~Http2SessionAccept() {}

  void accept(NetVConnection *, MIOBuffer *, IOBufferReader *);

private:
  int mainEvent(int event, void *netvc);
  Http2SessionAccept(const Http2SessionAccept &);     // disabled
  Http2SessionAccept & operator =(const Http2SessionAccept &);        // disabled

  Continuation *endpoint;
};

#endif /* Http2AcceptCont_H_ */
