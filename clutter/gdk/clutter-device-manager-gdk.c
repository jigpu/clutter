/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright © 2011  Intel Corp.
 *             2011  Giovanni Campagna <scampa.giovanni@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Emmanuele Bassi <ebassi@linux.intel.com>
 */

#include "config.h"

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

/* XXX: We're using the GdkDeviceManager deprecated API because
 * porting to the new GdkSeat API would imply adding an equivalent
 * API to Clutter.
 */
#define GDK_DISABLE_DEPRECATION_WARNINGS

#include "clutter-device-manager-gdk.h"

#include "clutter-backend-gdk.h"
#include "clutter-input-device-gdk.h"
#include "clutter-stage-gdk.h"

#include "clutter-backend.h"
#include "clutter-debug.h"
#include "clutter-device-manager-private.h"
#include "clutter-event-private.h"
#include "clutter-event-translator.h"
#include "clutter-stage-private.h"
#include "clutter-private.h"

#define clutter_device_manager_gdk_get_type     _clutter_device_manager_gdk_get_type

static void clutter_event_extender_iface_init   (ClutterEventExtenderInterface *iface);

G_DEFINE_TYPE_WITH_CODE (ClutterDeviceManagerGdk, clutter_device_manager_gdk,
                         CLUTTER_TYPE_DEVICE_MANAGER,
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_EVENT_EXTENDER,
                                                clutter_event_extender_iface_init))

enum {
  PROP_0,
  PROP_GDK_DISPLAY,
  PROP_LAST
};

static void
clutter_device_manager_gdk_copy_event_data (ClutterEventExtender *event_extender,
                                            const ClutterEvent   *src,
                                            ClutterEvent         *dest)
{
  GdkEvent *gdk_event;

  gdk_event = _clutter_event_get_platform_data (src);
  if (gdk_event != NULL)
    _clutter_event_set_platform_data (dest, gdk_event_copy (gdk_event));
}

static void
clutter_device_manager_gdk_free_event_data (ClutterEventExtender *event_extender,
                                            ClutterEvent         *event)
{
  GdkEvent *gdk_event;

  gdk_event = _clutter_event_get_platform_data (event);
  if (gdk_event != NULL)
    gdk_event_free (gdk_event);
}

static void
clutter_event_extender_iface_init (ClutterEventExtenderInterface *iface)
{
  iface->copy_event_data = clutter_device_manager_gdk_copy_event_data;
  iface->free_event_data = clutter_device_manager_gdk_free_event_data;
}

ClutterInputDevice *
_clutter_device_manager_gdk_lookup_device (ClutterDeviceManager *manager,
					  GdkDevice            *device)
{
  ClutterDeviceManagerGdk *manager_gdk = CLUTTER_DEVICE_MANAGER_GDK (manager);
  ClutterInputDevice *clutter_device;

  clutter_device = g_object_get_data (G_OBJECT (device), "clutter-device");
  if (clutter_device != NULL)
    return clutter_device;

  clutter_device = _clutter_input_device_gdk_new (manager, device);
  g_object_set_data_full (G_OBJECT (device), "clutter-device", clutter_device, g_object_unref);

  manager_gdk->device_cache = g_slist_prepend (manager_gdk->device_cache, g_object_ref (clutter_device));
  g_hash_table_replace (manager_gdk->device_by_id,
			GINT_TO_POINTER (clutter_input_device_get_device_id (clutter_device)),
			g_object_ref (clutter_device));

  return clutter_device;
}

static void
clutter_device_manager_gdk_add_device (ClutterDeviceManager *manager,
                                       ClutterInputDevice   *device)
{
  /* XXX implement */
}

static void
clutter_device_manager_gdk_remove_device (ClutterDeviceManager *manager,
                                          ClutterInputDevice   *device)
{
  /* XXX implement */
}

static const GSList *
clutter_device_manager_gdk_get_devices (ClutterDeviceManager *manager)
{
  ClutterDeviceManagerGdk *manager_gdk = CLUTTER_DEVICE_MANAGER_GDK (manager);

  return manager_gdk->device_cache;
}

static ClutterInputDevice *
clutter_device_manager_gdk_get_device (ClutterDeviceManager *manager,
                                       gint                  id)
{
  ClutterDeviceManagerGdk *manager_gdk = CLUTTER_DEVICE_MANAGER_GDK (manager);

  return g_hash_table_lookup (manager_gdk->device_by_id, GINT_TO_POINTER (id));
}

static ClutterInputDevice *
clutter_device_manager_gdk_get_core_device (ClutterDeviceManager   *manager,
                                            ClutterInputDeviceType  device_type)
{
  ClutterDeviceManagerGdk *manager_gdk = CLUTTER_DEVICE_MANAGER_GDK (manager);
  GdkDevice *gdk_device;

  gdk_device = gdk_seat_get_pointer (manager_gdk->seat);

  g_assert (gdk_device != NULL);

  return _clutter_device_manager_gdk_lookup_device (manager, gdk_device);
}

static void
gdk_device_added (GdkSeat                 *seat,
		  GdkDevice               *device,
		  ClutterDeviceManager    *self)
{
  /* this will do the right thing if the device is not there */
  ClutterInputDevice *clutter_device = _clutter_device_manager_gdk_lookup_device (self, device);

  _clutter_device_manager_add_device (self, clutter_device);
}

static void
gdk_device_removed (GdkSeat                 *seat,
		    GdkDevice               *device,
		    ClutterDeviceManagerGdk *self)
{
  ClutterInputDevice *clutter_device = g_object_get_data (G_OBJECT (device), "clutter-device");

  if (clutter_device == NULL)
    return;

  self->device_cache = g_slist_remove (self->device_cache, clutter_device);
  g_object_unref (clutter_device);

  g_hash_table_remove (self->device_by_id,
		       GINT_TO_POINTER (clutter_input_device_get_device_id (clutter_device)));

  _clutter_device_manager_remove_device (CLUTTER_DEVICE_MANAGER (self), clutter_device);
}

static void
gdk_device_foreach_cb (gpointer data,
		       gpointer user_data)
{
  _clutter_device_manager_gdk_lookup_device (user_data, data);
}

static void
clutter_device_manager_gdk_constructed (GObject *gobject)
{
  ClutterDeviceManagerGdk *manager_gdk = CLUTTER_DEVICE_MANAGER_GDK (gobject);
  ClutterDeviceManager *manager = CLUTTER_DEVICE_MANAGER (gobject);
  GList *all_devices;
  GdkDevice *gdk_device;

  g_assert (manager_gdk->seat != NULL);

  gdk_device = gdk_seat_get_pointer (manager_gdk->seat);
  _clutter_device_manager_gdk_lookup_device (manager, gdk_device);

  gdk_device = gdk_seat_get_keyboard (manager_gdk->seat);
  _clutter_device_manager_gdk_lookup_device (manager, gdk_device);

  all_devices = gdk_seat_get_slaves (manager_gdk->seat,
                                     GDK_SEAT_CAPABILITY_ALL);
  g_list_foreach (all_devices, gdk_device_foreach_cb, manager_gdk);
  g_list_free (all_devices);

  g_object_connect (manager_gdk->seat,
		    "object-signal::device-added", gdk_device_added, gobject,
		    "object-signal::device-removed", gdk_device_removed, gobject,
		    NULL);

  if (G_OBJECT_CLASS (clutter_device_manager_gdk_parent_class)->constructed)
    G_OBJECT_CLASS (clutter_device_manager_gdk_parent_class)->constructed (gobject);
}

static void
clutter_device_manager_gdk_set_property (GObject      *gobject,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
  ClutterDeviceManagerGdk *manager_gdk = CLUTTER_DEVICE_MANAGER_GDK (gobject);
  GdkDisplay *gdk_display;

  switch (prop_id)
    {
    case PROP_GDK_DISPLAY:
      gdk_display = GDK_DISPLAY (g_value_get_object (value));
      manager_gdk->seat = gdk_display_get_default_seat (gdk_display);
      g_object_ref (manager_gdk->seat);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_device_manager_gdk_class_init (ClutterDeviceManagerGdkClass *klass)
{
  ClutterDeviceManagerClass *manager_class;
  GObjectClass *gobject_class;
  GParamSpec *pspec;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->constructed = clutter_device_manager_gdk_constructed;
  gobject_class->set_property = clutter_device_manager_gdk_set_property;
  
  manager_class = CLUTTER_DEVICE_MANAGER_CLASS (klass);
  manager_class->add_device = clutter_device_manager_gdk_add_device;
  manager_class->remove_device = clutter_device_manager_gdk_remove_device;
  manager_class->get_devices = clutter_device_manager_gdk_get_devices;
  manager_class->get_core_device = clutter_device_manager_gdk_get_core_device;
  manager_class->get_device = clutter_device_manager_gdk_get_device;

  pspec = g_param_spec_object ("gdk-display",
			       "GdkDisplay",
			       "The GDK display",
			       GDK_TYPE_DISPLAY,
			       CLUTTER_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (gobject_class, PROP_GDK_DISPLAY, pspec);
}

static void
clutter_device_manager_gdk_init (ClutterDeviceManagerGdk *self)
{
  self->device_by_id = g_hash_table_new_full (NULL, NULL,
					      NULL, (GDestroyNotify) g_object_unref);
}
