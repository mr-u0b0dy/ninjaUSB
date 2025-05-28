
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/bos.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(usbd_app_config);


#define USB_DEVICE_MANUFACTURER "Zephyr Project" //TODO: change value
#define USB_DEVICE_PRODUCT "NinjaUSB"
#define USB_DEVICE_VID 0x2fe3 //TODO: change value
#define USB_DEVICE_PID 0x0007 //TODO: change value

/* doc device instantiation start */
/*
 * Instantiate a context named usbd_inst using the default USB device
 * controller, the Zephyr project vendor ID, and the sample product ID.
 * Zephyr project vendor ID must not be used outside of Zephyr samples.
 */
USBD_DEVICE_DEFINE(usbd_inst,
		   DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
		   USB_DEVICE_VID, USB_DEVICE_PID);
/* doc device instantiation end */

/* doc string instantiation start */
USBD_DESC_LANG_DEFINE(lang);
USBD_DESC_MANUFACTURER_DEFINE(mfr, USB_DEVICE_MANUFACTURER);
USBD_DESC_PRODUCT_DEFINE(product, USB_DEVICE_PRODUCT);
USBD_DESC_SERIAL_NUMBER_DEFINE(sn);
/* doc string instantiation end */

USBD_DESC_CONFIG_DEFINE(fs_cfg_desc, "FS Configuration");
/*USBD_DESC_CONFIG_DEFINE(hs_cfg_desc, "HS Configuration");*/

/* doc configuration instantiation start */
static const uint8_t attributes = (IS_ENABLED(CONFIG_usbd_inst_SELF_POWERED) ?
				   USB_SCD_SELF_POWERED : 0) |
				  (IS_ENABLED(CONFIG_usbd_inst_REMOTE_WAKEUP) ?
				   USB_SCD_REMOTE_WAKEUP : 0);

#define CONFIG_USB_DEVICE_MAX_POWER 125
/* Full speed configuration */
USBD_CONFIGURATION_DEFINE(fs_config,
			  attributes,
			  CONFIG_USB_DEVICE_MAX_POWER, &fs_cfg_desc);

/* High speed configuration */
/*USBD_CONFIGURATION_DEFINE(sample_hs_config,*/
/*			  attributes,*/
/*			  CONFIG_USB_DEVICE_MAX_POWER, &hs_cfg_desc);*/
/* doc configuration instantiation end */

/*
 * This does not yet provide valuable information, but rather serves as an
 * example, and will be improved in the future.
 */
static const struct usb_bos_capability_lpm bos_cap_lpm = {
	.bLength = sizeof(struct usb_bos_capability_lpm),
	.bDescriptorType = USB_DESC_DEVICE_CAPABILITY,
	.bDevCapabilityType = USB_BOS_CAPABILITY_EXTENSION,
	.bmAttributes = 0UL,
};

static const char *const blocklist[] = {
        "dfu_dfu",
        NULL,
};

USBD_DESC_BOS_DEFINE(usbnext, sizeof(bos_cap_lpm), &bos_cap_lpm);

static void app_fix_code_triple(struct usbd_context *uds_ctx,
				   const enum usbd_speed speed)
{
	/* Always use class code information from Interface Descriptors */
	if (IS_ENABLED(CONFIG_USBD_CDC_ACM_CLASS) ||
	    IS_ENABLED(CONFIG_USBD_CDC_ECM_CLASS) ||
	    IS_ENABLED(CONFIG_USBD_CDC_NCM_CLASS) ||
	    IS_ENABLED(CONFIG_USBD_AUDIO2_CLASS)) {
		/*
		 * Class with multiple interfaces have an Interface
		 * Association Descriptor available, use an appropriate triple
		 * to indicate it.
		 */
		usbd_device_set_code_triple(uds_ctx, speed,
					    USB_BCC_MISCELLANEOUS, 0x02, 0x01);
	} else {
		usbd_device_set_code_triple(uds_ctx, speed, 0, 0, 0);
	}
}

struct usbd_context *usbd_inst_setup_device(usbd_msg_cb_t msg_cb)
{
	int err;

	/* doc add string descriptor start */
	err = usbd_add_descriptor(&usbd_inst, &lang);
	if (err) {
		LOG_ERR("Failed to initialize language descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_descriptor(&usbd_inst, &mfr);
	if (err) {
		LOG_ERR("Failed to initialize manufacturer descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_descriptor(&usbd_inst, &product);
	if (err) {
		LOG_ERR("Failed to initialize product descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_descriptor(&usbd_inst, &sn);
	if (err) {
		LOG_ERR("Failed to initialize SN descriptor (%d)", err);
		return NULL;
	}
	/* doc add string descriptor end */

	/*if (usbd_caps_speed(&usbd_inst) == USBD_SPEED_HS) {*/
	/*	err = usbd_add_configuration(&usbd_inst, USBD_SPEED_HS,*/
	/*				     &sample_hs_config);*/
	/*	if (err) {*/
	/*		LOG_ERR("Failed to add High-Speed configuration");*/
	/*		return NULL;*/
	/*	}*/
	/**/
	/*	err = usbd_register_all_classes(&usbd_inst, USBD_SPEED_HS, 1);*/
	/*	if (err) {*/
	/*		LOG_ERR("Failed to add register classes");*/
	/*		return NULL;*/
	/*	}*/
	/**/
	/*	app_fix_code_triple(&usbd_inst, USBD_SPEED_HS);*/
	/*}*/

	/* doc configuration register start */
	err = usbd_add_configuration(&usbd_inst, USBD_SPEED_FS,
				     &fs_config);
	if (err) {
		LOG_ERR("Failed to add Full-Speed configuration");
		return NULL;
	}
	/* doc configuration register end */

	/* doc functions register start */
	err = usbd_register_all_classes(&usbd_inst, USBD_SPEED_FS, 1, blocklist);
	if (err) {
		LOG_ERR("Failed to add register classes");
		return NULL;
	}
	/* doc functions register end */

	app_fix_code_triple(&usbd_inst, USBD_SPEED_FS);

	if (msg_cb != NULL) {
		/* doc device init-and-msg start */
		err = usbd_msg_register_cb(&usbd_inst, msg_cb);
		if (err) {
			LOG_ERR("Failed to register message callback");
			return NULL;
		}
		/* doc device init-and-msg end */
	}

	if (IS_ENABLED(CONFIG_usbd_inst_20_EXTENSION_DESC)) {
		(void)usbd_device_set_bcd_usb(&usbd_inst, USBD_SPEED_FS, 0x0201);
		(void)usbd_device_set_bcd_usb(&usbd_inst, USBD_SPEED_HS, 0x0201);

		err = usbd_add_descriptor(&usbd_inst, &usbnext);
		if (err) {
			LOG_ERR("Failed to add USB 2.0 Extension Descriptor");
			return NULL;
		}
	}

	return &usbd_inst;
}

struct usbd_context *app_usbd_init_device(usbd_msg_cb_t msg_cb)
{
	int err;

	if (usbd_inst_setup_device(msg_cb) == NULL) {
		return NULL;
	}

	/* doc device init start */
	err = usbd_init(&usbd_inst);
	if (err) {
		LOG_ERR("Failed to initialize device support");
		return NULL;
	}
	/* doc device init end */

	return &usbd_inst;
}
