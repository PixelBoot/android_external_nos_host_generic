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

#define LOG_TAG "libnos_datagram"
#include <log/log.h>
#include <nos/device.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/types.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <android-base/properties.h>
#include <application.h>

/*****************************************************************************/
/* Ideally, this should be in <linux/citadel.h> */
#define CITADEL_IOC_MAGIC               'c'
struct citadel_ioc_tpm_datagram {
    __u64        buf;
    __u32        len;
    __u32        command;
};

/* GSA nos call request struct */
struct gsa_ioc_nos_call_req {
    __u8 app_id;
    __u8 reserved;
    __u16 params;
    __u32 arg_len;
    __u64 buf;
    __u32 reply_len;
    __u32 call_status;
};

#define CITADEL_IOC_TPM_DATAGRAM    _IOW(CITADEL_IOC_MAGIC, 1, \
                         struct citadel_ioc_tpm_datagram)
#define CITADEL_IOC_RESET           _IO(CITADEL_IOC_MAGIC, 2)
#define GSC_IOC_GSA_NOS_CALL        _IOW(CITADEL_IOC_MAGIC, 3, \
                         struct gsa_ioc_nos_call_req)
/*****************************************************************************/

#define DEV_CITADEL   "/dev/citadel0"
#define DEV_DAUNTLESS "/dev/gsc0"

/* Allocate 4KB buffer for GSA mbox data transmission */
#define MAX_GSA_NOS_CALL_TRANSFER 4096
static uint8_t gsa_nos_call_buf[MAX_GSA_NOS_CALL_TRANSFER];
static pthread_mutex_t nos_call_buf_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t in_buf_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint8_t in_buf[MAX_DEVICE_TRANSFER];
static int read_datagram(void *ctx, uint32_t command, uint8_t *buf, uint32_t len) {
    struct citadel_ioc_tpm_datagram dg = {
        .buf = (unsigned long)in_buf,
        .len = len,
        .command = command,
    };
    int ret;
    int fd;

    if (!ctx) {

        ALOGE("%s: invalid (NULL) device\n", __func__);
        return -ENODEV;
    }
    fd = *(int *)ctx;
    if (fd < 0) {
        ALOGE("%s: invalid device\n", __func__);
        return -ENODEV;
    }

    if (len > MAX_DEVICE_TRANSFER) {
        ALOGE("%s: invalid len (%d > %d)\n", __func__,
            len, MAX_DEVICE_TRANSFER);
        return -E2BIG;
    }

    /* Lock the in buffer while it is used for this transaction */
    if (pthread_mutex_lock(&in_buf_mutex) != 0) {
        ALOGE("%s: failed to lock in_buf_mutex: %s", __func__, strerror(errno));
        return -errno;
    }

    ret = ioctl(fd, CITADEL_IOC_TPM_DATAGRAM, &dg);
    if (ret < 0) {
        ALOGE("can't send spi message: %s", strerror(errno));
        ret = -errno;
        goto out;
    }

    memcpy(buf, in_buf, len);

out:
    if (pthread_mutex_unlock(&in_buf_mutex) != 0) {
        ALOGE("%s: failed to unlock in_buf_mutex: %s", __func__, strerror(errno));
        ret = -errno;
    }
    return ret;
}

static pthread_mutex_t out_buf_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint8_t out_buf[MAX_DEVICE_TRANSFER];
static int write_datagram(void *ctx, uint32_t command, const uint8_t *buf, uint32_t len) {
    struct citadel_ioc_tpm_datagram dg = {
        .buf = (unsigned long)out_buf,
        .len = len,
        .command = command,
    };
    int ret;
    int fd;

    if (!ctx) {
        ALOGE("%s: invalid (NULL) device\n", __func__);
        return -ENODEV;
    }
    fd = *(int *)ctx;
    if (fd < 0) {
        ALOGE("%s: invalid device\n", __func__);
        return -ENODEV;
    }

    if (len > MAX_DEVICE_TRANSFER) {
        ALOGE("%s: invalid len (%d > %d)\n", __func__, len,
            MAX_DEVICE_TRANSFER);
        return -E2BIG;
    }

    /* Lock the out buffer while it is used for this transaction */
    if (pthread_mutex_lock(&out_buf_mutex) != 0) {
        ALOGE("%s: failed to lock out_buf_mutex: %s", __func__, strerror(errno));
        return -errno;
    }

    memcpy(out_buf, buf, len);

    ret = ioctl(fd, CITADEL_IOC_TPM_DATAGRAM, &dg);
    if (ret < 0) {
        ALOGE("can't send spi message: %s", strerror(errno));
        ret = -errno;
        goto out;
    }

out:
    if (pthread_mutex_unlock(&out_buf_mutex) != 0) {
        ALOGE("%s: failed to unlock out_buf_mutex: %s", __func__, strerror(errno));
        ret = -errno;
    }
    return ret;
}

static int wait_for_interrupt(void *ctx, int msecs) {
    int fd = *(int *)ctx;
    struct pollfd fds = {fd, POLLIN, 0};
    int rv;

    rv = poll(&fds, 1 /*nfds*/, msecs);
    if (rv < 0) {
        ALOGE("poll: %s", strerror(errno));
    }

    return rv;
}

static int reset(void *ctx) {
    int ret;
    int fd;

    if (!ctx) {

        ALOGE("%s: invalid (NULL) device\n", __func__);
        return -ENODEV;
    }
    fd = *(int *)ctx;
    if (fd < 0) {
        ALOGE("%s: invalid device\n", __func__);
        return -ENODEV;
    }

    ret = ioctl(fd, CITADEL_IOC_RESET);
    if (ret < 0) {
        ALOGE("can't reset Citadel: %s", strerror(errno));
        return -errno;
    }
    return 0;
}

static void close_device(void *ctx) {
    int fd;

    if (!ctx) {
        ALOGE("%s: invalid (NULL) device (ignored)\n", __func__);
        return;
    }
    fd = *(int *)ctx;
    if (fd < 0) {
        ALOGE("%s: invalid device (ignored)\n", __func__);
        return;
    }

    if (close(fd) < 0)
        ALOGE("Problem closing device (ignored): %s", strerror(errno));
    free(ctx);
}

/* Detect if GSA kernel support nos_call interface
 * Returns true on success or false on failure.
 */
static bool detect_gsa_nos_call_interface(int fd) {
  int ret;
  errno = 0;

  if (fd < 0) {
    ALOGE("invalid device handle (%d)", fd);
    return false;
  }

  /* Send app_id = 0 and params = 0 to detect GSA IOCTL interface */
  struct gsa_ioc_nos_call_req gsa_nos_call_req = {
      .app_id = 0,
      .reserved = 0,
      .params = 0,
      .arg_len = 0,
      .buf = (unsigned long)gsa_nos_call_buf,
      .reply_len = 0,
      .call_status = 0,
  };

  ret = ioctl(fd, GSC_IOC_GSA_NOS_CALL, &gsa_nos_call_req);
  if (ret < 0) {
      ALOGE("can't send GSA mbox command: %s", strerror(errno));
  }

  /* GSA kernel is not support GSA_NOS_CALL if return EINVAL or ENOTTY */
  if (!errno) {
    return true;
  } else {
    return false;
  }
}

static int one_pass_call(void *ctx, uint8_t app_id, uint16_t params,
                         const uint8_t *args, uint32_t arg_len,
                         uint8_t *reply, uint32_t *reply_len,
                         uint32_t *status_code) {
  *status_code = APP_SUCCESS;
  int ret;
  int fd;

  struct gsa_ioc_nos_call_req gsa_nos_call_req = {
      .app_id = app_id,
      .reserved = 0,
      .params = params,
      .arg_len = arg_len,
      .buf = (unsigned long)gsa_nos_call_buf,
      .reply_len = *reply_len,
      .call_status = *status_code,
  };

  ALOGD("Calling App 0x%02x with params 0x%04x", app_id, params);

  if (!ctx || (arg_len && !args) ||
      (reply_len && *reply_len && !reply) ||
      (arg_len > MAX_GSA_NOS_CALL_TRANSFER) ||
      (reply_len && *reply_len > MAX_GSA_NOS_CALL_TRANSFER) ||
      !status_code) {
    ALOGE("Invalid args to %s()", __func__);
    return -EINVAL;
  }

  fd = *(int *)ctx;
  if (fd < 0) {
      ALOGE("%s: invalid device\n", __func__);
      return -ENODEV;
  }

  /* Lock the out buffer while it is used for this transaction */
  if (pthread_mutex_lock(&nos_call_buf_mutex) != 0) {
      ALOGE("%s: failed to lock nos_call_buf_mutex: %s", __func__, strerror(errno));
      return -errno;
  }

  if (arg_len) {
      memcpy(gsa_nos_call_buf, args, arg_len);
  }

  ret = ioctl(fd, GSC_IOC_GSA_NOS_CALL, &gsa_nos_call_req);
  if (ret < 0) {
      ALOGE("can't send GSA mbox command: %s", strerror(errno));
      goto exit;
  }

  *status_code = gsa_nos_call_req.call_status;
  if (reply_len != NULL) {
      *reply_len = gsa_nos_call_req.reply_len;
      if (*reply_len) {
          memcpy(reply, gsa_nos_call_buf, *reply_len);
      }
  }

exit:
  if (pthread_mutex_unlock(&nos_call_buf_mutex) != 0) {
      ALOGE("%s: failed to unlock nos_call_buf_mutex: %s", __func__,
            strerror(errno));
      return -errno;
  }

  ALOGD("App 0x%02x returning 0x%x", app_id, *status_code);
  return ret;
}

static const char *default_device(void) {
    struct stat statbuf;
    int rv;

    rv = stat(DEV_CITADEL, &statbuf);
    if (!rv) {
        return DEV_CITADEL;
    }

    rv = stat(DEV_DAUNTLESS, &statbuf);
    if (!rv) {
        return DEV_DAUNTLESS;
    }

    return 0;
}

int nos_device_open(const char *device_name, struct nos_device *dev) {
    int fd, *new_ctx;

    if (!device_name) {
        device_name = default_device();
    }

    if (!device_name) {
      ALOGE("can't find device node\n");
      return -ENODEV;
    }

    fd = open(device_name, O_RDWR);
    if (fd < 0) {
        ALOGE("can't open device \"%s\": %s", device_name, strerror(errno));
        return -errno;
    }

    /* Our context is just a pointer to an int holding the fd */
    new_ctx = (int *)malloc(sizeof(int));
    if (!new_ctx) {
        ALOGE("can't malloc new ctx: %s", strerror(errno));
        close(fd);
        return -ENOMEM;
    }
    *new_ctx = fd;

    dev->ctx = new_ctx;
    dev->ops.read = read_datagram;
    dev->ops.write = write_datagram;
    dev->ops.wait_for_interrupt = wait_for_interrupt;
    dev->ops.reset = reset;
    dev->ops.close = close_device;
    dev->ops.one_pass_call = one_pass_call;
    dev->use_one_pass_call = detect_gsa_nos_call_interface(fd);
    return 0;
}
