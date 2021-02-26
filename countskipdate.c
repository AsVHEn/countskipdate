/*
    Countskipdate, a plugin for the DeaDBeeF audio player

    Based on Volume Meter plugin from Christian Boxdörfer <christian.boxdoerfer@posteo.de>

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <gtk/gtk.h>

#include "deadbeef.h"
#include "fastftoi.h"


#define MAX_CHANNELS 2


/* Global variables */

static const char *RATING = "RATING";
static const char *SKIP = "SKIP";
static const char *PLAY_COUNTER = "PLAY_COUNTER";
static const char *FIRST_PLAYED = "FIRST_PLAYED";
static const char *LAST_PLAYED = "LAST_PLAYED";

static DB_misc_t plugin;
static DB_functions_t *deadbeef    = NULL;
static gboolean scan_start_blocked = FALSE;
static gboolean scan_end_blocked   = FALSE;
static intptr_t mutex              = 0;

float skip_start = 0;
float skip_finish = 0;
int milestones[100] = {};
int mils = 0;
int count;
DB_playItem_t *it;
static DB_playItem_t *finished_song;





static void countskipdate_wavedata_listener( void *ctx, ddb_audio_data_t *data )
{

    deadbeef->mutex_lock( mutex );
    //printf( "dB: %f / start_dB: %f    middle_dB: %f     end_dB: %f \n", dB, dB_Threshold_Value_Start, dB_Threshold_Value_Middle, dB_Threshold_Value_End );

    //DB_playItem_t *it = deadbeef->streamer_get_playing_track();
    //float length      = deadbeef->pl_get_item_duration( it );
    //float pos         = deadbeef->streamer_get_playpos();
    //deadbeef->pl_item_unref( it );

    float percent     = deadbeef->playback_get_pos();

    DB_playItem_t *it = deadbeef->streamer_get_playing_track();
    float length      = deadbeef->pl_get_item_duration( it );
    float pos         = deadbeef->streamer_get_playpos();
    count             = deadbeef->pl_find_meta_int(it, PLAY_COUNTER, -1);
    int rate          = deadbeef->pl_find_meta_int(it, RATING, -1);
    const char *skip  = deadbeef->pl_find_meta (it, SKIP);
    int per = (percent + 0.5);
    milestones[per] = 1;

    if (skip != NULL && scan_start_blocked == FALSE)
    {
        char kip[17]={};
        strcpy(kip, skip);
		if ( kip[2] == ':' && kip[5] == ':'  && kip[8] == '-' && kip[11] == ':' && kip[14] == ':' )
        {
            skip_start = (kip[0] - 48)*36000 + (kip[1] - 48)*3600 + (kip[3] - 48)*600 + (kip[4] - 48)*60 + (kip[6] - 48)*10 + (kip[7] - 48);
            skip_finish = (kip[9] - 48)*36000 + (kip[10] - 48)*3600 + (kip[12] - 48)*600 + (kip[13] - 48)*60 + (kip[15] - 48)*10 + (kip[16] - 48);
        }
        else if ( kip[0] == '-' && kip[3] == ':'  && kip[6] == ':' )
        {
            skip_finish = (kip[1] - 48)*36000 + (kip[2] - 48)*3600 + (kip[4] - 48)*600 + (kip[5] - 48)*60 + (kip[7] - 48)*10 + (kip[8] - 48);
        }
    }

    if ( rate == 1 )
    {
		deadbeef->sendmessage( DB_EV_NEXT, 0, 0, 0 );
    }
	else if ( skip_start < pos && skip_finish > pos )
    {
        deadbeef->playback_set_pos( ( skip_finish/length*100 ) );
    }

    deadbeef->pl_item_unref( it );

    deadbeef->mutex_unlock( mutex );
}


static int countskipdate_connect( void )
{
    if (mutex == 0) mutex = deadbeef->mutex_create();
    deadbeef->vis_waveform_listen( NULL, countskipdate_wavedata_listener );

    return 0;
}

static int handle_event( uint32_t current_event, uintptr_t ctx, uint32_t p1, uint32_t p2 )
{

    if ( current_event == DB_EV_SONGFINISHED)
    {
		skip_start = 0;
		skip_finish = 0;
		mils = 0;
        finished_song = ((ddb_event_track_t *) ctx)->track;
        count  = deadbeef->pl_find_meta_int (finished_song, PLAY_COUNTER,0);
        for (int i = 0; i < 100; i = i + 1)
        {
            mils += milestones[i];
            milestones[i] = 0;


        }

        // if song have been played more than 50%, PLAY_COUNTER is updated and played times.
        if (mils > 50)
        {
            deadbeef->pl_lock();
            long lclock;
            struct tm *ltime;
            char new_last[50];

            time(&lclock);
            ltime = localtime(&lclock);
            sprintf(new_last, "%d-%02d-%02d %02d:%02d:%02d",ltime->tm_year + 1900, ltime->tm_mon + 1, ltime->tm_mday, ltime->tm_hour, ltime->tm_min, ltime->tm_sec );

            deadbeef->pl_set_meta_int(finished_song, PLAY_COUNTER, count + 1);
            const char *last  = deadbeef->pl_find_meta (finished_song, LAST_PLAYED);
            const char *first  = deadbeef->pl_find_meta (finished_song, FIRST_PLAYED);
            const char *track_location = deadbeef->pl_find_meta(finished_song, ":URI");
            printf("%s \n", track_location);
            if (last)
            {
                deadbeef->pl_delete_meta(finished_song, LAST_PLAYED);
				printf(" Deleted last: %s \n", LAST_PLAYED);
            }
            deadbeef->pl_add_meta(finished_song, LAST_PLAYED, new_last);
			printf("Added last: %s \n", new_last);

            if (!first)
            {
                deadbeef->pl_add_meta(finished_song, FIRST_PLAYED, new_last);
				printf("Added first: %s \n", FIRST_PLAYED);
            }
            //Rutine to save metadata.
            const char *dec = deadbeef->pl_find_meta_raw(finished_song, ":DECODER");
            char decoder_id[100];

            if (dec)
            {
                strncpy(decoder_id, dec, sizeof(decoder_id));
            }
            int match = finished_song && dec;
            if (match)
            {
                DB_decoder_t *dec = NULL;
                DB_decoder_t **decoders = deadbeef->plug_get_decoder_list();
                for (int i = 0; decoders[i]; i++)
                {
                    if (!strcmp(decoders[i]->plugin.id, decoder_id))
                    {
                        dec = decoders[i];
                        if (dec->write_metadata)
                        {
                            dec->write_metadata(finished_song);
							printf("Saved metadata: %i \n", i);
                        }
                            break;
                    }
                }
            }
            deadbeef->pl_unlock();
        }
    }

    // Reset variables at the start of the (next) song
    if ( current_event == DB_EV_SONGSTARTED )
    {
        scan_start_blocked = FALSE;
        scan_end_blocked   = FALSE;
        countskipdate_connect();
    return 0;

    }


    return 0;
}



static DB_misc_t plugin = {
    .plugin.type          = DB_PLUGIN_MISC,
    .plugin.api_vmajor    = 1,
    .plugin.api_vminor    = 5,
    .plugin.version_major = 1,
    .plugin.version_minor = 0,
    .plugin.id            = "countskipdate",
    .plugin.name          = "CountSkipDate",
    .plugin.descr         = "The plugin count song plays, skips determined parts and adds first and last date of playback.",
    .plugin.copyright     = "Copyright (C) 2021 AsVHEn\n"
                        "\n"
                        "This program is free software; you can redistribute it and/or\n"
                        "modify it under the terms of the GNU General Public License\n"
                        "as published by the Free Software Foundation; either version 3\n"
                        "of the License, or (at your option) any later version.\n"
                        "\n"
                        "This program is distributed in the hope that it will be useful,\n"
                        "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
                        "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
                        "GNU General Public License for more details.\n"
                        "\n"
                        "You should have received a copy of the GNU General Public License\n"
                        "along with this program; if not, write to the Free Software\n"
                        "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.\n",
    .plugin.website      = "https://github.com/AsVHEn/countskipdate",
    .plugin.disconnect   = NULL,
    .plugin.message      = handle_event,
};


DB_plugin_t *countskipdate_load( DB_functions_t *ddb )
{
    deadbeef = ddb;
    return &plugin.plugin;
}
