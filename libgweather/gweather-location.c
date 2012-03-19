/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* gweather-location.c - Location-handling code
 *
 * Copyright 2008, Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <math.h>
#include <locale.h>
#include <gtk/gtk.h>
#include <libxml/xmlreader.h>

#define GWEATHER_I_KNOW_THIS_IS_UNSTABLE
#include "gweather-location.h"
#include "gweather-timezone.h"
#include "parser.h"
#include "weather-priv.h"

/**
 * SECTION:gweather-location
 * @Title: GWeatherLocation
 *
 * A #GWeatherLocation represents a "location" of some type known to
 * libgweather; anything from a single weather station to the entire
 * world. See #GWeatherLocationLevel for information about how the
 * hierarchy of locations works.
 */

struct _GWeatherLocation {
    char *name, *sort_name;
    GWeatherLocation *parent, **children;
    GWeatherLocationLevel level;
    char *country_code, *tz_hint;
    char *station_code, *forecast_zone, *radar;
    double latitude, longitude;
    gboolean latlon_valid;
    GWeatherTimezone **zones;
    GHashTable *metar_code_cache;

    int ref_count;
};

/**
 * GWeatherLocationLevel:
 * @GWEATHER_LOCATION_WORLD: A location representing the entire world.
 * @GWEATHER_LOCATION_REGION: A location representing a continent or
 * other top-level region.
 * @GWEATHER_LOCATION_COUNTRY: A location representing a "country" (or
 * other geographic unit that has an ISO-3166 country code)
 * @GWEATHER_LOCATION_ADM1: A location representing a "first-level
 * administrative division"; ie, a state, province, or similar
 * division.
 * @GWEATHER_LOCATION_ADM2: A location representing a subdivision of a
 * %GWEATHER_LOCATION_ADM1 location. (Not currently used.)
 * @GWEATHER_LOCATION_CITY: A location representing a city
 * @GWEATHER_LOCATION_WEATHER_STATION: A location representing a
 * weather station.
 *
 * The size/scope of a particular #GWeatherLocation.
 *
 * Locations form a hierarchy, with a %GWEATHER_LOCATION_WORLD
 * location at the top, divided into regions or countries, and so on.
 * Countries may or may not be divided into "adm1"s, and "adm1"s may
 * or may not be divided into "adm2"s. A city will have at least one,
 * and possibly several, weather stations inside it. Weather stations
 * will never appear outside of cities.
 **/

static int
sort_locations_by_name (gconstpointer a, gconstpointer b)
{
    GWeatherLocation *loc_a = *(GWeatherLocation **)a;
    GWeatherLocation *loc_b = *(GWeatherLocation **)b;

    return g_utf8_collate (loc_a->sort_name, loc_b->sort_name);
}
 
static int
sort_locations_by_distance (gconstpointer a, gconstpointer b, gpointer user_data)
{
    GWeatherLocation *loc_a = *(GWeatherLocation **)a;
    GWeatherLocation *loc_b = *(GWeatherLocation **)b;
    GWeatherLocation *city = (GWeatherLocation *)user_data;
    double dist_a, dist_b;

    dist_a = gweather_location_get_distance (loc_a, city);
    dist_b = gweather_location_get_distance (loc_b, city);
    if (dist_a < dist_b)
	return -1;
    else if (dist_a > dist_b)
	return 1;
    else
	return 0;
}

static gboolean
parse_coordinates (const char *coordinates,
		   double *latitude, double *longitude)
{
    char *p;

    *latitude = g_ascii_strtod (coordinates, &p) * M_PI / 180.0;
    if (p == (char *)coordinates)
	return FALSE;
    if (*p++ != ' ')
	return FALSE;
    *longitude = g_ascii_strtod (p, &p) * M_PI / 180.0;
    return !*p;
}

static GWeatherLocation *
location_new_from_xml (GWeatherParser *parser, GWeatherLocationLevel level,
		       GWeatherLocation *parent)
{
    GWeatherLocation *loc, *child;
    GPtrArray *children = NULL;
    const char *tagname;
    char *value, *normalized;
    int tagtype, i;

    loc = g_slice_new0 (GWeatherLocation);
    loc->latitude = loc->longitude = DBL_MAX;
    loc->parent = parent;
    loc->level = level;
    loc->ref_count = 1;
    if (level == GWEATHER_LOCATION_WORLD)
	loc->metar_code_cache = g_hash_table_ref (parser->metar_code_cache);
    children = g_ptr_array_new ();

    if (xmlTextReaderRead (parser->xml) != 1)
	goto error_out;
    while ((tagtype = xmlTextReaderNodeType (parser->xml)) !=
	   XML_READER_TYPE_END_ELEMENT) {
	if (tagtype != XML_READER_TYPE_ELEMENT) {
	    if (xmlTextReaderRead (parser->xml) != 1)
		goto error_out;
	    continue;
	}

	tagname = (const char *) xmlTextReaderConstName (parser->xml);
	if (!strcmp (tagname, "name") && !loc->name) {
	    value = gweather_parser_get_localized_value (parser);
	    if (!value)
		goto error_out;
	    loc->name = g_strdup (value);
	    xmlFree (value);
	    normalized = g_utf8_normalize (loc->name, -1, G_NORMALIZE_ALL);
	    loc->sort_name = g_utf8_casefold (normalized, -1);
	    g_free (normalized);

	} else if (!strcmp (tagname, "iso-code") && !loc->country_code) {
	    value = gweather_parser_get_value (parser);
	    if (!value)
		goto error_out;
	    loc->country_code = g_strdup (value);
	    xmlFree (value);
	} else if (!strcmp (tagname, "tz-hint") && !loc->tz_hint) {
	    value = gweather_parser_get_value (parser);
	    if (!value)
		goto error_out;
	    loc->tz_hint = g_strdup (value);
	    xmlFree (value);
	} else if (!strcmp (tagname, "code") && !loc->station_code) {
	    value = gweather_parser_get_value (parser);
	    if (!value)
		goto error_out;
	    loc->station_code = g_strdup (value);
	    xmlFree (value);
	} else if (!strcmp (tagname, "coordinates") && !loc->latlon_valid) {
	    value = gweather_parser_get_value (parser);
	    if (!value)
		goto error_out;
	    if (parse_coordinates (value, &loc->latitude, &loc->longitude))
		loc->latlon_valid = TRUE;
	    xmlFree (value);
	} else if (!strcmp (tagname, "zone") && !loc->forecast_zone) {
	    value = gweather_parser_get_value (parser);
	    if (!value)
		goto error_out;
	    loc->forecast_zone = g_strdup (value);
	    xmlFree (value);
	} else if (!strcmp (tagname, "radar") && !loc->radar) {
	    value = gweather_parser_get_value (parser);
	    if (!value)
		goto error_out;
	    loc->radar = g_strdup (value);
	    xmlFree (value);

	} else if (!strcmp (tagname, "region")) {
	    child = location_new_from_xml (parser, GWEATHER_LOCATION_REGION, loc);
	    if (!child)
		goto error_out;
	    if (parser->use_regions)
		g_ptr_array_add (children, child);
	    else {
		if (child->children) {
		    for (i = 0; child->children[i]; i++)
			g_ptr_array_add (children, gweather_location_ref (child->children[i]));
		}
		gweather_location_unref (child);
	    }
	} else if (!strcmp (tagname, "country")) {
	    child = location_new_from_xml (parser, GWEATHER_LOCATION_COUNTRY, loc);
	    if (!child)
		goto error_out;
	    g_ptr_array_add (children, child);
	} else if (!strcmp (tagname, "state")) {
	    child = location_new_from_xml (parser, GWEATHER_LOCATION_ADM1, loc);
	    if (!child)
		goto error_out;
	    g_ptr_array_add (children, child);
	} else if (!strcmp (tagname, "city")) {
	    child = location_new_from_xml (parser, GWEATHER_LOCATION_CITY, loc);
	    if (!child)
		goto error_out;
	    g_ptr_array_add (children, child);
	} else if (!strcmp (tagname, "location")) {
	    child = location_new_from_xml (parser, GWEATHER_LOCATION_WEATHER_STATION, loc);
	    if (!child)
		goto error_out;
	    g_ptr_array_add (children, child);

	} else if (!strcmp (tagname, "timezones")) {
	    loc->zones = gweather_timezones_parse_xml (parser);
	    if (!loc->zones)
		goto error_out;

	} else {
	    if (xmlTextReaderNext (parser->xml) != 1)
		goto error_out;
	}
    }
    if (xmlTextReaderRead (parser->xml) != 1 && parent)
	goto error_out;

    if (level == GWEATHER_LOCATION_WEATHER_STATION) {
	/* Cache weather stations by METAR code */
	g_hash_table_replace (parser->metar_code_cache, loc->station_code, gweather_location_ref (loc));
    }

    if (children->len) {
	if (level == GWEATHER_LOCATION_CITY)
	    g_ptr_array_sort_with_data (children, sort_locations_by_distance, loc);
	else
	    g_ptr_array_sort (children, sort_locations_by_name);

	g_ptr_array_add (children, NULL);
	loc->children = (GWeatherLocation **)g_ptr_array_free (children, FALSE);
    } else
	g_ptr_array_free (children, TRUE);

    return loc;

error_out:
    gweather_location_unref (loc);
    for (i = 0; i < children->len; i++)
	gweather_location_unref (children->pdata[i]);
    g_ptr_array_free (children, TRUE);

    return NULL;
}

/**
 * gweather_location_new_world:
 * @use_regions: whether or not to divide the world into regions
 *
 * Creates a new #GWeatherLocation of type %GWEATHER_LOCATION_WORLD,
 * representing a hierarchy containing all of the locations from
 * Locations.xml.
 *
 * If @use_regions is %TRUE, the immediate children of the returned
 * location will be %GWEATHER_LOCATION_REGION nodes, representing the
 * top-level "regions" of Locations.xml (the continents and a few
 * other divisions), and the country-level nodes will be the children
 * of the regions. If @use_regions is %FALSE, the regions will be
 * skipped, and the children of the returned location will be the
 * %GWEATHER_LOCATION_COUNTRY nodes.
 *
 * Return value: (allow-none): a %GWEATHER_LOCATION_WORLD location, or
 * %NULL if Locations.xml could not be found or could not be parsed.
 **/
GWeatherLocation *
gweather_location_new_world (gboolean use_regions)
{
    GWeatherParser *parser;
    GWeatherLocation *world;

    parser = gweather_parser_new (use_regions);
    if (!parser)
	return NULL;

    world = location_new_from_xml (parser, GWEATHER_LOCATION_WORLD, NULL);

    gweather_parser_free (parser);
    return world;
}

/**
 * gweather_location_ref:
 * @loc: a #GWeatherLocation
 *
 * Adds 1 to @loc's reference count.
 *
 * Return value: @loc
 **/
GWeatherLocation *
gweather_location_ref (GWeatherLocation *loc)
{
    g_return_val_if_fail (loc != NULL, NULL);

    loc->ref_count++;
    return loc;
}

/**
 * gweather_location_unref:
 * @loc: a #GWeatherLocation
 *
 * Subtracts 1 from @loc's reference count, and frees it if the
 * reference count reaches 0.
 **/
void
gweather_location_unref (GWeatherLocation *loc)
{
    int i;

    g_return_if_fail (loc != NULL);

    if (--loc->ref_count)
	return;
    
    g_free (loc->name);
    g_free (loc->sort_name);
    g_free (loc->country_code);
    g_free (loc->tz_hint);
    g_free (loc->station_code);
    g_free (loc->forecast_zone);
    g_free (loc->radar);

    if (loc->children) {
	for (i = 0; loc->children[i]; i++) {
	    loc->children[i]->parent = NULL;
	    gweather_location_unref (loc->children[i]);
	}
	g_free (loc->children);
    }

    if (loc->zones) {
	for (i = 0; loc->zones[i]; i++)
	    gweather_timezone_unref (loc->zones[i]);
	g_free (loc->zones);
    }

    if (loc->metar_code_cache)
	g_hash_table_unref (loc->metar_code_cache);

    g_slice_free (GWeatherLocation, loc);
}

GType
gweather_location_get_type (void)
{
    static volatile gsize type_volatile = 0;

    if (g_once_init_enter (&type_volatile)) {
	GType type = g_boxed_type_register_static (
	    g_intern_static_string ("GWeatherLocation"),
	    (GBoxedCopyFunc) gweather_location_ref,
	    (GBoxedFreeFunc) gweather_location_unref);
	g_once_init_leave (&type_volatile, type);
    }
    return type_volatile;
}

/**
 * gweather_location_get_name:
 * @loc: a #GWeatherLocation
 *
 * Gets @loc's name, localized into the current language.
 *
 * Note that %GWEATHER_LOCATION_WEATHER_STATION nodes are not
 * localized, and so the name returned for those nodes will always be
 * in English, and should therefore not be displayed to the user.
 * (FIXME: should we just not return a name?)
 *
 * Return value: @loc's name
 **/
const char *
gweather_location_get_name (GWeatherLocation *loc)
{
    g_return_val_if_fail (loc != NULL, NULL);
    return loc->name;
}

/**
 * gweather_location_get_sort_name:
 * @loc: a #GWeatherLocation
 *
 * Gets @loc's "sort name", which is the name after having
 * g_utf8_normalize() (with %G_NORMALIZE_ALL) and g_utf8_casefold()
 * called on it. You can use this to sort locations, or to comparing
 * user input against a location name.
 *
 * Return value: @loc's sort name
 **/
const char *
gweather_location_get_sort_name (GWeatherLocation *loc)
{
    g_return_val_if_fail (loc != NULL, NULL);
    return loc->sort_name;
}

/**
 * gweather_location_get_level:
 * @loc: a #GWeatherLocation
 *
 * Gets @loc's level, from %GWEATHER_LOCATION_WORLD, to
 * %GWEATHER_LOCATION_WEATHER_STATION.
 *
 * Return value: @loc's level
 **/
GWeatherLocationLevel
gweather_location_get_level (GWeatherLocation *loc)
{
    g_return_val_if_fail (loc != NULL, GWEATHER_LOCATION_WORLD);
    return loc->level;
}

/**
 * gweather_location_get_parent:
 * @loc: a #GWeatherLocation
 *
 * Gets @loc's parent location.
 *
 * Return value: (transfer none) (allow-none): @loc's parent, or %NULL
 * if @loc is a %GWEATHER_LOCATION_WORLD node.
 **/
GWeatherLocation *
gweather_location_get_parent (GWeatherLocation *loc)
{
    g_return_val_if_fail (loc != NULL, NULL);
    return loc->parent;
}

/**
 * gweather_location_get_children:
 * @loc: a #GWeatherLocation
 *
 * Gets an array of @loc's children; this is owned by @loc and will
 * not remain valid if @loc is freed.
 *
 * Return value: (transfer none) (array zero-terminated=1): @loc's
 * children. (May be empty, but will not be %NULL.)
 **/
GWeatherLocation **
gweather_location_get_children (GWeatherLocation *loc)
{
    static GWeatherLocation *no_children = NULL;

    g_return_val_if_fail (loc != NULL, NULL);

    if (loc->children)
	return loc->children;
    else
	return &no_children;
}


/**
 * gweather_location_free_children:
 * @loc: a #GWeatherLocation
 * @children: an array of @loc's children
 *
 * This is a no-op. Do not use it.
 *
 * Deprecated: This is a no-op.
 **/
void
gweather_location_free_children (GWeatherLocation  *loc,
				 GWeatherLocation **children)
{
    ;
}

/**
 * gweather_location_has_coords:
 * @loc: a #GWeatherLocation
 *
 * Checks if @loc has valid latitude and longitude.
 *
 * Return value: %TRUE if @loc has valid latitude and longitude.
 **/
gboolean
gweather_location_has_coords (GWeatherLocation *loc)
{
    g_return_val_if_fail (loc != NULL, FALSE);
    return loc->latlon_valid;
}

/**
 * gweather_location_get_coords:
 * @loc: a #GWeatherLocation
 * @latitude: (out): on return will contain @loc's latitude
 * @longitude: (out): on return will contain @loc's longitude
 *
 * Gets @loc's coordinates; you must check
 * gweather_location_has_coords() before calling this.
 **/
void
gweather_location_get_coords (GWeatherLocation *loc,
			      double *latitude, double *longitude)
{
    //g_return_if_fail (loc->latlon_valid);
    g_return_if_fail (loc != NULL);
    g_return_if_fail (latitude != NULL);
    g_return_if_fail (longitude != NULL);

    *latitude = loc->latitude / M_PI * 180.0;
    *longitude = loc->longitude / M_PI * 180.0;
}

/**
 * gweather_location_get_distance:
 * @loc: a #GWeatherLocation
 * @loc2: a second #GWeatherLocation
 *
 * Determines the distance in kilometers between @loc and @loc2.
 *
 * Return value: the distance between @loc and @loc2.
 **/
double
gweather_location_get_distance (GWeatherLocation *loc, GWeatherLocation *loc2)
{
    /* average radius of the earth in km */
    static const double radius = 6372.795;

    g_return_val_if_fail (loc != NULL, 0);
    g_return_val_if_fail (loc2 != NULL, 0);

    //g_return_val_if_fail (loc->latlon_valid, 0.0);
    //g_return_val_if_fail (loc2->latlon_valid, 0.0);

    return acos (cos (loc->latitude) * cos (loc2->latitude) * cos (loc->longitude - loc2->longitude) +
		 sin (loc->latitude) * sin (loc2->latitude)) * radius;
}

/**
 * gweather_location_get_country:
 * @loc: a #GWeatherLocation
 *
 * Gets the ISO 3166 country code of @loc (or %NULL if @loc is a
 * region- or world-level location)
 *
 * Return value: (allow-none): @loc's country code (or %NULL if @loc
 * is a region- or world-level location)
 **/
const char *
gweather_location_get_country (GWeatherLocation *loc)
{
    g_return_val_if_fail (loc != NULL, NULL);

    while (loc->parent && !loc->country_code)
	loc = loc->parent;
    return loc->country_code;
}

/**
 * gweather_location_get_timezone:
 * @loc: a #GWeatherLocation
 *
 * Gets the timezone associated with @loc, if known.
 *
 * The timezone is owned either by @loc or by one of its parents.
 * FIXME.
 *
 * Return value: (transfer none) (allow-none): @loc's timezone, or
 * %NULL
 **/
GWeatherTimezone *
gweather_location_get_timezone (GWeatherLocation *loc)
{
    const char *tz_hint;
    int i;

    g_return_val_if_fail (loc != NULL, NULL);

    while (loc && !loc->tz_hint)
	loc = loc->parent;
    if (!loc)
	return NULL;
    tz_hint = loc->tz_hint;

    while (loc) {
	while (loc && !loc->zones)
	    loc = loc->parent;
	if (!loc)
	    return NULL;
	for (i = 0; loc->zones[i]; i++) {
	    if (!strcmp (tz_hint, gweather_timezone_get_tzid (loc->zones[i])))
		return loc->zones[i];
	}
	loc = loc->parent;
    }

    return NULL;
}

static void
add_timezones (GWeatherLocation *loc, GPtrArray *zones)
{
    int i;

    if (loc->zones) {
	for (i = 0; loc->zones[i]; i++)
	    g_ptr_array_add (zones, gweather_timezone_ref (loc->zones[i]));
    }
    if (loc->level < GWEATHER_LOCATION_COUNTRY && loc->children) {
	for (i = 0; loc->children[i]; i++)
	    add_timezones (loc->children[i], zones);
    }
}

/**
 * gweather_location_get_timezones:
 * @loc: a #GWeatherLocation
 *
 * Gets an array of all timezones associated with any location under
 * @loc. You can use gweather_location_free_timezones() to free this
 * array.
 *
 * Return value: (transfer full) (array zero-terminated=1): an array
 * of timezones. May be empty but will not be %NULL.
 **/
GWeatherTimezone **
gweather_location_get_timezones (GWeatherLocation *loc)
{
    GPtrArray *zones;

    g_return_val_if_fail (loc != NULL, NULL);

    zones = g_ptr_array_new ();
    add_timezones (loc, zones);
    g_ptr_array_add (zones, NULL);
    return (GWeatherTimezone **)g_ptr_array_free (zones, FALSE);
}

/**
 * gweather_location_free_timezones:
 * @loc: a #GWeatherLocation
 * @zones: an array returned from gweather_location_get_timezones()
 *
 * Frees the array of timezones returned by
 * gweather_location_get_timezones().
 **/
void
gweather_location_free_timezones (GWeatherLocation  *loc,
				  GWeatherTimezone **zones)
{
    int i;

    g_return_if_fail (loc != NULL);
    g_return_if_fail (zones != NULL);

    for (i = 0; zones[i]; i++)
	gweather_timezone_unref (zones[i]);
    g_free (zones);
}

/**
 * gweather_location_get_code:
 * @loc: a #GWeatherLocation
 *
 * Gets the METAR station code associated with a
 * %GWEATHER_LOCATION_WEATHER_STATION location.
 *
 * Return value: (allow-none): @loc's METAR station code, or %NULL
 **/
const char *
gweather_location_get_code (GWeatherLocation *loc)
{
    g_return_val_if_fail (loc != NULL, NULL);
    return loc->station_code;
}

/**
 * gweather_location_get_city_name:
 * @loc: a #GWeatherLocation
 *
 * For a %GWEATHER_LOCATION_CITY location, this is equivalent to
 * gweather_location_get_name(). For a
 * %GWEATHER_LOCATION_WEATHER_STATION location, it is equivalent to
 * calling gweather_location_get_name() on the location's parent. For
 * other locations it will return %NULL.
 *
 * Return value: (allow-none) @loc's city name, or %NULL
 **/
char *
gweather_location_get_city_name (GWeatherLocation *loc)
{
    g_return_val_if_fail (loc != NULL, NULL);

    if (loc->level == GWEATHER_LOCATION_CITY)
	return g_strdup (loc->name);
    else if (loc->level == GWEATHER_LOCATION_WEATHER_STATION &&
	     loc->parent &&
	     loc->parent->level == GWEATHER_LOCATION_CITY)
	return g_strdup (loc->parent->name);
    else
	return NULL;
}

WeatherLocation *
_weather_location_from_gweather_location (GWeatherLocation *gloc, const gchar *name)
{
    const char *code = NULL, *zone = NULL, *radar = NULL, *tz_hint = NULL;
    gboolean latlon_valid = FALSE;
    gdouble lat = DBL_MAX, lon = DBL_MAX;
    GWeatherLocation *l;
    WeatherLocation *wloc;

    g_return_val_if_fail (gloc != NULL, NULL);

    if (gloc->level == GWEATHER_LOCATION_CITY && gloc->children)
	l = gloc->children[0];
    else
	l = gloc;

    while (l && (!code || !zone || !radar || !tz_hint || !latlon_valid)) {
	if (!code && l->station_code)
	    code = l->station_code;
	if (!zone && l->forecast_zone)
	    zone = l->forecast_zone;
	if (!radar && l->radar)
	    radar = l->radar;
	if (!tz_hint && l->tz_hint)
	    tz_hint = l->tz_hint;
	if (!latlon_valid && l->latlon_valid) {
	    lat = l->latitude;
	    lon = l->longitude;
	    latlon_valid = TRUE;
	}
	l = l->parent;
    }

    wloc = _weather_location_new (name ? name : gweather_location_get_name (gloc),
				  code, zone, radar,
				  latlon_valid, lat, lon,
				  gweather_location_get_country (gloc),
				  tz_hint);
    return wloc;
}

GWeatherLocation *
gweather_location_find_by_station_code (GWeatherLocation *world,
					const gchar      *station_code)
{
    return g_hash_table_lookup (world->metar_code_cache, station_code);
}
