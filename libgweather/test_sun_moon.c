/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 4 -*- */

#include <glib.h>
#include <string.h>
#include <time.h>

#include "gweather-private.h"

int
main (int argc, char **argv)
{
    GWeatherInfo   *info;
    GWeatherInfoPrivate *priv;
    GOptionContext* context;
    GError*         error = NULL;
    gdouble         latitude, longitude;
    gchar*          gtime = NULL;
    GDate           gdate;
    struct tm       tm;
    time_t          phases[4];
    const GOptionEntry entries[] = {
	{ "latitude", 0, 0, G_OPTION_ARG_DOUBLE, &latitude,
	  "observer's latitude in degrees north", NULL },
	{ "longitude", 0, 0,  G_OPTION_ARG_DOUBLE, &longitude,
	  "observer's longitude in degrees east", NULL },
	{ "time", 0, 0, G_OPTION_ARG_STRING, &gtime,
	  "time in seconds from Unix epoch", NULL },
	{ NULL }
    };

    context = g_option_context_new ("- test libgweather sun/moon calculations");
    g_option_context_add_main_entries (context, entries, NULL);
    g_option_context_parse (context, &argc, &argv, &error);

    if (error) {
	perror (error->message);
	return error->code;
    }
    else if (latitude < -90. || latitude > 90.) {
	perror ("invalid latitude: should be [-90 .. 90]");
	return -1;
    } else if (longitude < -180. || longitude > 180.) {
	perror ("invalid longitude: should be [-180 .. 180]");
	return -1;
    }

    info = g_object_new (GWEATHER_TYPE_INFO, NULL);
    priv = info->priv;
    priv->location.latitude = DEGREES_TO_RADIANS(latitude);
    priv->location.longitude = DEGREES_TO_RADIANS(longitude);
    priv->location.latlon_valid = TRUE;
    priv->valid = TRUE;

    if (gtime != NULL) {
	//	printf(" gtime=%s\n", gtime);
	g_date_set_parse(&gdate, gtime);
	g_date_to_struct_tm(&gdate, &tm);
	priv->current_time = mktime(&tm);
    } else {
	priv->current_time = time(NULL);
    }

    _gweather_info_ensure_sun (info);
    _gweather_info_ensure_moon (info);

    printf ("  Latitude %7.3f %c  Longitude %7.3f %c for %s  All times UTC\n",
	    fabs(latitude), (latitude >= 0. ? 'N' : 'S'),
	    fabs(longitude), (longitude >= 0. ? 'E' : 'W'),
	    asctime(gmtime(&priv->current_time)));
    printf("daytime:   %s\n", gweather_info_is_daytime(info) ? "yes" : "no");
    printf("sunrise:   %s",
	   (priv->sunriseValid ? ctime(&priv->sunrise) : "(invalid)\n"));
    printf("sunset:    %s",
	   (priv->sunsetValid ? ctime(&priv->sunset)  : "(invalid)\n"));
    if (priv->moonValid) {
	printf("moonphase: %g\n", priv->moonphase);
	printf("moonlat:   %g\n", priv->moonlatitude);

	if (gweather_info_get_upcoming_moonphases(info, phases)) {
	    printf("    New:   %s", asctime(gmtime(&phases[0])));
	    printf("    1stQ:  %s", asctime(gmtime(&phases[1])));
	    printf("    Full:  %s", asctime(gmtime(&phases[2])));
	    printf("    3rdQ:  %s", asctime(gmtime(&phases[3])));
	}
    }
    return 0;
}
