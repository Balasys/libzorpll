#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include <zorpll/random.h>

#include <stdio.h>
#include <set>

#define ROUNDS 1000

BOOST_AUTO_TEST_CASE(test_random)
{
  guchar buf[64];
  guint i, j;
  bool error = false;

  for (i = 0; i < ROUNDS; i++)
    {
      z_random_sequence_get_bounded(Z_RANDOM_STRONG, buf, sizeof(buf), 'A', 'Z');
      for (j = 0; j < sizeof(buf); j++)
        {
          if (buf[j] < 'A' || buf[j] > 'Z')
            {
              error = true;
              break;
            }
        }
    }
  BOOST_CHECK_MESSAGE(!error, "Invalid character in bounded random data");
}

BOOST_AUTO_TEST_CASE(test_random_seq_generator)
{
  unsigned int min =  34;
  unsigned int max = 123;
  auto sequence = z_random_sequence_create(min, max);
  BOOST_CHECK(sequence.size() == max - min + 1);

  // check unicity
  std::set<unsigned int> verify_set;
  for (const auto &i : sequence)
    {
      auto it_pair = verify_set.emplace(i);
      BOOST_CHECK(it_pair.second);
    }

  // check that each item is in the set
  unsigned it = min;
  for (const auto &i : verify_set)
    {
      BOOST_CHECK(it++ == i);
    }
}
