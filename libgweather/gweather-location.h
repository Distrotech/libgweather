/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* gweather-location.h - Location-handling code
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

#ifndef __GWEATHER_LOCATIONS_H__
#define __GWEATHER_LOCATIONS_H__

#ifndef GWEATHER_I_KNOW_THIS_IS_UNSTABLE
#error "libgweather should only be used if you understand that it's subject to change, and is not supported as a fixed API/ABI or as part of the platform"
#endif

#include <glib.h>
#include <libgweather/gweather-timezone.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _GWeatherLocation GWeatherLocation;
typedef gboolean (* GWeatherFilterFunc) (GWeatherLocation* location, gpointer user_data);

typedef enum { /*< underscore_name=gweather_location_level >*/
    GWEATHER_LOCATION_WORLD,
    GWEATHER_LOCATION_REGION,
    GWEATHER_LOCATION_COUNTRY,
    /* ADM1 = first-order administrative division = state/province, etc */
    GWEATHER_LOCATION_ADM1,
    /* ADM2 = second-order division = county, etc */
    GWEATHER_LOCATION_ADM2,
    GWEATHER_LOCATION_CITY,
    GWEATHER_LOCATION_WEATHER_STATION,
    GWEATHER_LOCATION_DETACHED
} GWeatherLocationLevel;

GType gweather_location_get_type (void);
#define GWEATHER_TYPE_LOCATION (gweather_location_get_type ())

GWeatherLocation      *gweather_location_get_world      (void);

G_DEPRECATED_FOR(gweather_location_get_world)
GWeatherLocation      *gweather_location_new_world      (gboolean use_regions);

GWeatherLocation      *gweather_location_ref            (GWeatherLocation  *loc);
void                   gweather_location_unref          (GWeatherLocation  *loc);

const char            *gweather_location_get_name       (GWeatherLocation  *loc);
const char            *gweather_location_get_sort_name  (GWeatherLocation  *loc);
GWeatherLocationLevel  gweather_location_get_level      (GWeatherLocation  *loc);
GWeatherLocation      *gweather_location_get_parent     (GWeatherLocation  *loc);

GWeatherLocation     **gweather_location_get_children   (GWeatherLocation  *loc);

gboolean               gweather_location_has_coords     (GWeatherLocation  *loc);
void                   gweather_location_get_coords     (GWeatherLocation  *loc,
							 double            *latitude,
							 double            *longitude);
double                 gweather_location_get_distance   (GWeatherLocation  *loc,
							 GWeatherLocation  *loc2);
GWeatherLocation      *gweather_location_find_nearest_city (GWeatherLocation *loc,
							    double            lat,
							    double            lon);

GWeatherLocation      *gweather_location_find_nearest_city_full (GWeatherLocation  *loc,
								 double             lat,
								 double             lon,
								 GWeatherFilterFunc func,
								 gpointer           user_data,
								 GDestroyNotify     destroy);

void                  gweather_location_detect_nearest_city (GWeatherLocation   *loc,
							     double              lat,
							     double              lon,
							     GCancellable       *cancellable,
							     GAsyncReadyCallback callback,
							     gpointer            user_data);
GWeatherLocation      *gweather_location_detect_nearest_city_finish (GAsyncResult *result, GError **error);

const char            *gweather_location_get_country    (GWeatherLocation  *loc);

GWeatherTimezone      *gweather_location_get_timezone   (GWeatherLocation  *loc);
GWeatherTimezone     **gweather_location_get_timezones  (GWeatherLocation  *loc);
void                   gweather_location_free_timezones (GWeatherLocation  *loc,
							 GWeatherTimezone **zones);

const char            *gweather_location_get_code       (GWeatherLocation  *loc);
char                  *gweather_location_get_city_name  (GWeatherLocation  *loc);
char                  *gweather_location_get_country_name (GWeatherLocation  *loc);

GWeatherLocation      *gweather_location_find_by_station_code (GWeatherLocation *world,
							       const gchar      *station_code);

gboolean               gweather_location_equal          (GWeatherLocation  *one,
							 GWeatherLocation  *two);

GVariant              *gweather_location_serialize      (GWeatherLocation  *loc);
GWeatherLocation      *gweather_location_deserialize    (GWeatherLocation  *world,
							 GVariant          *serialized);

G_END_DECLS

#endif /* __GWEATHER_LOCATIONS_H__ */
