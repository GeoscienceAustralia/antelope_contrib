/***************************************************************************
 * slutils.c
 *
 * General utility functions.
 *
 * Written by Chad Trabant, ORFEUS/EC-Project MEREDIAN
 *
 * Originally based on the SeedLink interface of the modified Comserv in
 * SeisComP written by Andres Heinloo
 *
 * Version: 2003.276
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libslink.h"


/***************************************************************************
 * sl_dtime();
 * Return a double precision version of the current time since the epoch.
 * This is the number of seconds since Jan. 1, 1970 without leap seconds.
 ***************************************************************************/
double
sl_dtime (void)
{
  /* Now just a shell for the portable version */
  return slp_dtime();
}				/* End of sl_dtime() */


/***************************************************************************
 * sl_doy2md():
 * Compute the month and day-of-month from a year and day-of-year.
 *
 * Returns 0 on success and -1 on error.
 ***************************************************************************/
int
sl_doy2md(int year, int jday, int *month, int *mday)
{
  int idx;
  int leap;
  int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  
  /* Sanity check for the supplied year */
  if ( year < 1900 || year > 2100 )
    {
      sl_log_r (NULL, 1, 0, "sl_doy2md(): year (%d) is out of range\n", year);
      return -1;
    }
    
  /* Test for leap year */
  leap = ( ((year%4 == 0) && (year%100 != 0)) || (year%400 == 0) ) ? 1 : 0;

  /* Add a day to February if leap year */
  if ( leap )
    days[1]++;

  if (jday > 365+leap || jday <= 0)
    {
      sl_log_r (NULL, 1, 0, "sl_doy2md(): day-of-year (%d) is out of range\n", jday);
      return -1;
    }
    
  for ( idx=0; idx < 12; idx++ )
    {
      jday -= days[idx];

      if ( jday <= 0 )
	{
	  *month = idx + 1;
	  *mday = days[idx] + jday;
	  break;
	}
    }

  return 0;
}				/* End of sl_doy2md() */


/***************************************************************************
 * sl_checkversion():
 * Check server version number against some value
 *
 * Returns:
 *  1 = version is greater than or equal to value specified
 *  0 = no server version is known
 * -1 = version is less than value specified
 ***************************************************************************/
int
sl_checkversion (const SLCD * slconn, float version)
{
  if (slconn->server_version == 0.0)
    {
      return 0;
    }
  else if (slconn->server_version >= version)
    {
      return 1;
    }
  else
    {
      return -1;
    }
}				/* End of sl_checkversion */


/***************************************************************************
 * sl_checkslcd():
 * Check a SeedLink connection description (SLCD struct).
 *
 * Returns 0 if pass and -1 if problems were identified.
 ***************************************************************************/
int
sl_checkslcd (const SLCD * slconn)
{
  int retval = 0;
  char *ptr;

  if (slconn->streams == NULL && slconn->info == NULL)
    {
      sl_log_r (slconn, 1, 0, "sl_checkslconn(): stream chain AND info type are empty\n");
      retval = -1;
    }

  ptr = strchr (slconn->sladdr, ':');
  if (slconn->sladdr == NULL)
    {
      sl_log_r (slconn, 1, 0, "sl_checkslconn(): server address is empty\n");
      retval = -1;
    }
  else if (ptr == NULL ||
	   ptr == slconn->sladdr ||
	   *++ptr == '\0')
    {
      sl_log_r (slconn, 1, 0, "[%s] host address is not in `[hostname]:port' format\n",
	      slconn->sladdr);
      retval = -1;
    }

  return retval;
}				/* End of sl_checkslconn */
