/*****************************************************************************
 *   Copyright(C)2009-2022 by VSF Team                                       *
 *                                                                           *
 *  Licensed under the Apache License, Version 2.0 (the "License");          *
 *  you may not use this file except in compliance with the License.         *
 *  You may obtain a copy of the License at                                  *
 *                                                                           *
 *     http://www.apache.org/licenses/LICENSE-2.0                            *
 *                                                                           *
 *  Unless required by applicable law or agreed to in writing, software      *
 *  distributed under the License is distributed on an "AS IS" BASIS,        *
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. *
 *  See the License for the specific language governing permissions and      *
 *  limitations under the License.                                           *
 *                                                                           *
 ****************************************************************************/

/*============================ INCLUDES ======================================*/

#include "shell/sys/linux/vsf_linux_cfg.h"

#if VSF_USE_LINUX == ENABLED

#define __VSF_USBH_CLASS_IMPLEMENT_CLASS__
#include <unistd.h>

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/usb.h>

/*============================ MACROS ========================================*/
/*============================ MACROFIED FUNCTIONS ===========================*/
/*============================ TYPES =========================================*/

struct usb_driver_adapter_vsf {
    vsf_dlist_node_t node;
    vk_usbh_class_t drvclass;
    vk_usbh_class_drv_t vsf_drv;
    struct usb_driver *linux_drv;
};

typedef struct vsf_usbh_adapter_linux_t {
    vk_usbh_dev_id_t *id;

    struct usb_device device;
    struct usb_interface ifs;
    struct work_struct probe_work;
    struct work_struct disconnect_work;
    struct usb_driver *linux_drv;
} vsf_usbh_adapter_linux_t;

/*============================ GLOBAL VARIABLES ==============================*/
/*============================ PROTOTYPES ====================================*/
/*============================ LOCAL VARIABLES ===============================*/

static vk_usbh_t *__usbdrv_host;
static vsf_dlist_t __usbdrv_list;

/*============================ IMPLEMENTATION ================================*/

extern int usb_urb_dir_in(struct urb *urb)
{
    vk_usbh_pipe_t pipe = vk_usbh_urb_get_pipe(&urb->__urb);
    return pipe.use_as__vk_usbh_pipe_flag_t.dir_in1out0;
}

void vsf_linux_usb_init(vk_usbh_t *usbh)
{
    __usbdrv_host = usbh;
}

static int __usb_anchor_check_wakeup(struct usb_anchor *anchor)
{
    return atomic_read(&anchor->suspend_wakeups) == 0 && list_empty(&anchor->urb_list);
}

void usb_anchor_urb(struct urb *urb, struct usb_anchor *anchor)
{
    vsf_protect_t orig = vsf_protect_int();
        usb_get_urb(urb);
        list_add_tail(&urb->anchor_list, &anchor->urb_list);
        urb->anchor = anchor;
    vsf_unprotect_int(orig);
}

static void __usb_unanchor_urb(vsf_protect_t orig, struct urb *urb, struct usb_anchor *anchor)
{
    urb->anchor = NULL;
    list_del(&urb->anchor_list);
    vsf_unprotect_int(orig);

    usb_put_urb(urb);
    if (__usb_anchor_check_wakeup(anchor)) {
        wake_up(&anchor->wait);
    }
}

void usb_unanchor_urb(struct urb *urb)
{
    VSF_LINUX_ASSERT((urb != NULL) && (urb->anchor != NULL));
    __usb_unanchor_urb(vsf_protect_int(), urb, urb->anchor);    
}

struct urb *usb_get_from_anchor(struct usb_anchor *anchor)
{
    struct urb *victim;
    vsf_protect_t orig = vsf_protect_int();
    if (!list_empty(&anchor->urb_list)) {
        victim = list_entry(anchor->urb_list.next, struct urb, anchor_list);
        usb_get_urb(victim);
        __usb_unanchor_urb(orig, victim, anchor);
    } else {
        vsf_unprotect_int(orig);
        victim = NULL;
    }
    return victim;
}

void usb_kill_anchored_urbs(struct usb_anchor *anchor)
{
    struct urb *victim;
    int is_empty;
    vsf_protect_t orig;

    do {
        orig = vsf_protect_int();
        while (!list_empty(&anchor->urb_list)) {
            victim = list_entry(&anchor->urb_list.prev, struct urb, anchor_list);
            usb_get_urb(victim);
            vsf_unprotect_int(orig);

            usb_kill_urb(victim);
            usb_put_urb(victim);
            orig = vsf_protect_int();
        }
        is_empty = __usb_anchor_check_wakeup(anchor);
        vsf_unprotect_int(orig);
    } while (!is_empty);
}

void usb_unlink_anchored_urbs(struct usb_anchor *anchor)
{
    struct urb *victim;
    while ((victim = usb_get_from_anchor(anchor)) != NULL) {
        usb_unlink_urb(victim);
        usb_put_urb(victim);
    }
}

int usb_wait_anchor_empty_timeout(struct usb_anchor *anchor, unsigned int timeout)
{
    return wait_event_timeout(anchor->wait,
                __usb_anchor_check_wakeup(anchor),
                msecs_to_jiffies(timeout));
}

static void __vsf_linux_usb_probe_work(struct work_struct *work)
{
    vsf_usbh_adapter_linux_t *usbh_linux = container_of(work, vsf_usbh_adapter_linux_t, probe_work);
    if (usbh_linux->linux_drv->probe != NULL) {
        usbh_linux->linux_drv->probe(&usbh_linux->ifs, usbh_linux->id);
    }
}

static void __vsf_linux_usb_disconnect_work(struct work_struct *work)
{
    vsf_usbh_adapter_linux_t *usbh_linux = container_of(work, vsf_usbh_adapter_linux_t, probe_work);
    if (usbh_linux->linux_drv->disconnect != NULL) {
        usbh_linux->linux_drv->disconnect(&usbh_linux->ifs);
    }
}

static void * __vsf_linux_usb_probe(vk_usbh_t *usbh, vk_usbh_dev_t *dev, vk_usbh_ifs_parser_t *parser_ifs)
{
    vk_usbh_ifs_t *ifs = parser_ifs->ifs;
    struct usb_driver_adapter_vsf *adapter = container_of(ifs->drv, struct usb_driver_adapter_vsf, vsf_drv);
    vsf_usbh_adapter_linux_t *usbh_linux = vsf_usbh_malloc(sizeof(*usbh_linux));
    if (NULL == usbh_linux) {
        return NULL;
    }

    usbh_linux->id = (vk_usbh_dev_id_t *)parser_ifs->id;
    usbh_linux->device.__host = usbh;
    usbh_linux->device.__dev = dev;
    usbh_linux->ifs.dev.parent = &usbh_linux->device.dev;

    usbh_linux->linux_drv = adapter->linux_drv;

    ifs->param = usbh_linux;
    INIT_WORK(&usbh_linux->probe_work, __vsf_linux_usb_probe_work);
    schedule_work(&usbh_linux->probe_work);
    return usbh_linux;
}

static void __usb_linux_usb_disconnect(vk_usbh_t *usbh, vk_usbh_dev_t *dev, void *param)
{
    vsf_usbh_adapter_linux_t *usbh_linux = param;
    INIT_WORK(&usbh_linux->disconnect_work, __vsf_linux_usb_disconnect_work);
    schedule_work(&usbh_linux->disconnect_work);
}

void usb_register_driver(struct usb_driver *drv, struct module *mod, const char *name)
{
    struct usb_driver_adapter_vsf *adapter = kzalloc(sizeof(*adapter), GFP_KERNEL);
    VSF_LINUX_ASSERT(adapter != NULL);

    vsf_dlist_init_node(struct usb_driver_adapter_vsf, node, adapter);
    adapter->linux_drv = drv;

    const struct usb_device_id *ids = adapter->vsf_drv.dev_ids = drv->id_table;
    adapter->vsf_drv.dev_id_num = 0;
    while (ids->match_flags != 0) {
        adapter->vsf_drv.dev_id_num++;
    }
    adapter->vsf_drv.name = drv->name;
    adapter->vsf_drv.probe = __vsf_linux_usb_probe;
    adapter->vsf_drv.disconnect = __usb_linux_usb_disconnect;

    vsf_protect_t orig = vsf_protect_sched();
        vsf_dlist_add_to_head(struct usb_driver_adapter_vsf, node, &__usbdrv_list, adapter);
    vsf_unprotect_sched(orig);
    adapter->drvclass.drv = &adapter->vsf_drv;
    vsf_slist_init_node(vsf_usbh_class_t, node, &adapter->drvclass);
    vk_usbh_register_class(__usbdrv_host, &adapter->drvclass);
}

void usb_deregister_driver(struct usb_driver *drv)
{
    struct usb_driver_adapter_vsf *adapter = NULL;

    vsf_protect_t orig = vsf_protect_sched();
        __vsf_slist_foreach_unsafe(struct usb_driver_adapter_vsf, node, &__usbdrv_list) {
            if (_->linux_drv == drv) {
                adapter = _;
                break;
            }
        }
    vsf_unprotect_sched(orig);

    if (adapter != NULL) {
        vk_usbh_unregister_class(__usbdrv_host, &adapter->drvclass);
        kfree(adapter);
    }
}

int usb_driver_claim_interface(struct usb_driver *driver, struct usb_interface *iface, void *data)
{
    struct device *dev = &iface->dev;
    if (dev->driver) {
        return -EBUSY;
    }
    dev->driver = &driver->drvwrap.driver;
    usb_set_intfdata(iface, data);
    return 0;
}

void usb_driver_release_interface(struct usb_driver *driver, struct usb_interface *iface)
{
    struct device *dev = &iface->dev;
    if (!dev->driver || (dev->driver != &driver->drvwrap.driver)) {
        return;
    }
    dev->driver = NULL;
}

void usb_init_urb(struct urb *urb)
{
    if (urb) {
        memset(urb, 0, sizeof(*urb));
        kref_init(&urb->kref);
        INIT_LIST_HEAD(&urb->urb_list);
        INIT_LIST_HEAD(&urb->anchor_list);
    }
}

struct urb *usb_get_urb(struct urb *urb)
{
    if (urb) {
        kref_get(&urb->kref);
    }
    return urb;
}

struct urb * usb_alloc_urb(int iso_packets, gfp_t flags)
{
#if VSF_USBH_CFG_ISO_EN != ENABLED
    if (iso_packets != 0) {
        VSF_LINUX_ASSERT(false);
        return NULL;
    }
#else
    if (iso_packets > VSF_USBH_CFG_ISO_PACKET_LIMIT) {
        VSF_LINUX_ASSERT(false);
        return NULL;
    }
#endif

    struct urb *urb = vsf_usbh_malloc(sizeof(*urb));
    if (NULL == urb) {
        return NULL;
    }

    usb_init_urb(urb);
    return urb;
}

static void urb_destroy(struct kref *kref)
{
    struct urb *urb = container_of(kref, struct urb, kref);
    if (urb->transfer_flags & URB_FREE_BUFFER) {
        vsf_usbh_free(urb->transfer_buffer);
    }
    vsf_usbh_free(urb);
}

void usb_free_urb(struct urb *urb)
{
    if (urb) {
        kref_put(&urb->kref, urb_destroy);
    }
}

int usb_submit_urb(struct urb *urb, gfp_t flags)
{
    vsf_err_t err = vk_usbh_submit_urb(__usbdrv_host, &urb->__urb);
    if (VSF_ERR_NONE != err) {
        return -EIO;
    }

    vsf_thread_wfe(VSF_EVT_MESSAGE);
    if (URB_OK != vk_usbh_urb_get_status(&urb->__urb)) {
        return -EIO;
    }
    return vk_usbh_urb_get_actual_length(&urb->__urb);
}

int usb_unlink_urb(struct urb *urb)
{
    vk_usbh_free_urb(__usbdrv_host, &urb->__urb);
    return 0;
}

void usb_kill_urb(struct urb *urb)
{
    vk_usbh_free_urb(__usbdrv_host, &urb->__urb);
}

int usb_control_msg(struct usb_device *udev, unsigned int pipe, __u8 request, __u8 requesttype, __u16 value, __u16 index, void *data, __u16 size, int timeout)
{
    vk_usbh_urb_t *urb = &udev->__dev->ep0.urb;
    vk_usbh_urb_set_buffer(urb, data, size);
    vsf_err_t err = vk_usbh_control_msg(udev->__host, udev->__dev, &(struct usb_ctrlrequest_t){
        .bRequest       = requesttype,
        .bRequest       = request,
        .wValue         = value,
        .wIndex         = index,
        .wLength        = size,
    });
    if (VSF_ERR_NONE != err) {
        return -EIO;
    }

    vsf_thread_wfm();

    if (URB_OK != vk_usbh_urb_get_status(&udev->__dev->ep0.urb)) {
        return -EIO;
    }
    return vk_usbh_urb_get_actual_length(urb);
}

int usb_control_msg_send(struct usb_device *udev, __u8 endpoint, __u8 request, __u8 requesttype, __u16 value, __u16 index, const void *data, __u16 size, int timeout, gfp_t memflags)
{
    int ret = usb_control_msg(udev, usb_sndctrlpipe(udev, endpoint), request, requesttype, value, index, (void *)data, size, timeout);
    return ret < 0 ? ret : 0;
}

int usb_control_msg_recv(struct usb_device *udev, __u8 endpoint, __u8 request, __u8 requesttype, __u16 value, __u16 index, void *data, __u16 size, int timeout, gfp_t memflags)
{
    int ret = usb_control_msg(udev, usb_rcvctrlpipe(udev, endpoint), request, requesttype, value, index, (void *)data, size, timeout);
    return ret < 0 ? ret : 0;
}

int usb_interrupt_msg(struct usb_device *udev, unsigned int pipe, void *data, int len, int *actual_length, int timeout)
{
    struct urb *urb = vsf_usbh_malloc(sizeof(*urb));
    if (NULL == urb) {
        return -ENOMEM;
    }
    vk_usbh_urb_prepare(&urb->__urb, udev->__dev, (struct usb_endpoint_desc_t *)&(usb_pipe_endpoint(udev, pipe)->desc));
    vk_usbh_alloc_urb(udev->__host, udev->__dev, &urb->__urb);
    vk_usbh_urb_set_buffer(&urb->__urb, data, len);
    if (VSF_ERR_NONE != vk_usbh_submit_urb(udev->__host, &urb->__urb)) {
        vk_usbh_free_urb(udev->__host, &urb->__urb);
        return -EIO;
    }

    vsf_thread_wfm();

    if (URB_OK != vk_usbh_urb_get_status(&urb->__urb)) {
        return -EIO;
    }
    return vk_usbh_urb_get_actual_length(&urb->__urb);
}

int usb_bulk_msg(struct usb_device *udev, unsigned int pipe, void *data, int len, int *actual_length, int timeout)
{
    return usb_interrupt_msg(udev, pipe, data, len, actual_length, timeout);
}

int usb_reset_device(struct usb_device *udev)
{
    vk_usbh_reset_dev(udev->__host, udev->__dev);
    while (vk_usbh_is_dev_resetting(udev->__host, udev->__dev)) {
        usleep(20);
    }
    return 0;
}

int usb_get_descriptor(struct usb_device *udev, unsigned char type, unsigned char index, void *buf, int size)
{
    return usb_control_msg(udev, usb_rcvctrlpipe(udev, 0), USB_REQ_GET_DESCRIPTOR, USB_DIR_IN, (type << 8) + index, 0, buf, size, 5000);
}
int usb_get_status(struct usb_device *udev, int recip, int type, int target, void *data)
{
    int length;
    switch (type) {
    case USB_STATUS_TYPE_STANDARD:  length = 2; break;
    case USB_STATUS_TYPE_PTM:       length = 4; break;
    default:                        return -EINVAL;
    }

    uint8_t status[length];
    int ret = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0), USB_REQ_GET_STATUS, USB_DIR_IN | recip, USB_STATUS_TYPE_STANDARD, target, status, length, 5000);
    switch (ret) {
    case 2:                         *(u16 *)data = le16_to_cpu(*(__le16 *)status);  break;
    case 4:                         *(u32 *)data = le32_to_cpu(*(__le32 *)status);  break;
    default:                        return -EIO;
    }
    return 0;
}

int usb_clear_halt(struct usb_device *udev, int pipe)
{
    int endp = usb_pipeendpoint(pipe);
    return usb_control_msg(udev, usb_sndctrlpipe(udev, 0), USB_REQ_CLEAR_FEATURE, USB_RECIP_ENDPOINT, USB_ENDPOINT_HALT, endp, NULL, 0, 5000);
}

int usb_set_interface(struct usb_device *udev, int ifnum, int alternate)
{
    return usb_control_msg(udev, usb_sndctrlpipe(udev, 0), USB_REQ_SET_INTERFACE, USB_RECIP_INTERFACE, alternate, ifnum, NULL, 0, 5000);
}

int usb_set_configuration(struct usb_device *udev, int configuration)
{
    return usb_control_msg(udev, usb_sndctrlpipe(udev, 0), USB_REQ_SET_CONFIGURATION, 0, configuration, 0, NULL, 0, 5000);
}

struct usb_interface * usb_ifnum_to_if(const struct usb_device *dev, unsigned ifnum)
{
    struct usb_host_config *config = dev->actconfig;
    if (!config) {
        return NULL;
    }

    for (int i = 0; i < config->desc.bNumInterfaces; i++) {
        if (config->interface[i]->altsetting[0].desc.bInterfaceNumber == ifnum) {
            return config->interface[i];
        }
    }

    return NULL;
}

struct usb_host_interface * usb_altnum_to_altsetting(const struct usb_interface *intf, unsigned int altnum)
{
    for (int i = 0; i < intf->num_altsetting; i++) {
        if (intf->altsetting[i].desc.bAlternateSetting == altnum) {
            return &intf->altsetting[i];
        }
    }
    return NULL;
}

struct usb_host_interface * usb_find_alt_setting(struct usb_host_config *config, unsigned int iface_num, unsigned int alt_num)
{
    struct usb_interface_cache *intf_cache = NULL;
    if (!config) {
        return NULL;
    }

    for (int i = 0; i < config->desc.bNumInterfaces; i++) {
        if (config->intf_cache[i]->altsetting[0].desc.bInterfaceNumber == iface_num) {
            intf_cache = config->intf_cache[i];
            break;
        }
    }
    if (!intf_cache) {
        return NULL;
    }

    for (int i = 0; i < intf_cache->num_altsetting; i++) {
        if (intf_cache->altsetting[i].desc.bAlternateSetting == alt_num) {
            return &intf_cache->altsetting[i];
        }
    }

    return NULL;
}

struct usb_interface * usb_find_interface(struct usb_driver *drv, int minor)
{
    return NULL;
}

static bool match_endpoint(struct usb_endpoint_descriptor *epd,
        struct usb_endpoint_descriptor **bulk_in,
        struct usb_endpoint_descriptor **bulk_out,
        struct usb_endpoint_descriptor **int_in,
        struct usb_endpoint_descriptor **int_out)
{
    switch (usb_endpoint_type(epd)) {
    case USB_ENDPOINT_XFER_BULK:
        if (usb_endpoint_dir_in(epd)) {
            if (bulk_in && !*bulk_in) {
                *bulk_in = epd;
                break;
            }
        } else {
            if (bulk_out && !*bulk_out) {
                *bulk_out = epd;
                break;
            }
        }

        return false;
    case USB_ENDPOINT_XFER_INT:
        if (usb_endpoint_dir_in(epd)) {
            if (int_in && !*int_in) {
                *int_in = epd;
                break;
            }
        } else {
            if (int_out && !*int_out) {
                *int_out = epd;
                break;
            }
        }

        return false;
    default:
        return false;
    }

    return (!bulk_in || *bulk_in) && (!bulk_out || *bulk_out)
        && (!int_in || *int_in) && (!int_out || *int_out);
}

int usb_find_common_endpoints(struct usb_host_interface *alt,
        struct usb_endpoint_descriptor **bulk_in,
        struct usb_endpoint_descriptor **bulk_out,
        struct usb_endpoint_descriptor **int_in,
        struct usb_endpoint_descriptor **int_out)
{
    if (bulk_in) {
        *bulk_in = NULL;
    }
    if (bulk_out) {
        *bulk_out = NULL;
    }
    if (int_in) {
        *int_in = NULL;
    }
    if (int_out) {
        *int_out = NULL;
    }

    for (int i = 0; i < alt->desc.bNumEndpoints; ++i) {
        if (match_endpoint(&alt->endpoint[i].desc, bulk_in, bulk_out, int_in, int_out)) {
            return 0;
        }
    }

    return -ENXIO;
}

int usb_find_common_endpoints_reverse(struct usb_host_interface *alt,
        struct usb_endpoint_descriptor **bulk_in,
        struct usb_endpoint_descriptor **bulk_out,
        struct usb_endpoint_descriptor **int_in,
        struct usb_endpoint_descriptor **int_out)
{
    if (bulk_in) {
        *bulk_in = NULL;
    }
    if (bulk_out) {
        *bulk_out = NULL;
    }
    if (int_in) {
        *int_in = NULL;
    }
    if (int_out) {
        *int_out = NULL;
    }

    for (int i = alt->desc.bNumEndpoints; i >= 0; --i) {;
        if (match_endpoint(&alt->endpoint[i].desc, bulk_in, bulk_out, int_in, int_out)) {
            return 0;
        }
    }

    return -ENXIO;
}

#endif
