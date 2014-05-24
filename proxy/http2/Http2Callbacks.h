/** @file

  Http2Callbacks.h

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

#ifndef __P_HTTP2_CALLBACKS_H__
#define __P_HTTP2_CALLBACKS_H__

#include <nghttp2/nghttp2.h>
class Http2ClientSession;

void http2_callbacks_init(nghttp2_session_callbacks * callbacks);
void http2_prepare_status_response(Http2ClientSession * sm, int stream_id, const char *status);

/**
 * @functypedef
 *
 * Callback function invoked when |session| wants to send data to the
 * remote peer. The implementation of this function must send at most
 * |length| bytes of data stored in |data|. The |flags| is currently
 * not used and always 0. It must return the number of bytes sent if
 * it succeeds.  If it cannot send any single byte without blocking,
 * it must return :enum:`NGHTTP2_ERR_WOULDBLOCK`. For other errors, it
 * must return :enum:`NGHTTP2_ERR_CALLBACK_FAILURE`.
 */
ssize_t http2_send_callback(nghttp2_session * session, const uint8_t * data, size_t length, int flags, void *user_data);

/**
 * @functypedef
 *
 * Callback function invoked when |session| wants to receive data from
 * the remote peer. The implementation of this function must read at
 * most |length| bytes of data and store it in |buf|. The |flags| is
 * currently not used and always 0. It must return the number of bytes
 * written in |buf| if it succeeds. If it cannot read any single byte
 * without blocking, it must return :enum:`NGHTTP2_ERR_WOULDBLOCK`. If
 * it gets EOF before it reads any single byte, it must return
 * :enum:`NGHTTP2_ERR_EOF`. For other errors, it must return
 * :enum:`NGHTTP2_ERR_CALLBACK_FAILURE`.
 */
ssize_t http2_recv_callback(nghttp2_session * session, uint8_t * buf, size_t length, int flags, void *user_data);

/**
 * @functypedef
 *
 * Callback function invoked by `nghttp2_session_recv()` when a
 * control frame is received.
 */
int http2_on_frame_recv_callback(nghttp2_session * session, const nghttp2_frame * frame, void *user_data);

/**
 * @functypedef
 *
 * Callback function invoked by `nghttp2_session_recv()` when an
 * invalid control frame is received. The |status_code| is one of the
 * :enum:`nghttp2_status_code` and indicates the error. When this
 * callback function is invoked, the library automatically submits
 * either RST_STREAM or GOAWAY frame.
 */
int http2_on_invalid_frame_recv_callback
  (nghttp2_session * session, const nghttp2_frame * frame, nghttp2_error_code status_code, void *user_data);

/**
 * @functypedef
 *
 * Callback function invoked when a chunk of data in DATA frame is
 * received. The |stream_id| is the stream ID this DATA frame belongs
 * to. The |flags| is the flags of DATA frame which this data chunk is
 * contained. ``(flags & NGHTTP2_DATA_FLAG_FIN) != 0`` does not
 * necessarily mean this chunk of data is the last one in the
 * stream. You should use :type:`nghttp2_on_data_recv_callback` to
 * know all data frames are received.
 */
int http2_on_data_chunk_recv_callback
  (nghttp2_session * session, uint8_t flags, int32_t stream_id, const uint8_t * data, size_t len, void *user_data);

/**
 * @functypedef
 *
 * Callback function invoked when DATA frame is received. The actual
 * data it contains are received by
 * :type:`nghttp2_on_data_chunk_recv_callback`.
 */
void http2_on_data_recv_callback
  (nghttp2_session * session, uint8_t flags, int32_t stream_id, int32_t length, void *user_data);

/**
 * @functypedef
 *
 * Callback function invoked before the control frame |frame| of type
 * |type| is sent. This may be useful, for example, to know the stream
 * ID of SYN_STREAM frame (see also
 * `nghttp2_session_get_stream_user_data()`), which is not assigned
 * when it was queued.
 */
int http2_before_frame_send_callback(nghttp2_session * session, const nghttp2_frame * frame, void *user_data);

/**
 * @functypedef
 *
 * Callback function invoked after the control frame |frame| of type
 * |type| is sent.
 */
int http2_on_frame_send_callback(nghttp2_session * session, const nghttp2_frame * frame, void *user_data);

/**
 * @functypedef
 *
 * Callback function invoked after the control frame |frame| of type
 * |type| is not sent because of the error. The error is indicated by
 * the |error_code|, which is one of the values defined in
 * :type:`nghttp2_error`.
 */
int http2_on_frame_not_send_callback
  (nghttp2_session * session, const nghttp2_frame * frame, int error_code, void *user_data);

/**
 * @functypedef
 *
 * Callback function invoked after DATA frame is sent.
 */
void http2_on_data_send_callback
  (nghttp2_session * session, uint8_t flags, int32_t stream_id, int32_t length, void *user_data);

/**
 * @functypedef
 *
 * Callback function invoked when the stream |stream_id| is
 * closed. The reason of closure is indicated by the
 * |status_code|. The stream_user_data, which was specified in
 * `nghttp2_submit_request()` or `nghttp2_submit_syn_stream()`, is
 * still available in this function.
 */
int http2_on_stream_close_callback
  (nghttp2_session * session, int32_t stream_id, nghttp2_error_code status_code, void *user_data);

/**
 * @functypedef
 *
 * Callback function invoked when the library needs the cryptographic
 * proof that the client has possession of the private key associated
 * with the certificate for the given |origin|.  If called with
 * |prooflen| == 0, the implementation of this function must return
 * the length of the proof in bytes. If called with |prooflen| > 0,
 * write proof into |proof| exactly |prooflen| bytes and return 0.
 *
 * Because the client certificate vector has limited number of slots,
 * the application code may be required to pass the same proof more
 * than once.

ssize_t http2_get_credential_proof
(nghttp2_session *session, const nghttp2_origin *origin,
 uint8_t *proof, size_t prooflen, void *user_data);
 */

/**
 * @functypedef
 *
 * Callback function invoked when the library needs the length of the
 * client certificate chain for the given |origin|.  The
 * implementation of this function must return the length of the
 * client certificate chain.  If no client certificate is required for
 * the given |origin|, return 0.  If positive integer is returned,
 * :type:`nghttp2_get_credential_proof` and
 * :type:`nghttp2_get_credential_cert` callback functions will be used
 * to get the cryptographic proof and certificate respectively.

ssize_t http2_get_credential_ncerts
(nghttp2_session *session, const nghttp2_origin *origin, void *user_data);
 */

/**
 * @functypedef
 *
 * Callback function invoked when the library needs the client
 * certificate for the given |origin|. The |idx| is the index of the
 * certificate chain and 0 means the leaf certificate of the chain.
 * If called with |certlen| == 0, the implementation of this function
 * must return the length of the certificate in bytes. If called with
 * |certlen| > 0, write certificate into |cert| exactly |certlen|
 * bytes and return 0.

ssize_t http2_get_credential_cert
(nghttp2_session *session, const nghttp2_origin *origin, size_t idx,
 uint8_t *cert, size_t certlen, void *user_data);
 */

/**
 * @functypedef
 *
 * Callback function invoked when the request from the remote peer is
 * received.  In other words, the frame with FIN flag set is received.
 * In HTTP, this means HTTP request, including request body, is fully
 * received.
 */
void http2_on_request_recv_callback(nghttp2_session * session, int32_t stream_id, void *user_data);

/**
 * @functypedef
 *
 * Callback function invoked when the received control frame octets
 * could not be parsed correctly. The |type| indicates the type of
 * received control frame. The |head| is the pointer to the header of
 * the received frame. The |headlen| is the length of the
 * |head|. According to the HTTP2 spec, the |headlen| is always 8. In
 * other words, the |head| is the first 8 bytes of the received frame.
 * The |payload| is the pointer to the data portion of the received
 * frame.  The |payloadlen| is the length of the |payload|. This is
 * the data after the length field. The |error_code| is one of the
 * error code defined in :enum:`nghttp2_error` and indicates the
 * error.
 */
void http2_on_ctrl_recv_parse_error_callback
  (nghttp2_session * session, nghttp2_frame_type type,
   const uint8_t * head, size_t headlen, const uint8_t * payload, size_t payloadlen, int error_code, void *user_data);

/**
 * @functypedef
 *
 * Callback function invoked when the received control frame type is
 * unknown. The |head| is the pointer to the header of the received
 * frame. The |headlen| is the length of the |head|. According to the
 * HTTP2 spec, the |headlen| is always 8. In other words, the |head| is
 * the first 8 bytes of the received frame.  The |payload| is the
 * pointer to the data portion of the received frame.  The
 * |payloadlen| is the length of the |payload|. This is the data after
 * the length field.
 */
int http2_on_unknown_frame_recv_callback
  (nghttp2_session * session,
   const uint8_t * head, size_t headlen, const uint8_t * payload, size_t payloadlen, void *user_data);

int http2_on_begin_headers_callback(nghttp2_session * session, const nghttp2_frame * frame, void *user_data);

int http2_on_header_callback
  (nghttp2_session * session,
   const nghttp2_frame * frame,
   const uint8_t * name, size_t namelen, const uint8_t * value, size_t valuelen, uint8_t flags, void *user_data);

ssize_t http2_select_padding_callback
  (nghttp2_session * session, const nghttp2_frame * frame, size_t max_payloadlen, void *user_data);

#endif
