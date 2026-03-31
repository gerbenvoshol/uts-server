#include "http.h"
#include <argp.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <limits.h>
#ifdef BSD
#include <sys/syslimits.h>
#else
#include <linux/limits.h>
#endif /* BSD */
#include <stdio.h>
#include <stdlib.h>
#include <sys/syslog.h>
#include <unistd.h>

const char *argp_program_version = UTS_VERSION;

const char *argp_program_bug_address =
    "Pierre-Francois Carpentier <carpentier.pf@gmail.com>";

static char doc[] = "\nUTS micro timestamp server (RFC 3161)";

static struct argp_option options[] = {
    {"conffile",   'c', "CONFFILE",   0, "Path to configuration file"},
    {"daemonize",  'd', 0,            0, "Launch as a daemon"},
    {"pidfile",    'p', "PIDFILE",    0, "Path to pid file"},
    {"debug",      'D', 0,            0, "STDOUT debugging"},
    {"chroot-dir", 'r', "CHROOT_DIR", 0,
     "Chroot to this directory before starting (requires root or "
     "CAP_SYS_CHROOT); all other paths (-c, -p, config) are resolved "
     "relative to this root after the chroot"},
    {0}};

/* A description of the arguments we accept. */
static char args_doc[] = "-c CONFFILE [-r CHROOT_DIR] [-d] [-D] [-p <pidfile>]";

struct arguments {
    char *args[2]; /* arg1 & arg2 */
    int daemonize;
    bool stdout_dbg;
    char *conffile;
    char *pidfile;
    char *chroot_dir;
};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    /* Get the input argument from argp_parse, which we
       know is a pointer to our arguments structure. */
    struct arguments *arguments = (struct arguments *)state->input;

    switch (key) {
    case 'd':
        arguments->daemonize = 1;
        break;
    case 'D':
        arguments->stdout_dbg = 1;
        break;
    case 'c':
        arguments->conffile = arg;
        break;
    case 'p':
        arguments->pidfile = arg;
        break;
    case 'r':
        arguments->chroot_dir = arg;
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

/* Our argp parser. */
static struct argp argp = {options, parse_opt, args_doc, doc};

int main(int argc, char **argv) {
    struct arguments args;
    args.conffile = NULL;
    args.pidfile = NULL;
    args.daemonize = 0;
    args.stdout_dbg = 0;
    args.chroot_dir = NULL;
    argp_parse(&argp, argc, argv, 0, 0, &args);
    int ret = EXIT_SUCCESS;

    /* --- Optional chroot -------------------------------------------------- *
     * Open syslog with LOG_NDELAY first so the socket to the real /dev/log    *
     * is established before we enter the jail.  All subsequent path           *
     * arguments (-c, -p) are treated as absolute paths inside the chroot.     *
     * The chroot must happen before realpath() so that path resolution is     *
     * anchored to the new root.  It also happens before mg_start(), which     *
     * is important because civetweb opens the SSL certificate file and drops  *
     * privileges (run_as_user) inside mg_start().                             *
     * ----------------------------------------------------------------------- */
    if (args.chroot_dir != NULL) {
        openlog("uts-server", LOG_PID | LOG_NDELAY, LOG_DAEMON);
        if (chroot(args.chroot_dir) != 0) {
            syslog(LOG_CRIT, "chroot('%s') failed: %s",
                   args.chroot_dir, strerror(errno));
            closelog();
            return EXIT_FAILURE;
        }
        if (chdir("/") != 0) {
            syslog(LOG_CRIT, "chdir('/') after chroot failed: %s",
                   strerror(errno));
            closelog();
            return EXIT_FAILURE;
        }
        syslog(LOG_NOTICE, "chroot to '%s' succeeded", args.chroot_dir);
    }

    // get the full path of the configuration (daemon -> chdir to / concequently
    // full path is necessary)
    char conf_fp[PATH_MAX];
    if (realpath(args.conffile, conf_fp) == NULL) {
        syslog(LOG_CRIT, "unable to get the full path of the configuration "
                         "file, uts-server start failed");
        return EXIT_FAILURE;
    }

    init_pid(args.pidfile);
    // get the full path for the pid file
    char pid_file[PATH_MAX];
    if ((args.pidfile != NULL) && realpath(args.pidfile, pid_file) == NULL) {
        syslog(LOG_CRIT, "unable to get the full path of the pid "
                         "file, uts-server start failed");
        return EXIT_FAILURE;
    }

    // get the directory containing the configuration file
    // other uts-server files (ca, certs, etc) can be declared relatively to the
    // configuration file
    char *tmp_wd = strdup(conf_fp);
    char *conf_wd = dirname(tmp_wd);

    if (args.daemonize)
        skeleton_daemon();
    else
        set_sig_handler();

    syslog(LOG_NOTICE,
           "uts-server daemon starting with conf '%s' from working dir '%s'",
           conf_fp, conf_wd);

    if (args.pidfile != NULL) {
        if (write_pid(pid_file) == 0) {
            syslog(LOG_CRIT, "failed to write pid file '%s'", pid_file);
            return EXIT_FAILURE;
        }
    }

    while (1) {
        ret = http_server_start(conf_fp, conf_wd, args.stdout_dbg);
        break;
    }

    syslog(LOG_NOTICE, "uts-server daemon terminated.");
    free(tmp_wd);
    closelog();

    return ret;
}
