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
#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif
#include "deadbeef.h"


#define MAX_CHANNELS 2


/* Global variables */

static const char *RATING = "RATING";
static const char *SKIP = "SKIP";
static const char *PLAY_COUNTER = "PLAY_COUNTER";
static const char *FIRST_PLAYED = "FIRST_PLAYED";
static const char *LAST_PLAYED = "LAST_PLAYED";

static DB_misc_t plugin;
static DB_functions_t *deadbeef    = NULL;
static intptr_t mutex              = 0;

int milestones[100] = {};
int mils = 0;
int count;
DB_playItem_t *it;
static DB_playItem_t *finished_song;

struct skiptime{
	float start;
	float finish;
};

struct skiptime bounds;

static const char settings_dlg[] = "property \"Skip songs with a rate less or equal as: (-1 to deactivate) \" spinbtn[-1,5,1] countskipdate.ratebound 1 \";";

struct skiptime timereader(const char *skip) {
	float skip_start = 1;
	float skip_finish = 0;
	if (skip != NULL) {

		if ( skip[2] == ':' && skip[5] == ':'  && skip[8] == '-' && skip[11] == ':' && skip[14] == ':' ){
            skip_start = (skip[0] - 48)*36000 + (skip[1] - 48)*3600 + (skip[3] - 48)*600 + (skip[4] - 48)*60 + (skip[6] - 48)*10 + (skip[7] - 48);
            skip_finish = (skip[9] - 48)*36000 + (skip[10] - 48)*3600 + (skip[12] - 48)*600 + (skip[13] - 48)*60 + (skip[15] - 48)*10 + (skip[16] - 48);
		}
		else if ( skip[0] == '-' && skip[3] == ':'  && skip[6] == ':' ){
			skip_finish = (skip[1] - 48)*36000 + (skip[2] - 48)*3600 + (skip[4] - 48)*600 + (skip[5] - 48)*60 + (skip[7] - 48)*10 + (skip[8] - 48);
		}
	}
	struct skiptime parsed = {skip_start, skip_finish};
	return parsed;
}

static void countskipdate_wavedata_listener( void *ctx, ddb_audio_data_t *data )
{
    float percent = deadbeef->playback_get_pos();

    if (percent <= 0.0){
		return;
	}

    deadbeef->mutex_lock(mutex);
    //printf( "dB: %f / start_dB: %f    middle_dB: %f     end_dB: %f \n", dB, dB_Threshold_Value_Start, dB_Threshold_Value_Middle, dB_Threshold_Value_End );

    //DB_playItem_t *it = deadbeef->streamer_get_playing_track();
    //float length      = deadbeef->pl_get_item_duration( it );
    //float pos         = deadbeef->streamer_get_playpos();
    //deadbeef->pl_item_unref( it );

    //float percent     = deadbeef->playback_get_pos();

    it = deadbeef->streamer_get_playing_track();
	//printf("Length: %f", length);
	//printf(" Pos: %f",pos);
    //count             = deadbeef->pl_find_meta_int(it, PLAY_COUNTER, -1);
	//printf(" Count: %i",count);
    //int rate          = deadbeef->pl_find_meta_int(it, RATING, -1);
	//printf(" Rate: %i \n",rate);

    milestones[(int)(deadbeef->playback_get_pos() + 0.5)] = 1;
	deadbeef->pl_lock ();
	bounds = timereader(deadbeef->pl_find_meta (it, SKIP));
	int track_rating = deadbeef->pl_find_meta_int(it, RATING, -1);
	deadbeef->pl_unlock ();


    if ( (track_rating != -1) && (track_rating != 0) && (track_rating <= deadbeef->conf_get_int("countskipdate.ratebound", -1)) ){
		deadbeef->playback_set_pos(100);
    }

	else if (bounds.start < deadbeef->streamer_get_playpos() && bounds.finish > deadbeef->streamer_get_playpos()){
        deadbeef->playback_set_pos((bounds.finish/deadbeef->pl_get_item_duration(it)*100));
    }

    deadbeef->pl_item_unref(it);
    deadbeef->mutex_unlock(mutex);
}


static int countskipdate_connect( void ){
    if (mutex == 0) {
		mutex = deadbeef->mutex_create();
	}
    deadbeef->vis_waveform_listen( NULL, countskipdate_wavedata_listener );
    return 0;
}

static int countskipdate_stop( void ){
	return 0;
}

static int handle_event( uint32_t current_event, uintptr_t ctx, uint32_t p1, uint32_t p2 ){
	//printf("Current event: %d \n", current_event);

    // Reset variables at the end of the song
    if ( current_event == DB_EV_SONGFINISHED){
		mils = 0;
        finished_song = ((ddb_event_track_t *) ctx)->track;
		//printf("Finished song duration: %f \n", deadbeef->pl_get_item_duration(finished_song));
	    deadbeef->pl_lock ();
        count  = deadbeef->pl_find_meta_int (finished_song, PLAY_COUNTER,0);
	    deadbeef->pl_unlock ();
        for (int i = 0; i < 100; i = i + 1){
            mils += milestones[i];
            milestones[i] = 0;
        }

		//printf("mils: %i \n", mils);
			

        // if the song have been played more than plugin.configdialog = 50%, PLAY_COUNTER is updated and played times.
        if ((mils > 50) || (deadbeef->pl_get_item_duration(finished_song) < 15)){
            long lclock;
            struct tm *ltime;
            char new_last[50];


            time(&lclock);
            ltime = localtime(&lclock);
            sprintf(new_last, "%d-%02d-%02d %02d:%02d:%02d",ltime->tm_year + 1900, ltime->tm_mon + 1, ltime->tm_mday, ltime->tm_hour, ltime->tm_min, ltime->tm_sec );
			deadbeef->pl_lock(); 
            deadbeef->pl_set_meta_int(finished_song, PLAY_COUNTER, count + 1);
            const char *last  = deadbeef->pl_find_meta (finished_song, LAST_PLAYED);
            const char *first  = deadbeef->pl_find_meta (finished_song, FIRST_PLAYED);
            const char *track_location = deadbeef->pl_find_meta(finished_song, ":URI");
			deadbeef->pl_unlock();
            //printf("%s \n", track_location);

            if (last){
                deadbeef->pl_delete_meta(finished_song, LAST_PLAYED);
				//printf(" Deleted last: %s \n", LAST_PLAYED);
            }
            deadbeef->pl_add_meta(finished_song, LAST_PLAYED, new_last);
			//printf("Added last: %s \n", new_last);

            if (!first){
                deadbeef->pl_add_meta(finished_song, FIRST_PLAYED, new_last);
				//printf("Added first: %s \n", FIRST_PLAYED);
            }

            //Rutine to save metadata.
			deadbeef->pl_lock(); 
            const char *dec = deadbeef->pl_find_meta_raw(finished_song, ":DECODER"); 
            char decoder_id[100];

            if (dec){
                strncpy(decoder_id, dec, sizeof(decoder_id));
            }
            int match = finished_song && dec;
			deadbeef->pl_unlock();
			//printf("Updating \n");

            if (match){
                DB_decoder_t *dec = NULL;
                DB_decoder_t **decoders = deadbeef->plug_get_decoder_list();

                for (int i = 0; decoders[i]; i++){

                    if (!strcmp(decoders[i]->plugin.id, decoder_id)){
                        dec = decoders[i];

                        if (dec->write_metadata) {
							//deadbeef->pl_lock();
                            dec->write_metadata(finished_song);
							//deadbeef->pl_unlock(); 
							//printf("Saved metadata: %i \n", i);
                        }
                     	break;
                    }
                }
            }
        }
	
    }
    if ( current_event == DB_EV_SONGSTARTED || current_event == DB_EV_PLUGINSLOADED ){
        countskipdate_connect();
		//printf("Reconnecting \n");
    }
	//printf("Updated \n");
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
                        "modify it undlear cacheer the terms of the GNU General Public License\n"
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
    .plugin.connect      = countskipdate_connect,
    .plugin.stop         = countskipdate_stop,
    .plugin.disconnect   = NULL,
    .plugin.message      = handle_event,
    .plugin.configdialog = settings_dlg,
};

DB_plugin_t *countskipdate_load( DB_functions_t *ddb ){
    deadbeef = ddb;
	//bindtextdomain("deadbeef-countskipdate", "/usr/share/locale");
	//textdomain("deadbeef-countskipdate");
	return &plugin.plugin;
}
