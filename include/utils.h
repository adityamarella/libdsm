#ifndef DSM_COMMON_UTILS_H
#define DSM_COMMON_UTILS_H

#include <sys/types.h>
#include <unistd.h>
#include <time.h>

char __log_timestamp__[32];

long long current_us();

/*
 * The macros below are for debugging and printing out verbose output. They are
 * similar to the Linux debug macros in that defining DEBUG will enable debug
 * output. The verbose macro will print if DEBUG was defined or if the `active`
 * parameter is true.
 */
#ifdef DEBUG
    #define DEBUG_ 1
#else
    #define DEBUG_ 0
#endif

#define debug_cond(condition, stream, ...) \
    do { \
        if ((condition)) { \
            time_t now = time(0); \
            struct tm *stm = gmtime(&now); \
            strftime (__log_timestamp__, sizeof(__log_timestamp__), "%Y-%m-%d %H:%M:%S", stm); \
            fprintf(stream, "%s %s:%d:%s(): ", __log_timestamp__,  __FILE__, __LINE__, __func__); \
            fprintf(stream, __VA_ARGS__); \
            fflush(stream); \
        } \
    } while (0)

#define debug(...) debug_cond(DEBUG_, stdout, __VA_ARGS__)

#define log(...) debug_cond(0, stdout, __VA_ARGS__)

#define verbose(active, ...) \
    do { \
        if ((active)) { \
            printf(__VA_ARGS__); \
            fflush(stdout); \
        } \
    } while (0)

#define if_debug \
  if (DEBUG_)

/*
 * The macros below are helper macros for printing error messages.
 * Additionally, err_exit will exit with a failing return code after printing
 * the message.
 */
#define print_err(...) \
    do { \
        fprintf(stdout, "ERROR: "); \
        fprintf(stdout, __VA_ARGS__); \
        fflush(stdout); \
    } while (0)

#define err_exit(...) \
    do { \
        print_err(__VA_ARGS__); \
        exit(EXIT_FAILURE); \
    } while (0)

#define assert_malloc(ptr) \
    do { \
        if (!(ptr)) { \
            err_exit("Couldn't allocate memory for '%s' at %s:%d[%s()].\n", \
                #ptr, __FILE__, __LINE__, __func__); \
        }; \
    } while (0)

#define assert(cond) \
    do { \
        if (!(cond)) { \
            err_exit("Assert '%s' failed %s:%d[%s()].\n", \
                #cond, __FILE__, __LINE__, __func__); \
        }; \
    } while (0)

/*
 * Given a usage() function, the macros below print some message, followed by a
 * new line, followed by the usage information as printed by usage(). The do,
 * while wrapper is a convention that is used so that an invocation of the macro
 * mirrors a function invocation.
 */
#define usage_msg(...) \
    do { \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n"); \
        usage(); \
    } while (0)

#define usage_msg_exit(...) \
    do { \
        usage_msg(__VA_ARGS__); \
        exit(EXIT_FAILURE); \
    } while (0)

/*
 * Typed versions of min() and max().
 */
#ifndef max
#define max(a,b) \
  ({ \
     __typeof__ (a) _a = (a); \
     __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; \
   })
#endif

#ifndef min
#define min(a,b) \
  ({ \
     __typeof__ (a) _a = (a); \
     __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; \
   })
#endif

/*
 * A simple macro that allows a variable to pass through unused checks even if
 * it is actually unused.
 */
#define UNUSED(var) \
  (void)(var)


#endif
