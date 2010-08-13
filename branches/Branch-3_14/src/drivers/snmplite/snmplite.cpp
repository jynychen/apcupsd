/*
 * snmp.c
 *
 * SNMP Lite UPS driver
 */

/*
 * Copyright (C) 2009 Adam Kropelin
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General
 * Public License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA 02111-1307, USA.
 */

#include "apc.h"
#include "snmplite.h"
#include "snmplite-common.h"
#include "snmp.h"
#include "mibs.h"

int snmplite_ups_open(UPSINFO *ups)
{
   struct snmplite_ups_internal_data *sid;

   /* Allocate the internal data structure and link to UPSINFO. */
   sid = (struct snmplite_ups_internal_data *)
      malloc(sizeof(struct snmplite_ups_internal_data));
   if (sid == NULL) {
      log_event(ups, LOG_ERR, "Out of memory.");
      exit(1);
   }

   ups->driver_internal_data = sid;

   memset(sid, 0, sizeof(struct snmplite_ups_internal_data));
   sid->port = 161;
   sid->community = "private";
   sid->vendor = "APC";

   if (ups->device == NULL || *ups->device == '\0') {
      log_event(ups, LOG_ERR, "snmplite Missing hostname");
      exit(1);
   }

   astrncpy(sid->device, ups->device, sizeof(sid->device));

   /*
    * Split the DEVICE statement and assign pointers to the various parts.
    * The DEVICE statement syntax in apcupsd.conf is:
    *
    *    DEVICE address:port:vendor:community
    *
    * vendor can be "APC", "APC_NOTRAP", or "RFC".
    */

   char *cp = sid->device;
   sid->host = sid->device;
   cp = strchr(cp, ':');
   if (cp)
   {
      *cp++ = '\0';
      sid->port = atoi(cp);
      if (sid->port == 0)
      {
         log_event(ups, LOG_ERR, "snmplite Bad port number");
         exit(1);
      }

      cp = strchr(cp, ':');
      if (cp)
      {
         *cp++ = '\0';
         sid->vendor = cp;

         cp = strchr(cp, ':');
         if (cp)
         {
            *cp++ = '\0';
            sid->community = cp;
         }
      }
   }

   // Search for MIB matching vendor
   for (unsigned int i = 0; MibStrategies[i]; i++)
   {
      if (strcmp(MibStrategies[i]->name, sid->vendor) == 0)
      {
         sid->strategy = MibStrategies[i];
         break;
      }
   }

   if (!sid->strategy)
   {
      log_event(ups, LOG_ERR, "snmplite Invalid vendor");
      exit(1);
   }

   sid->snmp = new Snmp::SnmpEngine();
   if (!sid->snmp->Open(sid->host, sid->port, sid->community, 
                        sid->strategy->trapwait_func != NULL))
   {
      return 0;
   }

   return 1;
}

int snmplite_ups_close(UPSINFO *ups)
{
   write_lock(ups);

   struct snmplite_ups_internal_data *sid = 
      (struct snmplite_ups_internal_data *)ups->driver_internal_data;

   sid->snmp->Close();
   delete sid->snmp;
   free(sid);
   ups->driver_internal_data = NULL;
   write_unlock(ups);
   return 1;
}

int snmplite_ups_setup(UPSINFO *ups)
{
   return 1;
}

bool snmplite_ups_check_ci(int ci, Snmp::Variable &data)
{
   // Sanity check a few values that SNMP UPSes claim to report but seem
   // to always come back as zeros.
   switch (ci)
   {
   // SmartUPS 1000 is returning 0 for this via SNMP so screen it out
   // in case this is a common issue.
   case CI_NOMBATTV:
      return data.u32 != 0;
   }

   return true;
}

int snmplite_ups_get_capabilities(UPSINFO *ups)
{
   struct snmplite_ups_internal_data *sid =
      (struct snmplite_ups_internal_data *)ups->driver_internal_data;

   write_lock(ups);

   // Walk the OID map, issuing an SNMP query for each item, one at a time.
   // If the query suceeds, sanity check the returned value and set the
   // capabilities flag.
   CiOidMap *mib = sid->strategy->mib;
   for (unsigned int i = 0; mib[i].ci != -1; i++)
   {
      Snmp::Variable data;
      if (mib[i].oid && sid->snmp->Get(mib[i].oid, &data))
      {
         ups->UPS_Cap[mib[i].ci] =
            snmplite_ups_check_ci(mib[i].ci, data);
      }
   }

   write_unlock(ups);

   // Succeed if we found CI_STATUS
   return ups->UPS_Cap[CI_STATUS];
}

int snmplite_ups_program_eeprom(UPSINFO *ups, int command, const char *data)
{
   return 0;
}

int snmplite_ups_kill_power(UPSINFO *ups)
{
   struct snmplite_ups_internal_data *sid =
      (struct snmplite_ups_internal_data *)ups->driver_internal_data;

   if (sid->strategy->killpower_func)
      return sid->strategy->killpower_func(ups);

   return 0;
}

int snmplite_ups_shutdown(UPSINFO *ups)
{
   struct snmplite_ups_internal_data *sid =
      (struct snmplite_ups_internal_data *)ups->driver_internal_data;

   if (sid->strategy->shutdown_func)
      return sid->strategy->shutdown_func(ups);

   return 0;
}

int snmplite_ups_check_state(UPSINFO *ups)
{
   struct snmplite_ups_internal_data *sid =
      (struct snmplite_ups_internal_data *)ups->driver_internal_data;

   if (sid->strategy->trapwait_func)
      sid->strategy->trapwait_func(ups);
   else
      sleep(ups->wait_time);

   return 1;
}

static int snmplite_ups_update_cis(UPSINFO *ups, bool dynamic)
{
   struct snmplite_ups_internal_data *sid =
      (struct snmplite_ups_internal_data *)ups->driver_internal_data;
   CiOidMap *mib = sid->strategy->mib;

   // Walk OID map and build a query for all parameters we have that
   // match the requested 'dynamic' setting
   Snmp::SnmpEngine::OidVar oidvar;
   alist<Snmp::SnmpEngine::OidVar> oids;
   for (unsigned int i = 0; mib[i].ci != -1; i++)
   {
      if (ups->UPS_Cap[mib[i].ci] && 
          mib[i].oid && mib[i].dynamic == dynamic)
      {
         oidvar.oid = mib[i].oid;
         oids.append(oidvar);
      }
   }

   // Issue the query, bail if it fails
   if (!sid->snmp->Get(oids))
      return 0;

   // Walk the OID map again to correlate results with CIs.
   // Invoke the update function to set the values.
   alist<Snmp::SnmpEngine::OidVar>::iterator iter = oids.begin();
   for (unsigned int i = 0; mib[i].ci != -1; i++)
   {
      if (ups->UPS_Cap[mib[i].ci] && 
          mib[i].oid && mib[i].dynamic == dynamic)
      {
         sid->strategy->update_ci_func(ups, mib[i].ci, (*iter).data);
         ++iter;
      }
   }
   
   return 1;
}

int snmplite_ups_read_volatile_data(UPSINFO *ups)
{
   struct snmplite_ups_internal_data *sid =
      (struct snmplite_ups_internal_data *)ups->driver_internal_data;

   write_lock(ups);

   int ret = snmplite_ups_update_cis(ups, true);

   time_t now = time(NULL);
   if (ret)
   {
      // Successful query
      sid->error_count = 0;
      ups->poll_time = now;    /* save time stamp */

      // If we were commlost, we're not any more
      if (ups->is_commlost())
      {
         ups->clear_commlost();
         generate_event(ups, CMDCOMMOK);
      }
   }
   else
   {
      // Query failed. Close and reopen SNMP to help recover.
      sid->snmp->Close();
      sid->snmp->Open(sid->host, sid->port, sid->community, 
                      sid->strategy->trapwait_func != NULL);

      if (ups->is_commlost())
      {
         // We already know we're commlost.
         // Log an event every 10 minutes.
         if ((now - sid->commlost_time) >= 10*60)
         {
            sid->commlost_time = now;
            log_event(ups, event_msg[CMDCOMMFAILURE].level,
               event_msg[CMDCOMMFAILURE].msg);            
         }
      }
      else
      {
         // Check to see if we've hit enough errors to declare commlost.
         // If we have, set commlost flag and log an event.
         if (++sid->error_count >= 3)
         {
            sid->commlost_time = now;
            ups->set_commlost();
            generate_event(ups, CMDCOMMFAILURE);
         }
      }
   }

   write_unlock(ups);
   return ret;
}

int snmplite_ups_read_static_data(UPSINFO *ups)
{
   write_lock(ups);
   int ret = snmplite_ups_update_cis(ups, false);
   write_unlock(ups);
   return ret;
}

int snmplite_ups_entry_point(UPSINFO *ups, int command, void *data)
{
   return 0;
}

void snmplite_trap_wait(UPSINFO *ups)
{
   struct snmplite_ups_internal_data *sid =
      (struct snmplite_ups_internal_data *)ups->driver_internal_data;

   // Simple trap handling: Any valid trap causes us to return and thus
   // new data will be fetched from the UPS.
   Snmp::TrapMessage *trap = sid->snmp->TrapWait(ups->wait_time * 1000);
   if (trap)
   {
      Dmsg2(80, "Got TRAP: generic=%d, specific=%d\n", 
         trap->Generic(), trap->Specific());
      delete trap;
   }
}
