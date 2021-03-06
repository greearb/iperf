/*
 * iperf, Copyright (c) 2014, 2015, 2017, 2019, The Regents of the University of
 * California, through Lawrence Berkeley National Laboratory (subject
 * to receipt of any required approvals from the U.S. Dept. of
 * Energy).  All rights reserved.
 *
 * If you have questions about your rights to use or distribute this
 * software, please contact Berkeley Lab's Technology Transfer
 * Department at TTD@lbl.gov.
 *
 * NOTICE.  This software is owned by the U.S. Department of Energy.
 * As such, the U.S. Government has been granted for itself and others
 * acting on its behalf a paid-up, nonexclusive, irrevocable,
 * worldwide license in the Software to reproduce, prepare derivative
 * works, and perform publicly and display publicly.  Beginning five
 * (5) years after the date permission to assert copyright is obtained
 * from the U.S. Department of Energy, and subject to any subsequent
 * five (5) year renewals, the U.S. Government is granted for itself
 * and others acting on its behalf a paid-up, nonexclusive,
 * irrevocable, worldwide license in the Software to reproduce,
 * prepare derivative works, distribute copies to the public, perform
 * publicly and display publicly, and to permit others to do so.
 *
 * This code is distributed under a BSD style license, see the LICENSE
 * file for complete information.
 */
#include "iperf_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#include <sys/types.h>

#include "iperf.h"
#include "iperf_api.h"
#include "units.h"
#include "iperf_locale.h"
#include "net.h"


static int run(struct iperf_test *test);


/**************************************************************************/
int
main(int argc, char **argv)
{
    struct iperf_test *test;

#ifdef __WIN32__
    WORD wVersionRequested;
    WSADATA wsaData;
    int err;
   
    wVersionRequested = MAKEWORD( 2, 0 );
    if ((err = WSAStartup( wVersionRequested, &wsaData )) != 0) {
       /* Tell the user that we could not find a usable */
       /* WinSock DLL.                                  */
       fprintf(stderr, "ERROR:  Could not load Winsock 2.0 DLLs, err: %d\n", err);
       return err;
    }
#endif

    // XXX: Setting the process affinity requires root on most systems.
    //      Is this a feature we really need?
#ifdef TEST_PROC_AFFINITY
    /* didnt seem to work.... */
    /*
     * increasing the priority of the process to minimise packet generation
     * delay
     */
    int rc = setpriority(PRIO_PROCESS, 0, -15);

    if (rc < 0) {
        perror("setpriority:");
        fprintf(stderr, "setting priority to valid level\n");
        rc = setpriority(PRIO_PROCESS, 0, 0);
    }
    
    /* setting the affinity of the process  */
    cpu_set_t cpu_set;
    int affinity = -1;
    int ncores = 1;

    sched_getaffinity(0, sizeof(cpu_set_t), &cpu_set);
    if (errno)
        perror("couldn't get affinity:");

    if ((ncores = sysconf(_SC_NPROCESSORS_CONF)) <= 0)
        err("sysconf: couldn't get _SC_NPROCESSORS_CONF");

    CPU_ZERO(&cpu_set);
    CPU_SET(affinity, &cpu_set);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpu_set) != 0)
        err("couldn't change CPU affinity");
#endif

    test = iperf_new_test();

    if (!test)
        iperf_errexit(NULL, "create new test error - %s", iperf_strerror(i_errno));
    iperf_defaults(test);	/* sets defaults */

    if (iperf_parse_arguments(test, argc, argv) < 0) {
        iperf_err(test, "parameter error - %s", iperf_strerror(i_errno));
        fprintf(stderr, "\n");
        usage_long(stdout);
        exit(1);
    }

    if (run(test) < 0)
        iperf_errexit(test, "error - %s", iperf_strerror(i_errno));

    iperf_free_test(test);

    return 0;
}


static jmp_buf sigend_jmp_buf;

static void __attribute__ ((noreturn))
sigend_handler(int sig)
{
    longjmp(sigend_jmp_buf, 1);
}

/**************************************************************************/
static int
run(struct iperf_test *test)
{
    /* Termination signals. */
    iperf_catch_sigend(sigend_handler);
    if (setjmp(sigend_jmp_buf))
	iperf_got_sigend(test);

#ifndef __WIN32__
    /* Ignore SIGPIPE to simplify error handling */
    signal(SIGPIPE, SIG_IGN);
#endif

    if (iperf_create_pidfile(test) < 0) {
        i_errno = IEPIDFILE;
        iperf_errexit(test, "error - %s", iperf_strerror(i_errno));
    }

    switch (test->role) {
        case 's':
	    if (test->daemon) {
#ifndef __WIN32__
		int rc;
		rc = daemon(0, 0);
		if (rc < 0) {
		    i_errno = IEDAEMON;
		    iperf_errexit(test, "error - %s", iperf_strerror(i_errno));
		}
#else
                iperf_errexit(test, "daemon mode not supported on windows.");
#endif
	    }
            for (;;) {
		int rc;
		rc = iperf_run_server(test);
		if (rc < 0) {
		    iperf_err(test, "error - %s", iperf_strerror(i_errno));
		    if (rc < -1) {
		        iperf_errexit(test, "exiting");
		    }
                }
                else {
                    iperf_err(test, "Finished with iperf_run_srver..");
                }
                cleanup_server(test);
                iperf_reset_test(test);
                if (iperf_get_test_one_off(test)) {
		    /* Authentication failure doesn't count for 1-off test */
		    if (rc < 0 && i_errno == IEAUTHTEST) {
			continue;
		    }
		    break;
		}
            }
            break;
	case 'c':
	    if (iperf_run_client(test) < 0)
		iperf_errexit(test, "error - %s", iperf_strerror(i_errno));
            break;
        default:
            usage();
            break;
    }

    iperf_delete_pidfile(test);

    iperf_catch_sigend(SIG_DFL);
#ifndef __WIN32__
    signal(SIGPIPE, SIG_DFL);
#endif

    return 0;
}

#ifdef __WIN32__
const char* winstrerror() {
   // To decode these, try:
   // http://msdn.microsoft.com/library/default.asp?url=/library/en-us/winsock/winsock/windows_sockets_error_codes_2.asp
   static char lfsrr[128];
   sprintf(lfsrr, "errno: %s(%d) (WSAGetLastError: Error# %d)", strerror(errno), errno, WSAGetLastError());
   return lfsrr;
}
#endif
