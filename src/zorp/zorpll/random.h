/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/
#ifndef ZORPLIB_RANDOM_H_INCLUDED
#define ZORPLIB_RANDOM_H_INCLUDED

#include <glib.h>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Strength of random number generation algorithm.
 **/
typedef enum
{
  Z_RANDOM_STRONG=0,
  Z_RANDOM_BASIC,
  Z_RANDOM_WEAK,
  Z_RANDOM_NUM_STRENGTHS
} ZRandomStrength;

gboolean
z_random_sequence_get(ZRandomStrength strength, guchar *target, gsize target_len);

gboolean 
z_random_sequence_get_bounded(ZRandomStrength strength, 
                              guchar *target, gsize target_len,
                              guchar min, guchar max);

#ifdef __cplusplus
}
#endif

std::vector<unsigned int> z_random_sequence_create(const unsigned int len, const unsigned int min);

#endif
