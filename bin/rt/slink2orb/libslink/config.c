/***************************************************************************
 * config.c:
 *
 * Routines to assist with the configuration of a SeedLink connection
 * description.
 *
 * Written by Chad Trabant, ORFEUS/EC-Project MEREDIAN
 *
 * modified: 2003.276
 ***************************************************************************/

#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "libslink.h"


/***************************************************************************
 * sl_read_streamlist()
 * Read a list of streams and selectors from a file and add them to the 
 * stream chain for configuring a multi-station connection.
 *
 * If 'defselect' is not NULL or 0 it will be used as the default selectors
 * for entries will no specific selectors indicated.
 *
 * The file is expected to be repeating lines of the form:
 *   <NET> <STA> [selectors]
 * For example:
 * --------
 * # Comment lines begin with a '#' or '*'
 * GE ISP  BH?.D
 * NL HGN
 * MN AQU  BH?  HH?
 * -------- 
 *
 * Returns the number of streams configured or -1 on error.
 ***************************************************************************/
int
sl_read_streamlist (SLCD * slconn, const char * streamfile,
		    const char * defselect)
{
  FILE *streamfp;
  char net[3];
  char sta[6];
  char selectors[100];
  char line[100];
  int fields;
  int count;
  int stacount;
  int addret;

  net[0] = '\0';
  sta[0] = '\0';
  selectors[0] = '\0';

  /* Open the stream list file */
  if ((streamfp = fopen (streamfile, "r")) == NULL)
    {
      if (errno == ENOENT)
	{
	  sl_log_r (slconn, 0, 0, "could not open stream list file: %s\n", streamfile);
	  return -1;
	}
      else
	{
	  sl_log_r (slconn, 1, 0, "opening stream list file, %s\n", strerror (errno));
	  return -1;
	}
    }

  sl_log_r (slconn, 0, 1, "reading stream list from %s\n", streamfile);

  count = 1;
  stacount = 0;

  while ( (fgets (line, sizeof(line), streamfp)) !=  NULL)
    {
      fields = sscanf (line, "%2s %5s %99[a-zA-Z0-9?. ]\n",
		       net, sta, selectors);

      /* Ignore blank or comment lines */
      if ( fields < 0 || net[0] == '#' || net[0] == '*' )
	continue;

      if ( fields < 2 )
	{
	  sl_log_r (slconn, 1, 0, "parsing line %d of stream list\n", count);
	}
      
      /* Add this stream to the stream chain */
      if ( fields == 3 )
	{
	  sl_addstream (slconn, net, sta, selectors, -1, NULL);
	  stacount++;
	}
      else 
	{
	  addret = sl_addstream (slconn, net, sta, defselect, -1, NULL);
	  stacount++;
	}
      
	count++;
    }

  if ( stacount == 0 )
    {
      sl_log_r (slconn, 1, 0, "no streams defined in %s\n", streamfile);
    }
  else if ( stacount > 0 )
    {
      sl_log_r (slconn, 0, 2, "Read %d streams from %s\n", stacount, streamfile);
    }

  if ( fclose (streamfp) )
    {
      sl_log_r (slconn, 1, 0, "closing stream list file, %s\n", strerror (errno));
      return -1;
    }

  return count;
}              /* End of sl_read_streamlist() */


/***************************************************************************
 * sl_parse_streamlist()
 * Parse a string of streams and selectors and add them to the stream
 * chain for configuring a multi-station connection.
 *
 * The string should be of the following form:
 * "stream1[:selectors1],stream2[:selectors2],..."
 *
 * For example:
 * "IU_KONO:BHE BHN,GE_WLF,MN_AQU:HH?.D"
 * 
 * Returns the number of streams configured or -1 on error.
 ***************************************************************************/
int
sl_parse_streamlist (SLCD * slconn, const char * streamlist,
		     const char * defselect)
{
  int count = 0;
  int fields;

  const char *staselect;
  char *net;
  char *sta;

  strlist *ringlist   = NULL;       /* split streamlist on ',' */
  strlist *reqlist    = NULL;       /* split ringlist on ':' */
  strlist *netstalist = NULL;       /* split reqlist[0] on "_" */

  strlist *ringptr    = NULL;
  strlist *reqptr     = NULL;
  strlist *netstaptr  = NULL;

  /* Parse the streams and selectors */
  strparse (streamlist, ",", &ringlist);
  ringptr = ringlist;

  while (ringptr != 0)
    {
      net = NULL;
      sta = NULL;
      staselect = NULL;
      
      fields = strparse (ringptr->element, ":", &reqlist);
      reqptr = reqlist;

      /* Fill in the NET and STA fields */
      if (strparse (reqptr->element, "_", &netstalist) != 2)
	{
	  sl_log_r (slconn, 1, 0, "Not in NET_STA format: %s\n", reqptr->element);
	  count = -1;
	}
      else
	{
	  /* Point to the first element, should be a network code */
	  netstaptr = netstalist;
	  if (strlen (netstaptr->element) == 0)
	    {
	      sl_log_r (slconn, 1, 0, "Not in NET_STA format: %s\n",
		      reqptr->element);
	      count = -1;
	    }
	  net = netstaptr->element;
	  
	  /* Point to the second element, should be a station code */
	  netstaptr = netstaptr->next;
	  if (strlen (netstaptr->element) == 0)
	    {
	      sl_log_r (slconn, 1, 0, "Not in NET_STA format: %s\n",
		      reqptr->element);
	      count = -1;
	    }
	  sta = netstaptr->element;
	}

      if (fields > 1)
	{                   /* Selectors were included */
	  /* Point to the second element of reqptr, should be selectors */
	  reqptr = reqptr->next;
	  if (strlen (reqptr->element) == 0)
	    {
	      sl_log_r (slconn, 1, 0, "Empty selector: %s\n", reqptr->element);
	      count = -1;
	    }
	  staselect = reqptr->element;
	}
      else  /* If no specific selectors, use the default */
	{
	  staselect = defselect;
	}
      
      /* Add this to the stream chain */
      if ( count != -1 )
        {
          sl_addstream(slconn, net, sta, staselect, -1, 0);
      	  count++;
	}

      /* Free the netstalist (the 'NET_STA' part) */
      strparse (NULL, NULL, &netstalist);
      
      /* Free the reqlist (the 'NET_STA:selector' part) */
      strparse (NULL, NULL, &reqlist);
      
      ringptr = ringptr->next;
    }

  if ( netstalist != NULL )
    {
      strparse (NULL, NULL, &netstalist);
    }
  if ( reqlist != NULL )
    {
      strparse (NULL, NULL, &reqlist);
    }
  
  if ( count == 0 )
    {
      sl_log_r (slconn, 1, 0, "no streams defined in stream list\n");
    }
  else if ( count > 0 )
    {
      sl_log_r (slconn, 0, 2, "Parsed %d streams from stream list\n", count);
    }

  /* Free the ring list */
  strparse (NULL, NULL, &ringlist);
  
  return count;
}              /* End of sl_parse_streamlist() */
