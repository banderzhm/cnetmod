/* External diagnostic only. Build/run later under the serial test window. */
#define _GNU_SOURCE
#include <sys/socket.h>
#include <linux/udp.h>
#include <netinet/in.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

static pthread_once_t init_once = PTHREAD_ONCE_INIT;
static int (*real_sendmmsg)(int, struct mmsghdr *, unsigned int, int);
static atomic_int fired;

static void initialize(void)
{
    *(void **)(&real_sendmmsg) = dlsym(RTLD_NEXT, "sendmmsg");
}

int sendmmsg(int fd, struct mmsghdr *messages, unsigned int count, int flags)
{
    pthread_once(&init_once, initialize);
    if (!real_sendmmsg) { errno = ENOSYS; return -1; }
    const char *mode = getenv("CNETMOD_GSO_FAULT");
    if (!mode || !*mode || atomic_load(&fired))
        return real_sendmmsg(fd, messages, count, flags);
    int grouped = 0, first_grouped = 0;
    unsigned int first_stride = 0;
    size_t first_bytes = 0;
    for (unsigned int i = 0; i < count; ++i)
        for (struct cmsghdr *c = CMSG_FIRSTHDR(&messages[i].msg_hdr); c;
             c = CMSG_NXTHDR(&messages[i].msg_hdr, c))
            if (c->cmsg_level == IPPROTO_UDP && c->cmsg_type == UDP_SEGMENT)
            {
                ++grouped;
                if (i == 0 && c->cmsg_len == CMSG_LEN(sizeof(unsigned short)))
                {
                    unsigned short stride;
                    memcpy(&stride, CMSG_DATA(c), sizeof(stride));
                    first_grouped = 1;
                    first_stride = stride;
                    for (size_t j = 0; j < messages[0].msg_hdr.msg_iovlen; ++j)
                        first_bytes += messages[0].msg_hdr.msg_iov[j].iov_len;
                }
            }
    int fault = !strcmp(mode, "EMSGSIZE") ? EMSGSIZE :
        !strcmp(mode, "ENOPROTOOPT") ? ENOPROTOOPT :
        !strcmp(mode, "EOPNOTSUPP") ? EOPNOTSUPP :
        !strcmp(mode, "EINVAL") ? EINVAL : !strcmp(mode, "EIO") ? EIO :
        !strcmp(mode, "EAGAIN") ? EAGAIN : 0;
    const int prefix = !strcmp(mode, "prefix");
    const int observe = !strcmp(mode, "observe");
    int expected = 0;
    if (!grouped || (!fault && !prefix && !observe) ||
        (prefix && (count < 2 || !first_grouped)) ||
        !atomic_compare_exchange_strong(&fired, &expected, 1))
        return real_sendmmsg(fd, messages, count, flags);
    fprintf(stderr, "CNETMOD_GSO_FAULT fired mode=%s kernel_messages=%u gso_messages=%d first_stride=%u first_iovs=%zu first_bytes=%zu\n",
        mode, count, grouped, first_stride, messages[0].msg_hdr.msg_iovlen, first_bytes);
    if (prefix || observe)
    {
        const int result = real_sendmmsg(fd, messages, prefix ? 1 : count, flags);
        const int saved_errno = errno;
        fprintf(stderr, "CNETMOD_GSO_REAL result=%d errno=%d mode=%s first_gso=%d\n",
            result, result < 0 ? saved_errno : 0, mode, first_grouped);
        errno = saved_errno;
        return result;
    }
    errno = fault;
    return -1;
}
