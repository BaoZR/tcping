#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <float.h>
#include <sys/time.h>
#include <math.h>
#include <time.h>
#include "tcp.h"

static volatile int stop = 0;

void usage(void)
{
    fprintf(stderr, "tcping, (C) 2003 folkert@vanheusden.com\n\n");
    fprintf(stderr, "hostname	hostname (e.g. localhost)\n");
    fprintf(stderr, "-p portnr	portnumber (e.g. 80)\n");
    fprintf(stderr, "-c count	how many times to connect\n");
    fprintf(stderr, "-i interval	delay between each connect (supports decimals, e.g. 0.5 for 500ms)\n");
    fprintf(stderr, "-f	flood connect (no delays)\n");
    fprintf(stderr, "-q	quiet, only returncode\n\n");
    fprintf(stderr, "-d	time duration in seconds (e.g. 10 for 10s)\n\n\n\n");
}

void handler(int sig)
{
    stop = 1;
    if(sig == 2){
        exit(1);
    }
}

// Calculate two time differences (seconds)
double time_diff(struct timeval *start, struct timeval *end) {
    return (end->tv_sec - start->tv_sec) + 
           (end->tv_usec - start->tv_usec) / 1000000.0;
}


int validate_arguments(const char *hostname, int count, double interval, 
                       int duration, int duration_flag) {
    // 检查 hostname 是否提供
    if (hostname == NULL || strlen(hostname) == 0) {
        fprintf(stderr, "No hostname given\n");
        return 3;
    }

    // 检查 count 是否为 0（count 为 -1 表示无限次，合法）
    if (count == 0) {
        fprintf(stderr, "count cannot be zero\n");
        return 3;
    }

    // 检查 interval（间隔时间）是否为负数
    if (interval < 0) {
        fprintf(stderr, "interval cannot be negative\n");
        return 3;
    }

    // 检查 duration（持续时间）是否合法（仅当指定了 -d 时）
    if (duration_flag && duration <= 0) {
        fprintf(stderr, "Timeout must be a positive number\n");
        return 3;
    }

    return 0; // 所有参数合法
}

int main(int argc, char *argv[])
{
    char *hostname = NULL;
    char *portnr = "80";
    int c;
    int count = -1, curncount = 0;
    int  quiet = 0, duration = -1;
    double wait = 0.5;
    int duration_flag = 0; 
    int ok = 0, err = 0;
    double min = DBL_MAX, avg = 0.0, max = 0.0;
    struct addrinfo *resolved;
    int errcode;
    int seen_addrnotavail = 0;
    struct timeval start_time,current_time;

    double prev_rtt = 0.0;
    double jitter_sum = 0.0; 

    while((c = getopt(argc, argv, "h:p:c:i:fq?d:")) != -1)
    {
        switch(c)
        {
            case 'p':
                portnr = optarg;
                break;

            case 'c':
                count = atoi(optarg);
                break;

            case 'i':
                wait = atof(optarg);
                break;

            case 'f':
                wait = 0;
                break;

            case 'q':
                quiet = 1;
                break;

            case 'd':
                duration = atoi(optarg);
                duration_flag = 1;
                break;
            case '?':
            default:
                usage();
                return 0;
        }
    }

    if (optind >= argc)
    {
        fprintf(stderr, "No hostname given\n");
        usage();
        return 3;
    }

    hostname = argv[optind];
    
    int validation_result = validate_arguments(hostname, count, wait, duration, duration_flag);
    if (validation_result != 0) {
        usage(); // 校验失败时打印用法
        return 3; // 统一返回错误码
    }



    signal(SIGINT, handler);
    signal(SIGTERM, handler);

    if ((errcode = lookup(hostname, portnr, &resolved)) != 0)
    {
        fprintf(stderr, "%s\n", gai_strerror(errcode));
        return 2;
    }

    if (!quiet)
        printf("PING %s:%s\n", hostname, portnr);

    gettimeofday(&start_time,NULL);

    while((curncount < count || count == -1) && stop == 0)
    {
        // check duration
        if(duration_flag > 0){
            gettimeofday(&current_time,NULL);
            if(time_diff(&start_time,&current_time) >= duration){
                stop = 1;
                break;
            }
        }

        double ms;
        struct timeval rtt;

        if ((errcode = connect_to(resolved, &rtt, 5)) != 0)
        {
            if (errcode != -EADDRNOTAVAIL)
            {
                printf("error connecting to host (%d): %s\n", -errcode, strerror(-errcode));
                err++;
            }
            else
            {
                if (seen_addrnotavail)
                {
                    printf(".");
                    fflush(stdout);
                }
                else
                {
                    printf("error connecting to host (%d): %s\n", -errcode, strerror(-errcode));
                }
                seen_addrnotavail = 1;
            }
        }
        else
        {
            seen_addrnotavail = 0;
            ok++;

            ms = ((double)rtt.tv_sec * 1000.0) + ((double)rtt.tv_usec / 1000.0);
            avg += ms;
            min = min > ms ? ms : min;
            max = max < ms ? ms : max;
            
            if(ok > 1){
                double current_jitter = fabs(ms - prev_rtt);
                jitter_sum += current_jitter;
            }
            prev_rtt = ms;

            printf("attempts from %s:%s, seq=%d time=%.2f ms\n", hostname, portnr, curncount, ms);
            //if (ms > 500) break; /* Stop the test on the first long connect() */
        }

        curncount++;

        if (curncount != count ){
            struct timespec ts;
            ts.tv_sec = (time_t)wait;                  // 秒部分
            ts.tv_nsec = (long)((wait - ts.tv_sec) * 1e9);  // 纳秒部分
            nanosleep(&ts, NULL);  // 忽略剩余时间
        }

    }

    if (!quiet)
    {
        int total_attempts = curncount;
        double fail_percent = (total_attempts > 0) ? (100.0 * err / total_attempts) : 0.0;
       
        printf("--- %s:%s ping statistics ---\n", hostname, portnr);
        printf("%d attempts, %d ok, %3.2f%% failed\n", curncount, ok, fail_percent);

        if (ok > 0) {
            double avg_rtt = avg / ok;
            printf("round-trip min/avg/max = %.1f/%.1f/%.1f ms\n", min, avg_rtt, max);
            if(ok > 1){
                double avg_jitter = jitter_sum / (ok - 1);  // n次成功连接有n-1次抖动
                printf("jitter = %.2f ms\n", avg_jitter);
            }
        } else {
            printf("No successful connections.\n");
        }
    }

    freeaddrinfo(resolved);
    if (ok)
        return 0;
    else
        return 127;
}
