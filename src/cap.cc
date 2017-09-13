/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 *
 ***************************************************************************/

#include <zorpll/zorplib.h>
#include <zorpll/cap.h>
#include <zorpll/log.h>

#if ZORPLIB_ENABLE_CAPS

const gchar *zorp_caps = NULL;

/**
 * This function modifies the current permitted set of capabilities by
 * enabling or disabling the capability specified in capability.
 *
 * @param[in] capability capability to turn off or on
 * @param[in] onoff specifies whether the capability should be enabled or disabled
 *
 * @returns whether the operation was successful.
 **/
gboolean 
cap_modify(int capability, int onoff)
{
  cap_t caps;

  z_enter();
  if (!zorp_caps)
    z_return(TRUE);
  
  caps = cap_get_proc();
  if (!caps)
    z_return(FALSE);

  if (cap_set_flag(caps, CAP_EFFECTIVE, 1, &capability, static_cast<cap_flag_value_t>(onoff)) == -1)
    {
      cap_free(caps);
      z_return(FALSE);
    }

  if (cap_set_proc(caps) == -1)
    {
      cap_free(caps);
      z_return(FALSE);
    }
  cap_free(caps);
  z_return(TRUE);
}

/**
 * Save the set of current capabilities and return it.
 *
 * @returns the current set of capabilities
 *
 * This function saves the set of current capabilities and returns it.
 * The caller might restore the saved set of capabilities by using cap_restore().
 **/
cap_t 
cap_save(void)
{
  z_enter();
  if (!zorp_caps)
    z_return(NULL);

  z_return(cap_get_proc());
}

/**
 * Restore the set of current capabilities specified by r.
 *
 * @param[in] r capability set saved by cap_save()
 *
 * @returns whether the operation was successful.
 **/
gboolean
cap_restore(cap_t r)
{
  gboolean rc;

  z_enter();
  if (!zorp_caps)
    z_return(TRUE);

  rc = cap_set_proc(r) != -1;
  cap_free(r);
  z_return(rc);
}

#endif
