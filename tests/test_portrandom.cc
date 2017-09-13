#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

/**
 * Test source port randomization implementation in socket.c.
 **/

/* third-party headers */
#include <randtest.h>
/*
 * randtest.h and randtest.c come from http://www.fourmilab.ch/random/
 *
 * According to the website, they're public domain.
 */

/* zorp headers */
#include <zorpll/socket.h>

/* glib headers */
#include <glib.h>

/* unix headers */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

/* standard c headers */
#include <stdio.h>
#include <stdlib.h>

/* test-specific constants */
/** number of iterations to run */
#define ITERATIONS 100000
/** a port that should be in the port range */
#define DEFAULT_PORT 6666
/** lowest port in range; should match the one in socket.c. */
#define PORT_MIN 1024
/** highest port in range; should match the one in socket.c. */
#define PORT_MAX 65535
/** print each port number as we go */
#define PRINT_PORTS 0
/** minimum entropy per byte of port required */
#define MIN_ENTROPY 7.9
/** disable this to see what happened if we used only ZSF_LOOSE_BIND. */
#define REALLY_RANDOM 1
/** if enabled, failing to bind will stop the test; otherwise we only really test the random numbers */
#define FAIL_ON_BIND 1

class PortrandomSetup
{
public:
  PortrandomSetup()
    {
    }
  ~PortrandomSetup()
    {
    }

     /**
     * Bind once and return the port we got.
     *
     * @error the error string
     * @returns port bound to
     **/
    int try_bind_once(std::string &error)
    {
      int socket_fd;
      struct sockaddr_in socket_address;
      char *ip_address = "127.0.0.1";
    #if REALLY_RANDOM
      guint32 sock_flags = ZSF_LOOSE_BIND | ZSF_RANDOM_BIND;
    #else
      guint32 sock_flags = ZSF_LOOSE_BIND;
    #endif
      gint rc = -1;                         /* result code, 0 is success and -1 failure */

      /* create socket */
      socket_fd = socket(AF_INET, SOCK_STREAM, 0);
      if (socket_fd == -1)
        error = "Failed: trying to create socket in portrandom.c:try_bind_once()";

      /* set address */
      socket_address.sin_family = AF_INET;
      inet_aton(ip_address, &socket_address.sin_addr);
      socket_address.sin_port = htons(DEFAULT_PORT);

      /* call the function to test */
      rc = z_do_ll_bind(socket_fd, (struct sockaddr *)&socket_address, sizeof(socket_address), sock_flags);
      if (rc == -1)
        error = "Failed: z_do_ll_bind call in portrandom.c:try_bind_once()";

      close(socket_fd);
      return ntohs(socket_address.sin_port);
    }
};

BOOST_FIXTURE_TEST_SUITE(testsuite, PortrandomSetup)

BOOST_AUTO_TEST_CASE(test_portrandom)
{
  int i;
  int port;
  guint16 real_port;    /* port at its actual size (for randomtest) */
  double ent, chisq, mean, montepi, scc;        /* randomtest results */

  printf("Testing source port randomness; iterations=%d, minimal entropy=%f\n", ITERATIONS, MIN_ENTROPY);
  rt_init(FALSE);   /* initialize randomtest, not binary */

  /* run test iterations */
  std::string error_string;
  for (i = 0; i < ITERATIONS; i++)
    {
      port = try_bind_once(error_string);
      if (!error_string.empty())
        break;
#if PRINT_PORTS
      printf("Port bound to: %d\n", port);
#endif
      if (port < PORT_MIN)
        {
          error_string = "Failed: port number below minimum.";
          break;
        }
      if (port > PORT_MAX)
        {
          error_string = "Failed: port number above maximum.";
          break;
        }
      real_port = (guint16)port;
      rt_add(&real_port, sizeof(real_port));
    }

  BOOST_CHECK_MESSAGE(error_string.empty(), error_string);

  /* check randomness */
  rt_end(&ent, &chisq, &mean, &montepi, &scc);
  printf("Randomness: entropy=%f, chi-square=%f, mean=%f, monte-carlo-pi=%f, serial correlation=%f\n", ent, chisq, mean, montepi, scc);

  /* make decision */
  BOOST_CHECK_MESSAGE(ent >= MIN_ENTROPY, "Passed: entropy is high enough.");
}

BOOST_AUTO_TEST_SUITE_END()
