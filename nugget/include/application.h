/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef __CROS_EC_INCLUDE_APPLICATION_H
#define __CROS_EC_INCLUDE_APPLICATION_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __packed
#define __packed __attribute__((packed))
#endif

typedef const void * const __private;

/*
 * Typical applications are independent tasks which are directed (or at least
 * influenced) by some off-chip program. Communications with the applications
 * are initiated by that off-chip Master and are routed to the application
 * using a variety of methods.
 */

/****************************************************************************/
/*
 * Datagram API:
 *
 * Nugget OS abstracts the bus protocol (SPI, USB, whatever) into two
 * unidirectional "datagram" transactions:
 *
 * - Read (the master wants data from the application)
 * - Write (the master sends data to the application)
 *
 * Each transaction consists of a four-byte Command from the Master, plus zero
 * or more data bytes either to (Read) or from (Write) the Master.
 *
 * The Command indicates the direction of data transfer, the application it
 * addresses, and various other parameters. The application is responsible for
 * providing (Read) or accepting (Write) the data bytes.
 *
 * Note: This interface was first used on the SPI bus, which allows for
 * simultaneous bidirectional data transfer. We limit this interface to
 * unidirectional transfers, because none of the other buses support that
 * feature.
 */

/****************************************************************************/
/* Application IDs */

/* These two App IDs shouldn't be changed or used for other purposes */
#define APP_ID_NUGGET            0x00    /* because we're selfish */
#define APP_ID_TPM_REGISTER_API  0xD4    /* mandated by the TCG */
/*
 * Other App IDs are defined here. It will help avoid confusion if you use only
 * the values from here and don't change them once they're set. But it's up to
 * you. I'm a comment, not a cop.
 */
#define APP_ID_AVB               0x01
#define APP_ID_KEYMASTER         0x02
#define APP_ID_WEAVER            0x03
#define APP_ID_PROTOBUF          0x04
#define APP_ID_IDENTITY          0x05
#define APP_ID_GSC_FACEAUTH      0x06

/* Fake apps used only for testing */
#define APP_ID_AVB_TEST          0x11
#define APP_ID_TRANSPORT_TEST    0x12
#define APP_ID_FACEAUTH_TEST     0x13
#define APP_ID_TEST              0x7f

/* OR this with the APP_ID to request no-protobuf messages */
#define APP_ID_NO_PROTO_FLAG     0x80

/* No-protobuf app, experimental for now */
#define APP_ID_WEAVER2           (APP_ID_WEAVER | APP_ID_NO_PROTO_FLAG)

/****************************************************************************/
/* Other command fields */

/*
 * The Command encoding is:
 *
 *   Bits 31-24   Control flags (reserved)
 *   Bits 23-16   Application ID (bit 23 indicates C protocol, not protobuf)
 *   Bits 15-0    Parameters (application-specific)
 */

/* Control flag bits */
#define CMD_IS_READ       0x80000000    /* 1=Read, 0=Write */
/* All other control flags bits are reserved */

/* Extracting fields from a command */
#define GET_APP_ID(cmd)     (((cmd) & 0x00ff0000) >> 16)
#define GET_APP_PARAM(cmd)  ((cmd) & 0x0000ffff)

/* Specifying command fields */
#define CMD_ID(id)       (((id) & 0x000000ff) << 16)
#define CMD_PARAM(p)     ((p) & 0x0000ffff)
#define CMD_SET_PARAM(cmd, p) cmd = ((cmd & 0xffff0000) | (p & 0x0000ffff))

/****************************************************************************/
/* Data transfer */

/*
 * Functions of this type are invoked when the Master wants to read bytes from
 * an application. The app should parse the Command, copy up to max_tx_size
 * bytes into the tx_buffer provided, and return the number of bytes to send
 * back to the Master.
 *
 * This is called in interrupt context, so act quickly.
 *
 * The last arg is for internal use. Just ignore it.
 */
typedef uint32_t (read_from_app_fn_t)(uint32_t command,
                                      uint8_t *tx_buffer,
                                      uint32_t max_tx_bytes,
                                      __private priv);

/*
 * Functions of this type are invoked when the Master has sent bytes to the
 * application. The app should parse the Command and copy or process the
 * expected number of bytes in the rx_buffer that the master has sent, up to
 * rx_num_bytes.
 *
 * NOTE: Due to a quirk of the Citadel hardware, up to four extra bytes from
 * the *next* transaction may be at the end of the rx_buffer. The application
 * should only poke at the bytes it expects to see and ignore any extras.
 *
 * This is called in interrupt context, so act quickly.
 *
 * The last arg is for internal use. Just ignore it.
 */
typedef void (write_to_app_fn_t)(uint32_t command,
                                 const uint8_t *rx_buffer,
                                 uint32_t num_rx_bytes,
                                 __private priv);

/*
 * For apps that run asynchronously with little oversight, occasional
 * Read/Write operations may be all that's necessary. An app that intercepts
 * button presses, an accelerometer, or a fingerprint scanner can simply be
 * told to start or stop and will send interrupts to the Master when its
 * attention is required.
 *
 * Applications are free to define their own protcols and APIs using only the
 * functions and constants above (and at least one app does just that).
 *
 * An app that wishes to handle its messaging using only the components
 * described to this point would use the following macro to declare itself.
 */

/**
 * This registers an application that communicates using the Datagram API,
 * which deals only with the raw byte streams between Master (AP) and Slave
 * (application).
 *
 * The name and version values may be exported to the Master by Nugget OS, so
 * the Master can query what applications are available without blindly trying
 * them all to see what works.
 *
 * @param  Id        The Application ID, defined above
 * @param  Name      A human-readable string identifying the application
 * @param  Version   An app-specific uint32_t number, for compability purposes
 * @param  From_fn   A pointer to the app's read_from_app_fn_t handler
 * @param  To_fn     A pointer to the app's write_to_app_fn_t handler
 * @param  Data      App's private data
 */
#define DECLARE_APPLICATION_DATAGRAM(Id, Name, Version, From_fn, To_fn, Data) \
  const struct app_info __keep CONCAT2(app_, Id)                        \
    __attribute__((section(".rodata.app_info")))                        \
    = { .api = { .id = Id,                                              \
                 .from_fn = From_fn, .to_fn = To_fn,                    \
                 .data = Data},                                         \
        .version = Version, .name = Name }

/****************************************************************************/
/* Transport API */
/*
 * Rather than handle unidirectonal datagrams themselves, many applications
 * want to implement a request/response behavior, where the Master tells the
 * app to do something and waits for it to finish and return the result.
 *
 * Seen from the AP's side, the application would be invoked using a blocking
 * function something like this:
 *
 *   uint32_t call_application(uint8_t app_id, uint16_t app_param,
 *                             const uint8_t *args, uint16_t arg_len,
 *                             uint8_t *reply, uint16_t *reply_len);
 *
 * The request or response may be larger than one bus transaction, and the AP
 * may poll until the app finishes or wait for an interrupt before retrieving
 * the reply (there's no difference from app's point of view).
 *
 * With this API, the application is initially idle. Nugget OS will marshall
 * all the input from the Master before waking the application. The Application
 * then performs the requested operation and transititions to a "done" state.
 * The Master will retrieve the application status and any reply data from
 * Nugget OS, after which the application is ready to handle the next command.
 */

#define TRANSPORT_V0    0x0000
#define TRANSPORT_V1    0x0001

/* Command information for the transport protocol. */
struct transport_command_info {
  /* v1 fields */
  uint16_t length;           /* length of this message */
  uint16_t version;          /* max version used by master */
  uint16_t crc;              /* CRC of some command fields */
  uint16_t reply_len_hint;   /* max that the master will read */
} __packed;

#define COMMAND_INFO_MIN_LENGTH 8
#define COMMAND_INFO_MAX_LENGTH 32
/* If more data needs to be sent, chain a new struct to the end of this one. It
 * will require its own CRC for data integrity and something to signify the
 * presence of the extra data. */

struct transport_status {
  /* v0 fields */
  uint32_t status;         /* status of the app */
  uint16_t reply_len;      /* length of available response data */
  /* v1 fields */
  uint16_t length;         /* length of this message */
  uint16_t version;        /* max version used by slave */
  uint16_t flags;          /* space for more protocol state flags */
  uint16_t crc;            /* CRC of this status with crc set to 0 */
  uint16_t reply_crc;      /* CRC of the reply data */
} __packed;

/* Valid range of lengths for the status message */
#define STATUS_MIN_LENGTH 0x10
#define STATUS_MAX_LENGTH (sizeof(struct transport_status)) /* 0x10 */

/* Flags used in the status message */
#define STATUS_FLAG_WORKING 0x0001 /* added in v1 */

/* Pre-calculated CRCs for different status responses set in the interrupt
 * context where the CRC would otherwise not be calculated. */
#define STATUS_CRC_FOR_IDLE              0x54c1
#define STATUS_CRC_FOR_WORKING           0x2101
#define STATUS_CRC_FOR_ERROR_TOO_MUCH    0x97c0

/*
 * Applications that wish to use this transport API will need to declare a
 * private struct app_transport which Nugget OS can use to maintain the state:
 */
struct app_transport {
  void (*done_fn)(struct app_transport *);    /* optional cleanup function */
  /* Note: Any done_fn() is called in interrupt context. Be quick. */
  uint8_t *const request;                     /* input data buffer */
  uint8_t *const response;                    /* output data buffer */
  const uint16_t max_request_len;             /* input data buffer size */
  const uint16_t max_response_len;            /* output data buffer size */
  /* The following are used for the incoming command. */
  uint32_t command;                           /* from master */
  union {
    struct transport_command_info info;
    uint8_t data[COMMAND_INFO_MAX_LENGTH];    /* space for future growth */
  } command_info;                             /* extra info about the command */
  uint16_t request_len;                       /* command data buffer size */
  uint16_t response_idx;                      /* current index into response */
  struct transport_status status[2];          /* current transport_status */
  volatile uint8_t status_idx;                /* index of active status */
};

/*
 * Note that request and response buffers are transferred as byte streams.
 * However, if they will eventually represent structs, the usual ABI alignment
 * requirements will be required. Until we've declared all applications structs
 * in a union, we will need to align the buffers manually. Use this to declare
 * the uint8_t buffers until then:
 */
#define __TRANSPORT_ALIGNED__ __attribute__((aligned(8)))

/*
 * The application will need to provide a write_to_app_fn_t function that will
 * be invoked when a new request is ready to be processed. All command and data
 * parameters will already be present in the app's struct app_transport, so it
 * just needs to awaken the application task to do the work.
 *
 * When awakened, the application task must check that there were no errors in
 * the transmission of the request by calling this function. If it returns
 * true, the task should go back to sleep until the next request arrives.
 */
int request_is_invalid(struct app_transport *s);
/*
 * When processing is finished, the app should call the app_reply() function to
 * return its status code and specify the length of any data it has placed into
 * the response buffer, and then it can go back to sleep until its next
 * invocation. CAUTION: The Master polls for app completion independently, so
 * it may immediately begin retrieving the results as soon as this function
 * is called *without* waiting for the Nugget OS app to go to sleep.
 */
void app_reply(struct app_transport *st, uint32_t status, uint16_t reply_len);

/* Application status codes are uint32_t, but an enum is easier to read. */
enum app_status {
  /* A few values are common to all applications */
  APP_SUCCESS = 0,
  APP_ERROR_BOGUS_ARGS, /* caller being stupid */
  APP_ERROR_INTERNAL,   /* application being stupid */
  APP_ERROR_TOO_MUCH,   /* caller sent too much data */
  APP_ERROR_IO,         /* problem sending or receiving data */
  APP_ERROR_RPC,        /* problem during RPC communication */
  APP_ERROR_CHECKSUM,   /* checksum failed, only used within protocol */
  APP_ERROR_BUSY,       /* the app is already working on a commnad */
  APP_ERROR_TIMEOUT,    /* the app took too long to respond */
  APP_ERROR_NOT_READY,  /* some required condition is not satisfied */
  /* more? */

  /*
   * Applications can define their own app-specific error codes.  For example,
   * app_foobar.h can do:
   *
   *	#define APP_ERROR_FOOBAR_BAZ (APP_SPECIFIC_ERROR + 0)
   *
   * Do not use (APP_SPECIFIC_ERROR + N) directly in your code, because the
   * error definition, firmware which generates it, and host code which
   * interprets it are all in different repos.  You'll never be able to keep
   * the constants straight without using a #define or enum in your app's
   * header file that everyone can share.
   */
  APP_SPECIFIC_ERROR = 0x20, /* "should be enough for anybody" */

  /* For debugging, returning a line number might be helpful */
  APP_LINE_NUMBER_BASE = 0x70000000,
#define APP_ERROR_LINENO (APP_LINE_NUMBER_BASE + __LINE__)

  /* Bit 31 is reserved for internal use */
  MAX_APP_STATUS = 0x7fffffff,
};

/**
 * This registers an application that communicates using the Transport API.
 *
 * The name and version values may be exported to the Master by Nugget OS, so
 * the Master can query what applications are available without blindly trying
 * them all to see what works.
 *
 * @param  Id        The Application ID, defined above
 * @param  Name      A human-readable string identifying the application
 * @param  Version   An app-specific uint32_t number, for compability purposes
 * @param  State     A pointer to the app's struct app_transport
 * @param  To_fn     A pointer to the app's write_to_app_fn_t handler
 */
#define DECLARE_APPLICATION_TRANSPORT(Id, Name, Version, State, To_fn)  \
    const struct app_info __keep CONCAT2(app_, Id)                      \
      __attribute__((section(".rodata.app_info")))                      \
      = { .api = { .id = Id,                                            \
             .from_fn = transaction_api_from_fn,                        \
             .to_fn = transaction_api_to_fn,                            \
             .data = &(const struct datagram_api)                       \
             { .id = Id, .to_fn = To_fn,                                \
               .data = State } },                                       \
          .version = Version, .name = Name }

/****************************************************************************/
/* Pay no attention to that man behind the curtain */

/* We'll allow 31 bits of application status. We need one bit for transport. */
#define APP_STATUS_IDLE     0x00000000    /* waiting for instructions */
#define APP_STATUS_DONE     0x80000000    /* finished, reply is ready */
#define APP_STATUS_CODE(res) ((res) & 0x7fffffff) /* actual status */

/* Datagram API needs this info */
struct datagram_api {
  uint8_t id;
  read_from_app_fn_t * const from_fn;
  write_to_app_fn_t * const to_fn;
  const void * const data;
};

/* Here's the struct that keeps track of registered applications */
struct app_info {
  struct datagram_api api;
  uint32_t version;
  const char * const name;
};

/* These handle the Transport API */
extern read_from_app_fn_t transaction_api_from_fn;
extern write_to_app_fn_t transaction_api_to_fn;

/* Command flags used internally by Transport API messages */
#define CMD_TRANSPORT       0x40000000    /* 1=Transport API message */
/* When CMD_TRANSPORT is set, the following bits have meaning */
#define CMD_IS_DATA         0x20000000    /* 1=data msg 0=status msg */
#define CMD_MORE_TO_COME    0x10000000    /* 1=continued 0=new */

#ifdef __cplusplus
}
#endif

#endif  /* __CROS_EC_INCLUDE_APPLICATION_H */
