/***************************************************************************
 * msrecord.c:
 *
 * Generic routines to parse Mini-SEED records.
 *
 * Appropriate values from the record header will be byte-swapped to the
 * host order.  The purpose of this code is to provide a portable way of
 * accessing common SEED data record header information.  The recognized
 * structures are the Fixed Section Data Header and Blockettes 100, 1000
 * and 1001.  The Blockettes are optionally parsed and the data samples
 * are optionally decompressed/unpacked.
 *
 * Some ideas and structures were used from seedsniff 2.0
 *
 * Written by Chad Trabant, ORFEUS/EC-Project MEREDIAN
 *
 * modified: 2003.305
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "libslink.h"
#include "tswap.h"
#include "unpack.h"

/* Delcare routines only used in this source file */
void encoding_hash (char enc, char *encstr);
double host_latency (MSrecord * msr);


union four_byte_u
{
  float      f;
  uint32_t   l;
};


/***************************************************************************
 * msr_new():
 * Allocate, initialize and return a new MSrecord struct.
 *
 * Returns a pointer to a MSrecord struct on success
 *  or NULL on error.
 ***************************************************************************/
MSrecord *
msr_new ( void )
{
  MSrecord * msr;

  msr = (MSrecord *) malloc (sizeof(MSrecord));

  if ( msr == NULL )
    {
      sl_log_rl (NULL, 0, 1, "msr_new(): error allocating memory\n");
      return NULL;
    }

  msr->fsdh.sequence_number[0] = '\0';
  msr->fsdh.dhq_indicator      = 0;
  msr->fsdh.reserved           = 0;
  msr->fsdh.station[0]         = '\0';
  msr->fsdh.location[0]        = '\0';
  msr->fsdh.channel[0]         = '\0';
  msr->fsdh.network[0]         = '\0';
  msr->fsdh.start_time.year    = 0;
  msr->fsdh.start_time.day     = 0;
  msr->fsdh.start_time.hour    = 0;
  msr->fsdh.start_time.min     = 0;
  msr->fsdh.start_time.sec     = 0;
  msr->fsdh.start_time.unused  = 0;
  msr->fsdh.start_time.fract   = 0;
  msr->fsdh.num_samples        = 0;
  msr->fsdh.samprate_fact      = 0;
  msr->fsdh.samprate_mult      = 0;
  msr->fsdh.act_flags          = 0;
  msr->fsdh.io_flags           = 0;
  msr->fsdh.dq_flags           = 0;
  msr->fsdh.num_blockettes     = 0;
  msr->fsdh.time_correct       = 0;
  msr->fsdh.begin_data         = 0;
  msr->fsdh.begin_blockette    = 0;
  
  msr->Blkt100     = NULL;
  msr->Blkt1000    = NULL;
  msr->Blkt1001    = NULL;
  
  msr->msrecord    = NULL;
  msr->datasamples = NULL;
  msr->numsamples  = -1;
  msr->unpackerr   = MSD_NOERROR;
  
  return msr;
} /* End of msr_new() */


/***************************************************************************
 * msr_free():
 * Free all memory associated with a MSrecord struct, except the original
 * record indicated by the element 'msrecord'.
 *
 ***************************************************************************/
void
msr_free ( MSrecord ** msr )
{
  if ( msr != NULL && *msr != NULL )
    {
      free((*msr)->Blkt100);
      free((*msr)->Blkt1000);
      free((*msr)->Blkt1001);

      if ( (*msr)->datasamples != NULL )
	free((*msr)->datasamples);

      free(*msr);
      
      *msr = NULL;
    }
} /* End of msr_free() */


/***************************************************************************
 * msr_parse():
 * Parses a SEED record header/blockettes and populates a MSrecord struct.
 * 
 * If 'blktflag' is true the blockettes will also be parsed.  The parser
 * recognizes Blockettes 100, 1000 and 1001.
 *
 * If 'unpackflag' is true the data samples are unpacked/decompressed and
 * the MSrecord->datasamples pointer is set appropriately.  The data samples
 * will be 32-bit integers with the same byte order as the host.  The 
 * MSrecord->numsamples will be set to the actual number of samples 
 * unpacked/decompressed and MSrecord->unpackerr will be set to indicate
 * any errors encountered during unpacking/decompression (MSD_NOERROR if
 * no errors).
 *
 * All appropriate values will be byte-swapped to the host order, including
 * the data samples.
 *
 * All header values, blockette values and data samples will be overwritten
 * by subsequent calls to this function.
 *
 * If the msr struct is NULL it will be allocated.
 * 
 * Returns a pointer to the MSrecord struct populated on success
 *  or NULL on error.
 ***************************************************************************/
MSrecord *
msr_parse (SLlog * log, const char * msrecord, MSrecord ** ppmsr,
	   int blktflag, int unpackflag )
{
  int swapflag = 0;		        /* is swapping needed? */
  uint16_t begin_blockette;	        /* byte offset for next blockette */
  MSrecord * msr = NULL;

  if ( ppmsr == NULL )
    {
      sl_log_rl (log, 0, 1, "msr_parse(): pointer to MSrecord cannot be NULL\n");
      return NULL;
    }
  else
    {
      msr = * ppmsr;
    }

  /* If this record is new init a new one otherwise clean it up */
  if ( msr == NULL )
    {
      msr = msr_new();
    }
  else
    {
      if ( msr->Blkt100 != NULL )
	{
	  free (msr->Blkt100);
	  msr->Blkt100 = NULL;
	}
      if ( msr->Blkt1000 != NULL ) 
	{
	  free (msr->Blkt1000);
	  msr->Blkt1000 = NULL;
	}

      if ( msr->Blkt1001 != NULL )
	{
	  free (msr->Blkt1001);
	  msr->Blkt1001 = NULL;
	}

      if ( msr->datasamples != NULL )
	{
	  free (msr->datasamples);
	  msr->datasamples = NULL;
	}
    }

  msr->msrecord = msrecord;

  /* Copy the fixed section into msr */
  memcpy ((void *) &msr->fsdh, msrecord, 48);

  /* Sanity check for msr/quality indicator */
  if (msr->fsdh.dhq_indicator != 'D' &&
      msr->fsdh.dhq_indicator != 'R' &&
      msr->fsdh.dhq_indicator != 'Q')
    {
      sl_log_rl (log, 0, 1, "Record header/quality indicator unrecognized: %c\n",
		 msr->fsdh.dhq_indicator);
      return NULL;
    }

  /* Check to see if byte swapping is needed (bogus year makes good test) */
  if ((msr->fsdh.start_time.year < 1960) ||
      (msr->fsdh.start_time.year > 2050))
    swapflag = 1;

  /* Change byte order? */
  tswap2 ((uint16_t *) &msr->fsdh.start_time.year, swapflag);
  tswap2 ((uint16_t *) &msr->fsdh.start_time.day, swapflag);
  tswap2 ((uint16_t *) &msr->fsdh.start_time.fract, swapflag);
  tswap2 ((uint16_t *) &msr->fsdh.num_samples, swapflag);
  tswap2 ((uint16_t *) &msr->fsdh.samprate_fact, swapflag);
  tswap2 ((uint16_t *) &msr->fsdh.samprate_mult, swapflag);
  tswap4 ((uint32_t *) &msr->fsdh.time_correct, swapflag);
  tswap2 ((uint16_t *) &msr->fsdh.begin_data, swapflag);
  tswap2 ((uint16_t *) &msr->fsdh.begin_blockette, swapflag);


  /* Parse the blockettes if requested */
  if ( blktflag )
    {
      /* Define some structures */
      struct blkt_head_s  *blkt_head;
      struct blkt_100_s   *blkt_100;
      struct blkt_1000_s  *blkt_1000;
      struct blkt_1001_s  *blkt_1001;
      union  four_byte_u *four_byte;
      
      /* Initialize the blockette structures */
      blkt_head = (struct blkt_head_s *) malloc (sizeof (struct blkt_head_s));
      blkt_100  = NULL;
      blkt_1000 = NULL;
      blkt_1001 = NULL;

      /* loop through blockettes as long as number is non-zero and viable */
      begin_blockette = msr->fsdh.begin_blockette;
      
      while ((begin_blockette != 0) &&
	     (begin_blockette <= SLRECSIZE))
	{
	  
	  memcpy ((void *) blkt_head, msrecord + begin_blockette,
		  sizeof (struct blkt_head_s));
	  tswap2 (&blkt_head->blkt_type, swapflag);
	  tswap2 (&blkt_head->next_blkt, swapflag);
	  
	  if (blkt_head->blkt_type == 100)
	    {			/* found a 100 blockette */
	      four_byte =
		(union four_byte_u *) malloc (sizeof (union four_byte_u));
	      blkt_100 = (struct blkt_100_s *) malloc (sizeof (struct blkt_100_s));
	      memcpy ((void *) blkt_100, msrecord + begin_blockette,
		      sizeof (struct blkt_100_s));
	      
	      four_byte->f = blkt_100->sample_rate;
	      tswap4 (&four_byte->l, swapflag);
	      blkt_100->sample_rate = four_byte->f;
	      
	      blkt_100->blkt_type = blkt_head->blkt_type;
	      blkt_100->next_blkt = blkt_head->next_blkt;
	      
	      msr->Blkt100 = blkt_100;
	      
	      free (four_byte);
	    }
	  
	  if (blkt_head->blkt_type == 1000)
	    
	    {			/* found the 1000 blockette */
	      blkt_1000 =
		(struct blkt_1000_s *) malloc (sizeof (struct blkt_1000_s));
	      memcpy ((void *) blkt_1000, msrecord + begin_blockette,
		      sizeof (struct blkt_1000_s));
	      
	      blkt_1000->blkt_type = blkt_head->blkt_type;
	      blkt_1000->next_blkt = blkt_head->next_blkt;
	      
	      msr->Blkt1000 = blkt_1000;
	    }
	  
	  if (blkt_head->blkt_type == 1001)
	    {			/* found a 1001 blockette */
	      blkt_1001 =
		(struct blkt_1001_s *) malloc (sizeof (struct blkt_1001_s));
	      memcpy ((void *) blkt_1001, msrecord + begin_blockette,
		      sizeof (struct blkt_1001_s));
	      
	      blkt_1001->blkt_type = blkt_head->blkt_type;
	      blkt_1001->next_blkt = blkt_head->next_blkt;
	      
	      msr->Blkt1001 = blkt_1001;
	    }
	  
	  /* Point to the next blockette */
	  begin_blockette = blkt_head->next_blkt;
	}				/* End of while looping through blockettes */
      
      if (blkt_1000 == NULL)
	{
	  sl_log_rl (log, 1, 0, "1000 blockette was NOT found for %s.%s.%s.%s!",
		     msr->fsdh.network, msr->fsdh.station,
		     msr->fsdh.location, msr->fsdh.channel);
	}
      
      free (blkt_head);
    }

  /* Unpack the data samples if requested */
  if ( unpackflag )
    {
      msr->numsamples = msr_unpack (log, msr, swapflag);
    }
  else {
    msr->numsamples = -1;
  }

  /* Re-direct the original pointer and return the new */
  *ppmsr = msr;
  return msr;
} /* End of msr_parse() */


/***************************************************************************
 * msr_print():
 * Prints header values, if 'details' is greater than zero then
 * detailed information (values in the fixed header and following
 * blockettes) is printed.
 *
 * Returns non-zero on success or 0 on error.
 ***************************************************************************/
int
msr_print (SLlog * log, MSrecord * msr, int details)
{
  char sourcename[50];
  char prtnet[4], prtsta[7];
  char prtloc[4], prtchan[5];
  char stime[25];
  double latency;

  /* Generate clean identifier strings */
  strncpclean (prtnet, msr->fsdh.network, 2);
  strncpclean (prtsta, msr->fsdh.station, 5);
  strncpclean (prtloc, msr->fsdh.location, 2);
  strncpclean (prtchan, msr->fsdh.channel, 3);

  if (prtnet[0] != '\0')
    strcat (prtnet, "_");
  if (prtsta[0] != '\0')
    strcat (prtsta, "_");
  if (prtloc[0] != '\0')
    strcat (prtloc, "_");

  /* Build the source name string */
  sprintf (sourcename, "%.3s%.6s%.3s%.3s", prtnet, prtsta, prtloc, prtchan);

  /* Build a start time string */
  snprintf (stime, 25, "%04d,%03d,%02d:%02d:%02d.%d",
	    msr->fsdh.start_time.year, msr->fsdh.start_time.day,
	    msr->fsdh.start_time.hour, msr->fsdh.start_time.min,
	    msr->fsdh.start_time.sec,  msr->fsdh.start_time.fract);

  /* Calculate the latency */
  latency = host_latency (msr);

  /* Report information in the fixed header */
  if (details > 0)
    {
      sl_log_rl (log, 0, 0, "                 source: %s\n", sourcename);
      sl_log_rl (log, 0, 0, "             start time: %s  (latency ~%1.1f sec)\n",
		 stime, latency);
      sl_log_rl (log, 0, 0, "      number of samples: %d\n", msr->fsdh.num_samples);
      sl_log_rl (log, 0, 0, "     sample rate factor: %d\n", msr->fsdh.samprate_fact);
      sl_log_rl (log, 0, 0, " sample rate multiplier: %d\n", msr->fsdh.samprate_mult);
      sl_log_rl (log, 0, 0, "     num. of blockettes: %d\n",
		 msr->fsdh.num_blockettes);
      sl_log_rl (log, 0, 0, "        time correction: %ld\n",
		 msr->fsdh.time_correct);
      sl_log_rl (log, 0, 0, "      begin data offset: %d\n", msr->fsdh.begin_data);
      sl_log_rl (log, 0, 0, "  fist blockette offset: %d\n",
		 msr->fsdh.begin_blockette);
    }
  else
    {
      sl_log_rl (log, 0, 0, "  %s, %d samples, stime: %s (latency ~%1.1f sec)\n",
	     sourcename, msr->fsdh.num_samples, stime, latency);
    }

  if (details > 0)
    {
      if ( msr->Blkt100 != NULL )
	{
	  sl_log_rl (log, 0, 0, "          BLOCKETTE 100:\n");
	  sl_log_rl (log, 0, 0, "              next blockette: %d\n",
		     msr->Blkt100->next_blkt);
	  sl_log_rl (log, 0, 0, "          actual sample rate: %.4f\n",
		     msr->Blkt100->sample_rate);
	  sl_log_rl (log, 0, 0, "                       flags: %d\n",
		     msr->Blkt100->flags);
	}
      
      if ( msr->Blkt1000 != NULL )
	{
	  int i, recsize;
	  char order[40];
	  char encstr[100];

	  encoding_hash (msr->Blkt1000->encoding, &encstr[0]);
	  
	  /* Calculate record size in bytes as 2^(blkt_1000->rec_len) */
	  for (recsize = 1, i = 1; i <= msr->Blkt1000->rec_len; i++)
	    recsize *= 2;
	  
	  /* Big or little endian reported by the 1000 blockette? */
	  if (msr->Blkt1000->word_swap == 0)
	    strncpy (order, "Little endian (Intel/VAX)", sizeof(order)-1);
	  else if (msr->Blkt1000->word_swap == 1)
	    strncpy (order, "Big endian (SPARC/Motorola)", sizeof(order)-1);
	  else
	    strncpy (order, "Unkown value", sizeof(order)-1);
	  
	  sl_log_rl (log, 0, 0, "         BLOCKETTE 1000:\n");
	  sl_log_rl (log, 0, 0, "              next blockette: %d\n",
		     msr->Blkt1000->next_blkt);
	  sl_log_rl (log, 0, 0, "                    encoding: %s\n", encstr);
	  sl_log_rl (log, 0, 0, "                  byte order: %s\n", order);
	  sl_log_rl (log, 0, 0, "               record length: %d (val:%d)\n",
		     recsize, msr->Blkt1000->rec_len);
	  sl_log_rl (log, 0, 0, "                    reserved: %d\n",
		     msr->Blkt1000->reserved);
	}
      
      if ( msr->Blkt1001 != NULL )
	{
	  sl_log_rl (log, 0, 0, "         BLOCKETTE 1001:\n");
	  sl_log_rl (log, 0, 0, "              next blockette: %d\n",
		     msr->Blkt1001->next_blkt);
	  sl_log_rl (log, 0, 0, "              timing quality: %d%%\n",
		     msr->Blkt1001->timing_qual);
	  sl_log_rl (log, 0, 0, "                micro second: %d\n",
		     msr->Blkt1001->usec);
	  sl_log_rl (log, 0, 0, "                    reserved: %d\n",
		     msr->Blkt1001->reserved);
	  sl_log_rl (log, 0, 0, "                 frame count: %d\n",
		     msr->Blkt1001->frame_cnt);
	}
    }
  
  return 1;
} /* End of msr_print() */


/***************************************************************************
 * msr_dsamprate():
 * Calculate a double precision sample rate for the specified MSrecord and
 * store it in the passed samprate argument.  If a 100 Blockette was
 * included and parsed, the "Actual sample rate" (Blockette 100, field 3)
 * will be returned, otherwise a nominal sample rate will be calculated
 * from the sample rate factor and multiplier in the fixed section data
 * header.
 *
 * Returns 1 if actual sample rate (from 100 Blockette), 2 if nomial sample
 * rate (from factor and multiplier) or 0 on error.
 ***************************************************************************/
int
msr_dsamprate (MSrecord * msr, double * samprate)
{
  if ( ! msr )
    return 0;

  if ( msr->Blkt100 )
    {
      *samprate = (double) msr->Blkt100->sample_rate;
      return 1;
    }
  else
    {
      /* Calculate the nominal sample rate */
      double srcalc = 0.0;
      int factor;
      int multiplier;

      factor = msr->fsdh.samprate_fact;
      multiplier = msr->fsdh.samprate_mult;
      
      if ( factor > 0 )
	srcalc = (double) factor;
      else if ( factor < 0 )
	srcalc = -1.0 / (double) factor;
      if ( multiplier > 0 )
	srcalc = srcalc * (double) multiplier;
      else if ( multiplier < 0 )
	srcalc = -1.0 * (srcalc / (double) multiplier);
      
      *samprate = srcalc;
      return 2;
    }

} /* End of msr_dsamprate() */


/***************************************************************************
 * msr_depochstime():
 * Convert a btime struct of a FSDH struct of a MSrecord (the record start
 * time) into a double precision (Unix/POSIX) epoch time.
 *
 * Returns double precision epoch time or 0 for error.
 ***************************************************************************/
double
msr_depochstime (MSrecord * msr)
{
  struct btime_s *btime;

  if ( ! msr )
    return 0;

  btime = &msr->fsdh.start_time;

  return  (double) (btime->year - 1970) * 31536000 +
                   ((btime->year - 1969) / 4) * 86400 +
                   (btime->day - 1) * 86400 +
                   btime->hour * 3600 +
                   btime->min * 60 +
                   btime->sec +
                   (double) btime->fract / 10000.0;
} /* End of msr_depochstime() */


/***************************************************************************
 * encoding_hash():
 *
 * Set encstr to a string describing the data frame encoding.
 ***************************************************************************/
void
encoding_hash (char enc, char *encstr)
{
  switch (enc)
    {
    case 0:
      strcpy (encstr, "ASCII text (val:0)");
      break;
    case 1:
      strcpy (encstr, "16 bit integers (val:1)");
      break;
    case 2:
      strcpy (encstr, "24 bit integers (val:2)");
      break;
    case 3:
      strcpy (encstr, "32 bit integers (val:3)");
      break;
    case 4:
      strcpy (encstr, "IEEE floating point (val:4)");
      break;
    case 5:
      strcpy (encstr, "IEEE double precision float (val:5)");
      break;
    case 10:
      strcpy (encstr, "STEIM 1 Compression (val:10)");
      break;
    case 11:
      strcpy (encstr, "STEIM 2 Compression (val:11)");
      break;
    case 12:
      strcpy (encstr, "GEOSCOPE Muxed 24 bit int (val:12)");
      break;
    case 13:
      strcpy (encstr, "GEOSCOPE Muxed 16/3 bit gain/exp (val:13)");
      break;
    case 14:
      strcpy (encstr, "GEOSCOPE Muxed 16/4 bit gain/exp (val:14)");
      break;
    case 15:
      strcpy (encstr, "US National Network compression (val:15)");
      break;
    case 16:
      strcpy (encstr, "CDSN 16 bit gain ranged (val:16)");
      break;
    case 17:
      strcpy (encstr, "Graefenberg 16 bit gain ranged (val:17)");
      break;
    case 18:
      strcpy (encstr, "IPG - Strasbourg 16 bit gain (val:18)");
      break;
    case 19:
      strcpy (encstr, "STEIM 3 Compression (val:19)");
      break;
    case 30:
      strcpy (encstr, "SRO Format (val:30)");
      break;
    case 31:
      strcpy (encstr, "HGLP Format (val:31)");
      break;
    case 32:
      strcpy (encstr, "DWWSSN Gain Ranged Format (val:32)");
      break;
    case 33:
      strcpy (encstr, "RSTN 16 bit gain ranged (val:33)");
      break;
    default:
      sprintf (encstr, "Unknown format code: (%d)", enc);
    }				/* end switch */

} /* End of encoding_hash() */


/***************************************************************************
 * host_latency():
 * Calculate the latency based on the system time in UTC accounting for
 * the time covered using the number of samples and sample rate given.
 * Double precision is returned, but the true precision is dependent on
 * the accuracy of the host system clock among other things.
 *
 * Returns seconds of latency.
 ***************************************************************************/
double
host_latency (MSrecord *msr)
{
  double samp_rate = 0.0;       /* Nominal sampling rate */
  double span = 0.0;            /* Time covered by the samples */
  double epoch;                 /* Current epoch time */
  double sepoch;                /* Epoch time of the record start time */
  double latency = 0.0;

  msr_dsamprate (msr, &samp_rate);
    
  /* Calculate the time covered by the samples */
  if ( samp_rate )
    span = (double) msr->fsdh.num_samples * (1.0 / samp_rate);
  
  /* Grab UTC time according to the system clock */
  epoch = sl_dtime();
  
  /* Now calculate the latency */
  sepoch = msr_depochstime(msr);
  latency = epoch - sepoch - span;
  
  return latency;
} /* End of host_latency() */


