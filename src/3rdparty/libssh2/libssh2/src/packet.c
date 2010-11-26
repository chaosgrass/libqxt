/* Copyright (c) 2004-2007, Sara Golemon <sarag@libssh2.org>
 * Copyright (c) 2005,2006 Mikhail Gusarov
 * Copyright (c) 2009 by Daniel Stenberg
 * Copyright (c) 2010 Simon Josefsson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms,
 * with or without modification, are permitted provided
 * that the following conditions are met:
 *
 *   Redistributions of source code must retain the above
 *   copyright notice, this list of conditions and the
 *   following disclaimer.
 *
 *   Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the following
 *   disclaimer in the documentation and/or other materials
 *   provided with the distribution.
 *
 *   Neither the name of the copyright holder nor the names
 *   of any other contributors may be used to endorse or
 *   promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#include "libssh2_priv.h"
#include <errno.h>
#include <fcntl.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif

/* Needed for struct iovec on some platforms */
#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif

#include <sys/types.h>

#include "transport.h"
#include "channel.h"
#include "packet.h"

/*
 * libssh2_packet_queue_listener
 *
 * Queue a connection request for a listener
 */
static inline int
packet_queue_listener(LIBSSH2_SESSION * session, unsigned char *data,
                      unsigned long datalen,
                      packet_queue_listener_state_t *listen_state)
{
    /*
     * Look for a matching listener
     */
    /* 17 = packet_type(1) + channel(4) + reason(4) + descr(4) + lang(4) */
    unsigned long packet_len = 17 + (sizeof(FwdNotReq) - 1);
    unsigned char *p;
    LIBSSH2_LISTENER *listen = _libssh2_list_first(&session->listeners);
    char failure_code = 1;      /* SSH_OPEN_ADMINISTRATIVELY_PROHIBITED */
    int rc;

    (void) datalen;

    if (listen_state->state == libssh2_NB_state_idle) {
        unsigned char *s = data + (sizeof("forwarded-tcpip") - 1) + 5;
        listen_state->sender_channel = _libssh2_ntohu32(s);
        s += 4;

        listen_state->initial_window_size = _libssh2_ntohu32(s);
        s += 4;
        listen_state->packet_size = _libssh2_ntohu32(s);
        s += 4;

        listen_state->host_len = _libssh2_ntohu32(s);
        s += 4;
        listen_state->host = s;
        s += listen_state->host_len;
        listen_state->port = _libssh2_ntohu32(s);
        s += 4;

        listen_state->shost_len = _libssh2_ntohu32(s);
        s += 4;
        listen_state->shost = s;
        s += listen_state->shost_len;
        listen_state->sport = _libssh2_ntohu32(s);

        _libssh2_debug(session, LIBSSH2_TRACE_CONN,
                       "Remote received connection from %s:%ld to %s:%ld",
                       listen_state->shost, listen_state->sport,
                       listen_state->host, listen_state->port);

        listen_state->state = libssh2_NB_state_allocated;
    }

    if (listen_state->state != libssh2_NB_state_sent) {
        while (listen) {
            if ((listen->port == (int) listen_state->port) &&
                (strlen(listen->host) == listen_state->host_len) &&
                (memcmp (listen->host, listen_state->host,
                         listen_state->host_len) == 0)) {
                /* This is our listener */
                LIBSSH2_CHANNEL *channel = NULL;
                listen_state->channel = NULL;

                if (listen_state->state == libssh2_NB_state_allocated) {
                    if (listen->queue_maxsize &&
                        (listen->queue_maxsize <= listen->queue_size)) {
                        /* Queue is full */
                        failure_code = 4;       /* SSH_OPEN_RESOURCE_SHORTAGE */
                        _libssh2_debug(session, LIBSSH2_TRACE_CONN,
                                       "Listener queue full, ignoring");
                        listen_state->state = libssh2_NB_state_sent;
                        break;
                    }

                    channel = LIBSSH2_ALLOC(session, sizeof(LIBSSH2_CHANNEL));
                    if (!channel) {
                        _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                                       "Unable to allocate a channel for "
                                       "new connection");
                        failure_code = 4;       /* SSH_OPEN_RESOURCE_SHORTAGE */
                        listen_state->state = libssh2_NB_state_sent;
                        break;
                    }
                    listen_state->channel = channel;

                    memset(channel, 0, sizeof(LIBSSH2_CHANNEL));

                    channel->session = session;
                    channel->channel_type_len = sizeof("forwarded-tcpip") - 1;
                    channel->channel_type = LIBSSH2_ALLOC(session,
                                                          channel->
                                                          channel_type_len +
                                                          1);
                    if (!channel->channel_type) {
                        _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                                       "Unable to allocate a channel for new"
                                       " connection");
                        LIBSSH2_FREE(session, channel);
                        failure_code = 4;       /* SSH_OPEN_RESOURCE_SHORTAGE */
                        listen_state->state = libssh2_NB_state_sent;
                        break;
                    }
                    memcpy(channel->channel_type, "forwarded-tcpip",
                           channel->channel_type_len + 1);

                    channel->remote.id = listen_state->sender_channel;
                    channel->remote.window_size_initial =
                        LIBSSH2_CHANNEL_WINDOW_DEFAULT;
                    channel->remote.window_size =
                        LIBSSH2_CHANNEL_WINDOW_DEFAULT;
                    channel->remote.packet_size =
                        LIBSSH2_CHANNEL_PACKET_DEFAULT;

                    channel->local.id = _libssh2_channel_nextid(session);
                    channel->local.window_size_initial =
                        listen_state->initial_window_size;
                    channel->local.window_size =
                        listen_state->initial_window_size;
                    channel->local.packet_size = listen_state->packet_size;

                    _libssh2_debug(session, LIBSSH2_TRACE_CONN,
                                   "Connection queued: channel %lu/%lu win %lu/%lu packet %lu/%lu",
                                   channel->local.id, channel->remote.id,
                                   channel->local.window_size,
                                   channel->remote.window_size,
                                   channel->local.packet_size,
                                   channel->remote.packet_size);

                    p = listen_state->packet;
                    *(p++) = SSH_MSG_CHANNEL_OPEN_CONFIRMATION;
                    _libssh2_store_u32(&p, channel->remote.id);
                    _libssh2_store_u32(&p, channel->local.id);
                    _libssh2_store_u32(&p, channel->remote.window_size_initial);
                    _libssh2_store_u32(&p, channel->remote.packet_size);

                    listen_state->state = libssh2_NB_state_created;
                }

                if (listen_state->state == libssh2_NB_state_created) {
                    rc = _libssh2_transport_write(session, listen_state->packet,
                                                  17);
                    if (rc == LIBSSH2_ERROR_EAGAIN)
                        return rc;
                    else if (rc) {
                        listen_state->state = libssh2_NB_state_idle;
                        return _libssh2_error(session, rc,
                                              "Unable to send channel "
                                              "open confirmation");
                    }

                    /* Link the channel into the end of the queue list */
                    _libssh2_list_add(&listen->queue,
                                      &listen_state->channel->node);
                    listen->queue_size++;

                    listen_state->state = libssh2_NB_state_idle;
                    return 0;
                }
            }

            listen = _libssh2_list_next(&listen->node);
        }

        listen_state->state = libssh2_NB_state_sent;
    }

    /* We're not listening to you */
    p = listen_state->packet;
    *(p++) = SSH_MSG_CHANNEL_OPEN_FAILURE;
    _libssh2_store_u32(&p, listen_state->sender_channel);
    _libssh2_store_u32(&p, failure_code);
    _libssh2_store_str(&p, FwdNotReq, sizeof(FwdNotReq) - 1);
    _libssh2_htonu32(p, 0);

    rc = _libssh2_transport_write(session, listen_state->packet,
                                  packet_len);
    if (rc == LIBSSH2_ERROR_EAGAIN) {
        return rc;
    } else if (rc) {
        listen_state->state = libssh2_NB_state_idle;
        return _libssh2_error(session, rc, "Unable to send open failure");

    }
    listen_state->state = libssh2_NB_state_idle;
    return 0;
}

/*
 * packet_x11_open
 *
 * Accept a forwarded X11 connection
 */
static inline int
packet_x11_open(LIBSSH2_SESSION * session, unsigned char *data,
                unsigned long datalen,
                packet_x11_open_state_t *x11open_state)
{
    int failure_code = 2;       /* SSH_OPEN_CONNECT_FAILED */
    /* 17 = packet_type(1) + channel(4) + reason(4) + descr(4) + lang(4) */
    unsigned long packet_len = 17 + (sizeof(X11FwdUnAvil) - 1);
    unsigned char *p;
    LIBSSH2_CHANNEL *channel = x11open_state->channel;
    int rc;

    (void) datalen;

    if (x11open_state->state == libssh2_NB_state_idle) {
        unsigned char *s = data + (sizeof("x11") - 1) + 5;
        x11open_state->sender_channel = _libssh2_ntohu32(s);
        s += 4;
        x11open_state->initial_window_size = _libssh2_ntohu32(s);
        s += 4;
        x11open_state->packet_size = _libssh2_ntohu32(s);
        s += 4;
        x11open_state->shost_len = _libssh2_ntohu32(s);
        s += 4;
        x11open_state->shost = s;
        s += x11open_state->shost_len;
        x11open_state->sport = _libssh2_ntohu32(s);

        _libssh2_debug(session, LIBSSH2_TRACE_CONN,
                       "X11 Connection Received from %s:%ld on channel %lu",
                       x11open_state->shost, x11open_state->sport,
                       x11open_state->sender_channel);

        x11open_state->state = libssh2_NB_state_allocated;
    }

    if (session->x11) {
        if (x11open_state->state == libssh2_NB_state_allocated) {
            channel = LIBSSH2_ALLOC(session, sizeof(LIBSSH2_CHANNEL));
            if (!channel) {
                _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                               "Unable to allocate a channel for new connection");
                failure_code = 4;       /* SSH_OPEN_RESOURCE_SHORTAGE */
                goto x11_exit;
            }
            memset(channel, 0, sizeof(LIBSSH2_CHANNEL));

            channel->session = session;
            channel->channel_type_len = sizeof("x11") - 1;
            channel->channel_type = LIBSSH2_ALLOC(session,
                                                  channel->channel_type_len +
                                                  1);
            if (!channel->channel_type) {
                _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                               "Unable to allocate a channel for new connection");
                LIBSSH2_FREE(session, channel);
                failure_code = 4;       /* SSH_OPEN_RESOURCE_SHORTAGE */
                goto x11_exit;
            }
            memcpy(channel->channel_type, "x11",
                   channel->channel_type_len + 1);

            channel->remote.id = x11open_state->sender_channel;
            channel->remote.window_size_initial =
                LIBSSH2_CHANNEL_WINDOW_DEFAULT;
            channel->remote.window_size = LIBSSH2_CHANNEL_WINDOW_DEFAULT;
            channel->remote.packet_size = LIBSSH2_CHANNEL_PACKET_DEFAULT;

            channel->local.id = _libssh2_channel_nextid(session);
            channel->local.window_size_initial =
                x11open_state->initial_window_size;
            channel->local.window_size = x11open_state->initial_window_size;
            channel->local.packet_size = x11open_state->packet_size;

            _libssh2_debug(session, LIBSSH2_TRACE_CONN,
                           "X11 Connection established: channel %lu/%lu win %lu/%lu packet %lu/%lu",
                           channel->local.id, channel->remote.id,
                           channel->local.window_size,
                           channel->remote.window_size,
                           channel->local.packet_size,
                           channel->remote.packet_size);
            p = x11open_state->packet;
            *(p++) = SSH_MSG_CHANNEL_OPEN_CONFIRMATION;
            _libssh2_store_u32(&p, channel->remote.id);
            _libssh2_store_u32(&p, channel->local.id);
            _libssh2_store_u32(&p, channel->remote.window_size_initial);
            _libssh2_store_u32(&p, channel->remote.packet_size);

            x11open_state->state = libssh2_NB_state_created;
        }

        if (x11open_state->state == libssh2_NB_state_created) {
            rc = _libssh2_transport_write(session, x11open_state->packet, 17);
            if (rc == LIBSSH2_ERROR_EAGAIN) {
                return rc;
            } else if (rc) {
                x11open_state->state = libssh2_NB_state_idle;
                return _libssh2_error(session, LIBSSH2_ERROR_SOCKET_SEND,
                                      "Unable to send channel open "
                                      "confirmation");
            }

            /* Link the channel into the session */
            _libssh2_list_add(&session->channels, &channel->node);

            /*
             * Pass control to the callback, they may turn right around and
             * free the channel, or actually use it
             */
            LIBSSH2_X11_OPEN(channel, (char *)x11open_state->shost,
                             x11open_state->sport);

            x11open_state->state = libssh2_NB_state_idle;
            return 0;
        }
    } else {
        failure_code = 4;       /* SSH_OPEN_RESOURCE_SHORTAGE */
    }

  x11_exit:
    p = x11open_state->packet;
    *(p++) = SSH_MSG_CHANNEL_OPEN_FAILURE;
    _libssh2_store_u32(&p, x11open_state->sender_channel);
    _libssh2_store_u32(&p, failure_code);
    _libssh2_store_str(&p, X11FwdUnAvil, sizeof(X11FwdUnAvil) - 1);
    _libssh2_htonu32(p, 0);

    rc = _libssh2_transport_write(session, x11open_state->packet, packet_len);
    if (rc == LIBSSH2_ERROR_EAGAIN) {
        return rc;
    } else if (rc) {
        x11open_state->state = libssh2_NB_state_idle;
        return _libssh2_error(session, rc, "Unable to send open failure");
    }
    x11open_state->state = libssh2_NB_state_idle;
    return 0;
}

/*
 * _libssh2_packet_add
 *
 * Create a new packet and attach it to the brigade. Called from the transport
 * layer when it has received a packet.
 *
 * The input pointer 'data' is pointing to allocated data that this function
 * is asked to deal with so on failure OR success, it must be freed fine.
 */
int
_libssh2_packet_add(LIBSSH2_SESSION * session, unsigned char *data,
                    size_t datalen, int macstate)
{
    int rc;

    if (session->packAdd_state == libssh2_NB_state_idle) {
        session->packAdd_data_head = 0;

        /* Zero the whole thing out */
        memset(&session->packAdd_key_state, 0,
               sizeof(session->packAdd_key_state));

        /* Zero the whole thing out */
        memset(&session->packAdd_Qlstn_state, 0,
               sizeof(session->packAdd_Qlstn_state));

        /* Zero the whole thing out */
        memset(&session->packAdd_x11open_state, 0,
               sizeof(session->packAdd_x11open_state));

        _libssh2_debug(session, LIBSSH2_TRACE_TRANS,
                       "Packet type %d received, length=%d",
                       (int) data[0], (int) datalen);
        if (macstate == LIBSSH2_MAC_INVALID) {
            if (session->macerror &&
                LIBSSH2_MACERROR(session, (char *) data, datalen) == 0) {
                /* Calling app has given the OK, Process it anyway */
                macstate = LIBSSH2_MAC_CONFIRMED;
            } else {
                if (session->ssh_msg_disconnect) {
                    LIBSSH2_DISCONNECT(session, SSH_DISCONNECT_MAC_ERROR,
                                       "Invalid MAC received",
                                       sizeof("Invalid MAC received") - 1,
                                       "", 0);
                }
                LIBSSH2_FREE(session, data);
                return _libssh2_error(session, LIBSSH2_ERROR_INVALID_MAC,
                                      "Invalid MAC received");
            }
        }

        session->packAdd_state = libssh2_NB_state_allocated;
    }

    /*
     * =============================== NOTE ===============================
     * I know this is very ugly and not a really good use of "goto", but
     * this case statement would be even uglier to do it any other way
     */
    if (session->packAdd_state == libssh2_NB_state_jump1) {
        goto libssh2_packet_add_jump_point1;
    } else if (session->packAdd_state == libssh2_NB_state_jump2) {
        goto libssh2_packet_add_jump_point2;
    } else if (session->packAdd_state == libssh2_NB_state_jump3) {
        goto libssh2_packet_add_jump_point3;
    } else if (session->packAdd_state == libssh2_NB_state_jump4) {
        goto libssh2_packet_add_jump_point4;
    } else if (session->packAdd_state == libssh2_NB_state_jump5) {
        goto libssh2_packet_add_jump_point5;
    }

/* FIXME: I've noticed that DATA is accessed without proper
 * out-of-bounds checking (i.e., DATALEN) in many places
 * below. --simon */

    if (session->packAdd_state == libssh2_NB_state_allocated) {
        /* A couple exceptions to the packet adding rule: */
        switch (data[0]) {
        case SSH_MSG_DISCONNECT:
        {
            char *message, *language;
            int reason, message_len, language_len;

            reason = _libssh2_ntohu32(data + 1);
            message_len = _libssh2_ntohu32(data + 5);
            /* 9 = packet_type(1) + reason(4) + message_len(4) */
            message = (char *) data + 9;
            language_len = _libssh2_ntohu32(data + 9 + message_len);
            /*
             * This is where we hack on the data a little,
             * Use the MSB of language_len to to a terminating NULL
             * (In all liklihood it is already)
             * Shift the language tag back a byte (In all likelihood
             * it's zero length anyway)
             * Store a NULL in the last byte of the packet to terminate
             * the language string
             * With the lengths passed this isn't *REALLY* necessary,
             * but it's "kind"
             */
            message[message_len] = '\0';
            language = (char *) data + 9 + message_len + 3;
            if (language_len) {
                memmove(language, language + 1, language_len);
            }
            language[language_len] = '\0';

            if (session->ssh_msg_disconnect) {
                LIBSSH2_DISCONNECT(session, reason, message,
                                   message_len, language, language_len);
            }
            _libssh2_debug(session, LIBSSH2_TRACE_TRANS,
                           "Disconnect(%d): %s(%s)", reason,
                           message, language);
            LIBSSH2_FREE(session, data);
            session->socket_state = LIBSSH2_SOCKET_DISCONNECTED;
            session->packAdd_state = libssh2_NB_state_idle;
            return _libssh2_error(session, LIBSSH2_ERROR_SOCKET_DISCONNECT,
                                  "socket disconnect");
        }
        break;

        case SSH_MSG_IGNORE:
            if (datalen >= 5) {
                /* Back it up one and add a trailing NULL */
                memmove(data, data + 1, datalen - 1);
                data[datalen] = '\0';
                if (session->ssh_msg_ignore) {
                    LIBSSH2_IGNORE(session, (char *) data + 4, datalen - 1);
                }
            } else if (session->ssh_msg_ignore) {
                LIBSSH2_IGNORE(session, "", 0);
            }
            LIBSSH2_FREE(session, data);
            session->packAdd_state = libssh2_NB_state_idle;
            return 0;

        case SSH_MSG_DEBUG:
        {
            int always_display = data[0];
            char *message, *language;
            int message_len, language_len;

            message_len = _libssh2_ntohu32(data + 2);
            /* 6 = packet_type(1) + display(1) + message_len(4) */
            message = (char *) data + 6;
            language_len = _libssh2_ntohu32(data + 6 + message_len);
            /*
             * This is where we hack on the data a little,
             * Use the MSB of language_len to to a terminating NULL
             * (In all liklihood it is already)
             * Shift the language tag back a byte (In all likelihood
             * it's zero length anyway)
             * Store a NULL in the last byte of the packet to terminate
             * the language string
             * With the lengths passed this isn't *REALLY* necessary,
             * but it's "kind"
             */
            message[message_len] = '\0';
            language = (char *) data + 6 + message_len + 3;
            if (language_len) {
                memmove(language, language + 1, language_len);
            }
            language[language_len] = '\0';

            if (session->ssh_msg_debug) {
                LIBSSH2_DEBUG(session, always_display, message,
                              message_len, language, language_len);
            }
            /*
             * _libssh2_debug will actually truncate this for us so
             * that it's not an inordinate about of data
             */
            _libssh2_debug(session, LIBSSH2_TRACE_TRANS,
                           "Debug Packet: %s", message);
            LIBSSH2_FREE(session, data);
            session->packAdd_state = libssh2_NB_state_idle;
            return 0;
        }
        break;

        case SSH_MSG_GLOBAL_REQUEST:
        {
            uint32_t strlen = _libssh2_ntohu32(data + 1);
            unsigned char want_reply = data[5 + strlen];

            _libssh2_debug(session,
                           LIBSSH2_TRACE_CONN,
                           "Received global request type %.*s (wr %X)",
                           strlen, data + 5, want_reply);

            if (want_reply) {
              libssh2_packet_add_jump_point5:
                session->packAdd_state = libssh2_NB_state_jump5;
                data[0] = SSH_MSG_REQUEST_FAILURE;
                rc = _libssh2_transport_write(session, data, 1);
                if (rc == LIBSSH2_ERROR_EAGAIN)
                    return rc;
                LIBSSH2_FREE(session, data);
                session->packAdd_state = libssh2_NB_state_idle;
                return 0;
            }
        }
        break;

        case SSH_MSG_CHANNEL_EXTENDED_DATA:
            /* streamid(4) */
            session->packAdd_data_head += 4;
        case SSH_MSG_CHANNEL_DATA:
            /* packet_type(1) + channelno(4) + datalen(4) */
            session->packAdd_data_head += 9;

            session->packAdd_channel =
                _libssh2_channel_locate(session, _libssh2_ntohu32(data + 1));

            if (!session->packAdd_channel) {
                _libssh2_error(session, LIBSSH2_ERROR_CHANNEL_UNKNOWN,
                               "Packet received for unknown channel, ignoring");
                LIBSSH2_FREE(session, data);
                session->packAdd_state = libssh2_NB_state_idle;
                return 0;
            }
#ifdef LIBSSH2DEBUG
            {
                unsigned long stream_id = 0;
                if (data[0] == SSH_MSG_CHANNEL_EXTENDED_DATA) {
                    stream_id = _libssh2_ntohu32(data + 5);
                }

                _libssh2_debug(session, LIBSSH2_TRACE_CONN,
                               "%d bytes packet_add() for %lu/%lu/%lu",
                               (int) (datalen - session->packAdd_data_head),
                               session->packAdd_channel->local.id,
                               session->packAdd_channel->remote.id,
                               stream_id);
            }
#endif
            if ((session->packAdd_channel->remote.extended_data_ignore_mode ==
                 LIBSSH2_CHANNEL_EXTENDED_DATA_IGNORE) &&
                (data[0] == SSH_MSG_CHANNEL_EXTENDED_DATA)) {
                /* Pretend we didn't receive this */
                LIBSSH2_FREE(session, data);

                _libssh2_debug(session, LIBSSH2_TRACE_CONN,
                               "Ignoring extended data and refunding %d bytes",
                               (int) (datalen - 13));
                /* Adjust the window based on the block we just freed */
              libssh2_packet_add_jump_point1:
                session->packAdd_state = libssh2_NB_state_jump1;
                rc = _libssh2_channel_receive_window_adjust(session->
                                                            packAdd_channel,
                                                            datalen - 13,
                                                            0, NULL);
                if (rc == LIBSSH2_ERROR_EAGAIN)
                    return rc;

                session->packAdd_state = libssh2_NB_state_idle;
                return 0;
            }

            /*
             * REMEMBER! remote means remote as source of data,
             * NOT remote window!
             */
            if (session->packAdd_channel->remote.packet_size <
                (datalen - session->packAdd_data_head)) {
                /*
                 * Spec says we MAY ignore bytes sent beyond
                 * packet_size
                 */
                _libssh2_error(session,
                               LIBSSH2_ERROR_CHANNEL_PACKET_EXCEEDED,
                               "Packet contains more data than we offered"
                               " to receive, truncating");
                datalen =
                    session->packAdd_channel->remote.packet_size +
                    session->packAdd_data_head;
            }
            if (session->packAdd_channel->remote.window_size <= 0) {
                /*
                 * Spec says we MAY ignore bytes sent beyond
                 * window_size
                 */
                _libssh2_error(session,
                               LIBSSH2_ERROR_CHANNEL_WINDOW_EXCEEDED,
                               "The current receive window is full,"
                               " data ignored");
                LIBSSH2_FREE(session, data);
                session->packAdd_state = libssh2_NB_state_idle;
                return 0;
            }
            /* Reset EOF status */
            session->packAdd_channel->remote.eof = 0;

            if ((datalen - session->packAdd_data_head) >
                session->packAdd_channel->remote.window_size) {
                _libssh2_error(session,
                               LIBSSH2_ERROR_CHANNEL_WINDOW_EXCEEDED,
                               "Remote sent more data than current "
                               "window allows, truncating");
                datalen =
                    session->packAdd_channel->remote.window_size +
                    session->packAdd_data_head;
            }
            else {
                /* Now that we've received it, shrink our window */
                session->packAdd_channel->remote.window_size -=
                    datalen - session->packAdd_data_head;
            }

            break;

        case SSH_MSG_CHANNEL_EOF:
            session->packAdd_channel =
                _libssh2_channel_locate(session, _libssh2_ntohu32(data + 1));

            if (!session->packAdd_channel) {
                /* We may have freed already, just quietly ignore this... */
                LIBSSH2_FREE(session, data);
                session->packAdd_state = libssh2_NB_state_idle;
                return 0;
            }

            _libssh2_debug(session,
                           LIBSSH2_TRACE_CONN,
                           "EOF received for channel %lu/%lu",
                           session->packAdd_channel->local.id,
                           session->packAdd_channel->remote.id);
            session->packAdd_channel->remote.eof = 1;

            LIBSSH2_FREE(session, data);
            session->packAdd_state = libssh2_NB_state_idle;
            return 0;

        case SSH_MSG_CHANNEL_REQUEST:
        {
            uint32_t channel = _libssh2_ntohu32(data + 1);
            uint32_t strlen = _libssh2_ntohu32(data + 5);
            unsigned char want_reply = data[9 + strlen];

            _libssh2_debug(session,
                           LIBSSH2_TRACE_CONN,
                           "Channel %d received request type %.*s (wr %X)",
                           channel, strlen, data + 9, want_reply);

            if (strlen == sizeof("exit-status") - 1
                && !memcmp("exit-status", data + 9,
                           sizeof("exit-status") - 1)) {

                /* we've got "exit-status" packet. Set the session value */
                session->packAdd_channel =
                    _libssh2_channel_locate(session, channel);

                if (session->packAdd_channel) {
                    session->packAdd_channel->exit_status =
                        _libssh2_ntohu32(data + 9 + sizeof("exit-status"));
                    _libssh2_debug(session, LIBSSH2_TRACE_CONN,
                                   "Exit status %lu received for channel %lu/%lu",
                                   session->packAdd_channel->exit_status,
                                   session->packAdd_channel->local.id,
                                   session->packAdd_channel->remote.id);
                }

                LIBSSH2_FREE(session, data);
                session->packAdd_state = libssh2_NB_state_idle;
                return 0;
            }

            if (want_reply) {
              libssh2_packet_add_jump_point4:
                session->packAdd_state = libssh2_NB_state_jump4;
                data[0] = SSH_MSG_CHANNEL_FAILURE;
                rc = _libssh2_transport_write(session, data, 5);
                if (rc == LIBSSH2_ERROR_EAGAIN)
                    return rc;
                LIBSSH2_FREE(session, data);
                session->packAdd_state = libssh2_NB_state_idle;
                return 0;
            }
            break;
        }

        case SSH_MSG_CHANNEL_CLOSE:
            session->packAdd_channel =
                _libssh2_channel_locate(session, _libssh2_ntohu32(data + 1));

            if (!session->packAdd_channel) {
                /* We may have freed already, just quietly ignore this... */
                LIBSSH2_FREE(session, data);
                session->packAdd_state = libssh2_NB_state_idle;
                return 0;
            }
            _libssh2_debug(session, LIBSSH2_TRACE_CONN,
                           "Close received for channel %lu/%lu",
                           session->packAdd_channel->local.id,
                           session->packAdd_channel->remote.id);

            session->packAdd_channel->remote.close = 1;
            session->packAdd_channel->remote.eof = 1;

            LIBSSH2_FREE(session, data);
            session->packAdd_state = libssh2_NB_state_idle;
            return 0;

        case SSH_MSG_CHANNEL_OPEN:
            if ((datalen >= (sizeof("forwarded-tcpip") + 4)) &&
                ((sizeof("forwarded-tcpip") - 1) == _libssh2_ntohu32(data + 1))
                &&
                (memcmp(data + 5, "forwarded-tcpip",
                        sizeof("forwarded-tcpip") - 1) == 0)) {

              libssh2_packet_add_jump_point2:
                session->packAdd_state = libssh2_NB_state_jump2;
                rc = packet_queue_listener(session, data, datalen,
                                           &session->packAdd_Qlstn_state);
                if (rc == LIBSSH2_ERROR_EAGAIN)
                    return rc;

                LIBSSH2_FREE(session, data);
                session->packAdd_state = libssh2_NB_state_idle;
                return rc;
            }
            if ((datalen >= (sizeof("x11") + 4)) &&
                ((sizeof("x11") - 1) == _libssh2_ntohu32(data + 1)) &&
                (memcmp(data + 5, "x11", sizeof("x11") - 1) == 0)) {

              libssh2_packet_add_jump_point3:
                session->packAdd_state = libssh2_NB_state_jump3;
                rc = packet_x11_open(session, data, datalen,
                                     &session->packAdd_x11open_state);
                if (rc == LIBSSH2_ERROR_EAGAIN)
                    return rc;

                LIBSSH2_FREE(session, data);
                session->packAdd_state = libssh2_NB_state_idle;
                return rc;
            }
            break;

        case SSH_MSG_CHANNEL_WINDOW_ADJUST:
        {
            unsigned long bytestoadd = _libssh2_ntohu32(data + 5);
            session->packAdd_channel =
                _libssh2_channel_locate(session,
                                        _libssh2_ntohu32(data + 1));

            if (session->packAdd_channel && bytestoadd) {
                session->packAdd_channel->local.window_size += bytestoadd;
            }
            if(session->packAdd_channel)
                _libssh2_debug(session, LIBSSH2_TRACE_CONN,
                               "Window adjust received for channel %lu/%lu, adding %lu bytes, new window_size=%lu",
                               session->packAdd_channel->local.id,
                               session->packAdd_channel->remote.id,
                               bytestoadd,
                               session->packAdd_channel->local.window_size);
            else
                _libssh2_debug(session, LIBSSH2_TRACE_CONN,
                               "Window adjust for non-existing channel!");

            LIBSSH2_FREE(session, data);
            session->packAdd_state = libssh2_NB_state_idle;
            return 0;
        }
        break;
        }

        session->packAdd_state = libssh2_NB_state_sent;
    }

    if (session->packAdd_state == libssh2_NB_state_sent) {
        LIBSSH2_PACKET *packAdd_packet;
        packAdd_packet =
            LIBSSH2_ALLOC(session, sizeof(LIBSSH2_PACKET));
        if (!packAdd_packet) {
            _libssh2_debug(session, LIBSSH2_ERROR_ALLOC,
                           "Unable to allocate memory for LIBSSH2_PACKET");
            session->packAdd_state = libssh2_NB_state_idle;
            return -1;
        }
        memset(packAdd_packet, 0, sizeof(LIBSSH2_PACKET));

        packAdd_packet->data = data;
        packAdd_packet->data_len = datalen;
        packAdd_packet->data_head = session->packAdd_data_head;
        packAdd_packet->mac = macstate;

        _libssh2_list_add(&session->packets, &packAdd_packet->node);

        session->packAdd_state = libssh2_NB_state_sent1;
    }

    if ((data[0] == SSH_MSG_KEXINIT &&
         !(session->state & LIBSSH2_STATE_EXCHANGING_KEYS)) ||
        (session->packAdd_state == libssh2_NB_state_sent2)) {
        if (session->packAdd_state == libssh2_NB_state_sent1) {
            /*
             * Remote wants new keys
             * Well, it's already in the brigade,
             * let's just call back into ourselves
             */
            _libssh2_debug(session, LIBSSH2_TRACE_TRANS, "Renegotiating Keys");

            session->packAdd_state = libssh2_NB_state_sent2;
        }

        /*
         * The KEXINIT message has been added to the queue.  The packAdd and
         * readPack states need to be reset because _libssh2_kex_exchange
         * (eventually) calls upon _libssh2_transport_read to read the rest of
         * the key exchange conversation.
         */
        session->readPack_state = libssh2_NB_state_idle;
        session->packet.total_num = 0;
        session->packAdd_state = libssh2_NB_state_idle;
        session->fullpacket_state = libssh2_NB_state_idle;

        /*
         * Also, don't use packAdd_key_state for key re-exchange,
         * as it will be wiped out in the middle of the exchange.
         * How about re-using the startup_key_state?
         */
        memset(&session->startup_key_state, 0, sizeof(key_exchange_state_t));

        /*
         * If there was a key reexchange failure, let's just hope we didn't
         * send NEWKEYS yet, otherwise remote will drop us like a rock
         */
        rc = _libssh2_kex_exchange(session, 1, &session->startup_key_state);
        if (rc == LIBSSH2_ERROR_EAGAIN)
            return rc;
    }

    session->packAdd_state = libssh2_NB_state_idle;
    return 0;
}

/*
 * _libssh2_packet_ask
 *
 * Scan the brigade for a matching packet type, optionally poll the socket for
 * a packet first
 */
int
_libssh2_packet_ask(LIBSSH2_SESSION * session, unsigned char packet_type,
                    unsigned char **data, size_t *data_len,
                    int match_ofs, const unsigned char *match_buf,
                    size_t match_len)
{
    LIBSSH2_PACKET *packet = _libssh2_list_first(&session->packets);

    _libssh2_debug(session, LIBSSH2_TRACE_TRANS,
                   "Looking for packet of type: %d", (int) packet_type);

    while (packet) {
        if (packet->data[0] == packet_type
            && (packet->data_len >= (match_ofs + match_len))
            && (!match_buf ||
                (memcmp(packet->data + match_ofs, match_buf,
                        match_len) == 0))) {
            *data = packet->data;
            *data_len = packet->data_len;

            /* unlink struct from session->packets */
            _libssh2_list_remove(&packet->node);

            LIBSSH2_FREE(session, packet);

            return 0;
        }
        packet = _libssh2_list_next(&packet->node);
    }
    return -1;
}

/*
 * libssh2_packet_askv
 *
 * Scan for any of a list of packet types in the brigade, optionally poll the
 * socket for a packet first
 */
int
_libssh2_packet_askv(LIBSSH2_SESSION * session,
                     const unsigned char *packet_types,
                     unsigned char **data, size_t *data_len,
                     int match_ofs,
                     const unsigned char *match_buf,
                     size_t match_len)
{
    int i, packet_types_len = strlen((char *) packet_types);

    for(i = 0; i < packet_types_len; i++) {
        if (0 == _libssh2_packet_ask(session, packet_types[i], data,
                                     data_len, match_ofs,
                                     match_buf, match_len)) {
            return 0;
        }
    }

    return -1;
}

/*
 * _libssh2_packet_require
 *
 * Loops _libssh2_transport_read() until the packet requested is available
 * SSH_DISCONNECT or a SOCKET_DISCONNECTED will cause a bailout
 *
 * Returns negative on error
 * Returns 0 when it has taken care of the requested packet.
 */
int
_libssh2_packet_require(LIBSSH2_SESSION * session, unsigned char packet_type,
                        unsigned char **data, size_t *data_len,
                        int match_ofs,
                        const unsigned char *match_buf,
                        size_t match_len,
                        packet_require_state_t *state)
{
    if (state->start == 0) {
        if (_libssh2_packet_ask(session, packet_type, data, data_len,
                                match_ofs, match_buf,
                                match_len) == 0) {
            /* A packet was available in the packet brigade */
            return 0;
        }

        state->start = time(NULL);
    }

    while (session->socket_state == LIBSSH2_SOCKET_CONNECTED) {
        int ret = _libssh2_transport_read(session);
        if (ret == LIBSSH2_ERROR_EAGAIN)
            return ret;
        else if (ret < 0) {
            state->start = 0;
            /* an error which is not just because of blocking */
            return ret;
        } else if (ret == packet_type) {
            /* Be lazy, let packet_ask pull it out of the brigade */
            ret = _libssh2_packet_ask(session, packet_type, data, data_len,
                                      match_ofs, match_buf, match_len);
            state->start = 0;
            return ret;
        } else if (ret == 0) {
            /* nothing available, wait until data arrives or we time out */
            long left = LIBSSH2_READ_TIMEOUT - (long)(time(NULL) - state->start);

            if (left <= 0) {
                state->start = 0;
                return LIBSSH2_ERROR_TIMEOUT;
            }
            return -1; /* no packet available yet */
        }
    }

    /* Only reached if the socket died */
    return LIBSSH2_ERROR_SOCKET_DISCONNECT;
}

/*
 * _libssh2_packet_burn
 *
 * Loops _libssh2_transport_read() until any packet is available and promptly
 * discards it.
 * Used during KEX exchange to discard badly guessed KEX_INIT packets
 */
int
_libssh2_packet_burn(LIBSSH2_SESSION * session,
                     libssh2_nonblocking_states * state)
{
    unsigned char *data;
    size_t data_len;
    unsigned char all_packets[255];
    int i;
    int ret;

    if (*state == libssh2_NB_state_idle) {
        for(i = 1; i < 256; i++) {
            all_packets[i - 1] = i;
        }

        if (_libssh2_packet_askv(session, all_packets, &data, &data_len, 0,
                                 NULL, 0) == 0) {
            i = data[0];
            /* A packet was available in the packet brigade, burn it */
            LIBSSH2_FREE(session, data);
            return i;
        }

        _libssh2_debug(session, LIBSSH2_TRACE_TRANS,
                       "Blocking until packet becomes available to burn");
        *state = libssh2_NB_state_created;
    }

    while (session->socket_state == LIBSSH2_SOCKET_CONNECTED) {
        ret = _libssh2_transport_read(session);
        if (ret == LIBSSH2_ERROR_EAGAIN) {
            return ret;
        } else if (ret < 0) {
            *state = libssh2_NB_state_idle;
            return ret;
        } else if (ret == 0) {
            /* FIXME: this might busyloop */
            continue;
        }

        /* Be lazy, let packet_ask pull it out of the brigade */
        if (0 ==
            _libssh2_packet_ask(session, ret, &data, &data_len, 0, NULL, 0)) {
            /* Smoke 'em if you got 'em */
            LIBSSH2_FREE(session, data);
            *state = libssh2_NB_state_idle;
            return ret;
        }
    }

    /* Only reached if the socket died */
    return LIBSSH2_ERROR_SOCKET_DISCONNECT;
}

/*
 * _libssh2_packet_requirev
 *
 * Loops _libssh2_transport_read() until one of a list of packet types
 * requested is available. SSH_DISCONNECT or a SOCKET_DISCONNECTED will cause
 * a bailout. packet_types is a null terminated list of packet_type numbers
 */

int
_libssh2_packet_requirev(LIBSSH2_SESSION *session,
                         const unsigned char *packet_types,
                         unsigned char **data, size_t *data_len,
                         int match_ofs,
                         const unsigned char *match_buf, size_t match_len,
                         packet_requirev_state_t * state)
{
    if (_libssh2_packet_askv(session, packet_types, data, data_len, match_ofs,
                             match_buf, match_len) == 0) {
        /* One of the packets listed was available in the packet brigade */
        state->start = 0;
        return 0;
    }

    if (state->start == 0) {
        state->start = time(NULL);
    }

    while (session->socket_state != LIBSSH2_SOCKET_DISCONNECTED) {
        int ret = _libssh2_transport_read(session);
        if ((ret < 0) && (ret != LIBSSH2_ERROR_EAGAIN)) {
            state->start = 0;
            return ret;
        }
        if (ret <= 0) {
            long left = LIBSSH2_READ_TIMEOUT -
                (long)(time(NULL) - state->start);

            if (left <= 0) {
                state->start = 0;
                return LIBSSH2_ERROR_TIMEOUT;
            }
            else if (ret == LIBSSH2_ERROR_EAGAIN) {
                return ret;
            }
        }

        if (strchr((char *) packet_types, ret)) {
            /* Be lazy, let packet_ask pull it out of the brigade */
            return _libssh2_packet_askv(session, packet_types, data,
                                        data_len, match_ofs, match_buf,
                                        match_len);
        }
    }

    /* Only reached if the socket died */
    state->start = 0;
    return LIBSSH2_ERROR_SOCKET_DISCONNECT;
}
