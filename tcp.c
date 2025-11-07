#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#include "tcp.h"

int lookup(char *host, char *portnr, struct addrinfo **res)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV;
    hints.ai_protocol = 0;

    return getaddrinfo(host, portnr, &hints, res);
}

int connect_to(struct addrinfo *addr, struct timeval *rtt, int timeout_sec)
{
    int fd = -1;
    struct timeval start;
    int connect_result;
    //const int on = 1;

    /* int flags; */
    int flags;
    int saved_errno = ENOENT;

    fd_set write_fds;
    struct timeval timeout;

    /* try to connect for each of the entries: */
    while (addr != NULL)
    {
        /* create socket */
        if ((fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol)) == -1)
        {
            saved_errno = errno;
            goto next_addr0;
        }
        //deleted
        //if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
        //    saved_errno = errno;
        //    goto next_addr1;

        // Set socket to non-blocking
        if ((flags = fcntl(fd, F_GETFL, 0)) == -1)
        {
            saved_errno = errno;
            goto next_addr1;
        }

        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
        {
            saved_errno = errno;
            goto next_addr1;
        }


        if (gettimeofday(&start, NULL) == -1)
        {
            saved_errno = errno;
            goto next_addr1;
        }


        /* connect to peer */
        connect_result = connect(fd, addr->ai_addr, addr->ai_addrlen);
        if (connect_result == 0)
        {
            goto success;
        }
        
        if(errno != EINPROGRESS){
            saved_errno = errno;//save errno immediately
            goto next_addr1;
        }


        // Wait for socket to become writable (connection completed or failed)
        FD_ZERO(&write_fds);
        FD_SET(fd, &write_fds);
        timeout.tv_sec = timeout_sec;
        timeout.tv_usec = 0;

        int select_result = select(fd + 1, NULL, &write_fds, NULL, &timeout);
        if (select_result <= 0) {
            // Timeout or error
            saved_errno = (select_result == 0) ? ETIMEDOUT : errno;
            goto next_addr1;
        }

        // Check if connection actually succeeded
        socklen_t len = sizeof(saved_errno);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &saved_errno, &len) < 0) {
            saved_errno = errno;
            goto next_addr1;
        }

success:
            if (gettimeofday(rtt, NULL) == -1){
                saved_errno = errno;
                goto next_addr1;
            }

            rtt->tv_sec = rtt->tv_sec - start.tv_sec;
            rtt->tv_usec = rtt->tv_usec - start.tv_usec;
            if(rtt->tv_usec < 0)
            {
                rtt->tv_sec--;
                rtt->tv_usec += 1000000;
            }
            close(fd);
            return 0;
next_addr1:
        close(fd);
next_addr0:
        addr = addr->ai_next;
    }

    return -saved_errno;
}
