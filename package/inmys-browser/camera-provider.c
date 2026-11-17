// SPDX-License-Identifier: Apache-2.0
/* Expose the session's standard unixfdsrc as a camera to GstDeviceMonitor.
 * Pixel transport and buffer release are handled by the GStreamer unixfd plugin. */
#include <gst/gst.h>
#include <glib/gstdio.h>
#include <sys/stat.h>

typedef struct { GstDevice parent; } InmysCamera;
typedef struct { GstDeviceClass parent; } InmysCameraClass;
G_DEFINE_TYPE(InmysCamera, inmys_camera, GST_TYPE_DEVICE)

static GstElement *create_camera(GstDevice *device, const gchar *name)
{
    GstStructure *properties = gst_device_get_properties(device);
    const gchar *path = gst_structure_get_string(properties, "inmys.socket-path");
    GstElement *source = gst_element_factory_make("unixfdsrc", name);
    if (source)
        g_object_set(source, "socket-path", path, NULL);
    gst_structure_free(properties);
    return source;
}
static void inmys_camera_class_init(InmysCameraClass *klass)
{
    GST_DEVICE_CLASS(klass)->create_element = create_camera;
}
static void inmys_camera_init(InmysCamera *camera) { (void)camera; }

typedef struct { GstDeviceProvider parent; } InmysProvider;
typedef struct { GstDeviceProviderClass parent; } InmysProviderClass;
G_DEFINE_TYPE(InmysProvider, inmys_provider, GST_TYPE_DEVICE_PROVIDER)

static GList *probe_cameras(GstDeviceProvider *provider)
{
    (void)provider;
    const gchar *path = g_getenv("INMYS_CAMERA_SOCKET");
    if (!path || !g_path_is_absolute(path)) return NULL;
    struct stat st;
    if (g_stat(path, &st) || !S_ISSOCK(st.st_mode)) {
        return NULL;
    }
    GstCaps *caps = gst_caps_from_string(
        "video/x-raw,format=NV12,width=1280,height=720,framerate=15/1,"
        "interlace-mode=progressive,pixel-aspect-ratio=1/1");
    GstStructure *properties = gst_structure_new("inmys-camera",
        "device.class", G_TYPE_STRING, "camera",
        "device.api", G_TYPE_STRING, "unixfd",
        "node.name", G_TYPE_STRING, "inmys-csi",
        "is-default", G_TYPE_BOOLEAN, TRUE,
        "inmys.socket-path", G_TYPE_STRING, path, NULL);
    GstDevice *device = g_object_new(inmys_camera_get_type(),
        "display-name", "INMYS-CSI", "device-class", "Video/Source",
        "caps", caps, "properties", properties, NULL);
    gst_structure_free(properties);
    gst_caps_unref(caps);
    return g_list_append(NULL, device);
}
static void inmys_provider_class_init(InmysProviderClass *klass)
{
    GstDeviceProviderClass *provider = GST_DEVICE_PROVIDER_CLASS(klass);
    provider->probe = probe_cameras;
    gst_device_provider_class_set_static_metadata(provider,
        "INMYS CSI camera", "Video/Source", "Felix DMA-BUF camera over unixfd", "INMYS");
}
static void inmys_provider_init(InmysProvider *provider) { (void)provider; }
static gboolean plugin_init(GstPlugin *plugin)
{
    return gst_device_provider_register(plugin, "inmyscsi", GST_RANK_PRIMARY + 1,
        inmys_provider_get_type());
}
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, inmyscamera,
    "INMYS camera device provider", plugin_init, "1.0", "Apache 2.0",
    "INMYS SDK", "https://inmys.ru")
