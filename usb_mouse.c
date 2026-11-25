/* usb_mouse.c - USB HID mouse as a GRUB input terminal.
 *
 * Turns the mouse wheel into Up/Down key presses, so the boot menu can be
 * scrolled with the wheel, and a left click into Enter.  Pointer motion is
 * ignored.
 *
 * Unlike usb_keyboard this does NOT switch the device to the HID boot
 * protocol (boot mouse reports have no wheel).  It reads the HID report
 * descriptor and locates the button and wheel fields in the report
 * protocol.  If that fails, it falls back to the common layout
 * [buttons, x, y, wheel].
 *
 * Copyright (C) 2026
 * Based on term/usb_keyboard.c, Copyright (C) 2008, 2009 Free Software
 * Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <grub/term.h>
#include <grub/time.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/usb.h>
#include <grub/dl.h>
#include <grub/env.h>
#include <grub/command.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define USB_HID_SET_IDLE	0x0A
#define USB_HID_SET_PROTOCOL	0x0B
#define USB_HID_PROTOCOL_REPORT	1

#define USB_DESC_HID		0x21
#define USB_DESC_HID_REPORT	0x22
#define USB_DESC_INTERFACE	0x04

#define USB_HID_BOOT_SUBCLASS	0x01
#define USB_HID_MOUSE_PROTOCOL	0x02

#define MAX_MICE	4
#define MAX_REPORT	64
#define MAX_RDESC	1024
#define MAX_PENDING	16

/* Where a field lives inside a report.  */
struct field
{
  int present;
  int report_id;		/* 0: device uses no report IDs.  */
  unsigned bit;			/* Bit offset after the report ID byte.  */
  unsigned size;		/* In bits.  */
  int is_signed;
};

struct mouse_layout
{
  int uses_ids;
  struct field button1;
  struct field wheel;
};

struct grub_usb_mouse_data
{
  grub_usb_device_t usbdev;
  int interfno;
  struct grub_usb_desc_endp *endp;
  grub_usb_transfer_t transfer;
  grub_uint8_t report[MAX_REPORT];
  unsigned report_len;
  unsigned report_len_max;
  struct mouse_layout layout;
  int layout_from_desc;
  int dead;
  int button_down;
  int pending;			/* >0: Up presses queued, <0: Down presses.  */
  int pending_enter;
  unsigned long reports;
  int last_err;
};

static struct grub_term_input grub_usb_mice[MAX_MICE];

/* ---- HID report descriptor parsing ------------------------------------ */

static grub_uint32_t
item_udata (const grub_uint8_t *p, int size)
{
  switch (size)
    {
    case 1: return p[0];
    case 2: return p[0] | (p[1] << 8);
    case 4: return p[0] | (p[1] << 8) | (p[2] << 16) | ((grub_uint32_t) p[3] << 24);
    default: return 0;
    }
}

static grub_int32_t
item_sdata (const grub_uint8_t *p, int size)
{
  switch (size)
    {
    case 1: return (grub_int8_t) p[0];
    case 2: return (grub_int16_t) (p[0] | (p[1] << 8));
    case 4: return (grub_int32_t) item_udata (p, 4);
    default: return 0;
    }
}

#define MAX_USAGES 16
#define MAX_IDS 16

/* Returns 1 if a wheel was found.  */
static int
parse_report_descriptor (const grub_uint8_t *d, unsigned len,
			 struct mouse_layout *out)
{
  /* Global state.  */
  grub_uint32_t usage_page = 0;
  grub_int32_t logical_min = 0;
  grub_uint32_t report_size = 0, report_count = 0;
  int report_id = 0;
  /* Local state.  */
  grub_uint32_t usages[MAX_USAGES];
  unsigned n_usages = 0;
  grub_uint32_t usage_min = 0, usage_max = 0;
  int have_range = 0;
  /* Running bit offset per report ID.  */
  int ids[MAX_IDS];
  unsigned offsets[MAX_IDS];
  unsigned n_ids = 0;
  unsigned pos = 0;

  grub_memset (out, 0, sizeof (*out));

  while (pos < len)
    {
      grub_uint8_t prefix = d[pos];
      int size, type, tag;
      const grub_uint8_t *data;

      if (prefix == 0xfe)
	{
	  /* Long item: skip.  */
	  if (pos + 2 >= len)
	    break;
	  pos += 3 + d[pos + 1];
	  continue;
	}

      size = prefix & 3;
      if (size == 3)
	size = 4;
      type = (prefix >> 2) & 3;
      tag = prefix >> 4;
      if (pos + 1 + size > len)
	break;
      data = d + pos + 1;
      pos += 1 + size;

      if (type == 1)		/* Global.  */
	switch (tag)
	  {
	  case 0x0: usage_page = item_udata (data, size); break;
	  case 0x1: logical_min = item_sdata (data, size); break;
	  case 0x7: report_size = item_udata (data, size); break;
	  case 0x8:
	    report_id = item_udata (data, size);
	    out->uses_ids = 1;
	    break;
	  case 0x9: report_count = item_udata (data, size); break;
	  }
      else if (type == 2)	/* Local.  */
	{
	  grub_uint32_t v = item_udata (data, size);
	  /* A 4-byte usage carries its own usage page.  */
	  if (size < 4 && tag <= 2)
	    v |= usage_page << 16;
	  switch (tag)
	    {
	    case 0x0:
	      if (n_usages < MAX_USAGES)
		usages[n_usages++] = v;
	      break;
	    case 0x1: usage_min = v; have_range = 1; break;
	    case 0x2: usage_max = v; have_range = 1; break;
	    }
	}
      else if (type == 0)	/* Main.  */
	{
	  if (tag == 0x8)	/* Input.  */
	    {
	      grub_uint32_t flags = item_udata (data, size);
	      unsigned k, *off = NULL;
	      grub_uint32_t i;

	      for (k = 0; k < n_ids; k++)
		if (ids[k] == report_id)
		  off = &offsets[k];
	      if (!off && n_ids < MAX_IDS)
		{
		  ids[n_ids] = report_id;
		  offsets[n_ids] = 0;
		  off = &offsets[n_ids++];
		}
	      if (!off)
		return 0;

	      /* Variable (not array, not constant) fields only.  */
	      if (!(flags & 1) && (flags & 2) && report_count <= 64)
		for (i = 0; i < report_count; i++)
		  {
		    grub_uint32_t usage;
		    struct field *f = NULL;

		    if (have_range)
		      usage = usage_min + i;
		    else if (n_usages)
		      usage = usages[i < n_usages ? i : n_usages - 1];
		    else
		      usage = 0;

		    if (have_range && usage > usage_max)
		      usage = usage_max;

		    if (usage == 0x00090001 && !out->button1.present)
		      f = &out->button1;
		    else if (usage == 0x00010038 && !out->wheel.present)
		      f = &out->wheel;

		    if (f)
		      {
			f->present = 1;
			f->report_id = report_id;
			f->bit = *off + i * report_size;
			f->size = report_size;
			f->is_signed = logical_min < 0;
		      }
		  }
	      *off += report_size * report_count;
	    }
	  /* Every main item resets local state.  */
	  n_usages = 0;
	  have_range = 0;
	  usage_min = usage_max = 0;
	}
    }

  return out->wheel.present;
}

static void
default_layout (struct mouse_layout *l)
{
  grub_memset (l, 0, sizeof (*l));
  l->button1.present = 1;
  l->button1.size = 1;
  l->wheel.present = 1;
  l->wheel.bit = 24;
  l->wheel.size = 8;
  l->wheel.is_signed = 1;
}

/* Extract a field from a report.  Returns 0 and leaves *val alone when the
   report does not contain the field.  */
static int
get_field (const struct grub_usb_mouse_data *m, const struct field *f,
	   grub_int32_t *val)
{
  const grub_uint8_t *r = m->report;
  unsigned len = m->report_len;
  grub_uint32_t v = 0;
  unsigned i;

  if (!f->present || f->size == 0 || f->size > 32)
    return 0;
  if (m->layout.uses_ids)
    {
      if (len < 1 || r[0] != f->report_id)
	return 0;
      r++;
      len--;
    }
  if (f->bit + f->size > len * 8)
    return 0;

  for (i = 0; i < f->size; i++)
    {
      unsigned b = f->bit + i;
      if (r[b / 8] & (1 << (b % 8)))
	v |= (grub_uint32_t) 1 << i;
    }
  if (f->is_signed && f->size < 32 && (v & ((grub_uint32_t) 1 << (f->size - 1))))
    v |= ~(((grub_uint32_t) 1 << f->size) - 1);
  *val = (grub_int32_t) v;
  return 1;
}

/* Find the HID class descriptor that follows interface INTERFNO and return
   the length of its report descriptor, or 0.  */
static unsigned
report_descriptor_length (grub_usb_device_t usbdev, int configno, int interfno)
{
  struct grub_usb_desc_config *conf = usbdev->config[configno].descconf;
  grub_uint8_t *base = (grub_uint8_t *) conf;
  grub_uint8_t *p = (grub_uint8_t *) usbdev->config[configno].interf[interfno].descif;
  grub_uint8_t *end = base + grub_le_to_cpu16 (conf->totallen);

  if (p < base || p >= end)
    return 0;
  p += p[0];
  while (p + 2 <= end && p[0] >= 2)
    {
      if (p[1] == USB_DESC_INTERFACE)
	break;
      if (p[1] == USB_DESC_HID && p[0] >= 9 && p + 9 <= end)
	{
	  unsigned n = p[5], k;
	  for (k = 0; k < n && p + 9 + 3 * k <= end && 9 + 3 * k <= p[0]; k++)
	    if (p[6 + 3 * k] == USB_DESC_HID_REPORT)
	      return p[7 + 3 * k] | (p[8 + 3 * k] << 8);
	}
      p += p[0];
    }
  return 0;
}

/* ---- Terminal --------------------------------------------------------- */

static void
free_mouse (unsigned i)
{
  struct grub_usb_mouse_data *data = grub_usb_mice[i].data;

  if (data->transfer)
    grub_usb_cancel_transfer (data->transfer);
  grub_term_unregister_input (&grub_usb_mice[i]);
  grub_free ((char *) grub_usb_mice[i].name);
  grub_usb_mice[i].name = NULL;
  grub_free (data);
  grub_usb_mice[i].data = 0;
}

static void
grub_usb_mouse_detach (grub_usb_device_t usbdev,
		       int config __attribute__ ((unused)),
		       int interface __attribute__ ((unused)))
{
  unsigned i;

  for (i = 0; i < ARRAY_SIZE (grub_usb_mice); i++)
    {
      struct grub_usb_mouse_data *data = grub_usb_mice[i].data;
      if (data && data->usbdev == usbdev)
	free_mouse (i);
    }
}

static void
handle_report (struct grub_usb_mouse_data *m)
{
  grub_int32_t wheel = 0, button = 0;

  m->reports++;

  if (get_field (m, &m->layout.wheel, &wheel) && wheel)
    {
      /* Positive = wheel rolled away from the user = move up.  */
      m->pending += wheel;
      if (m->pending > MAX_PENDING)
	m->pending = MAX_PENDING;
      if (m->pending < -MAX_PENDING)
	m->pending = -MAX_PENDING;
    }

  if (get_field (m, &m->layout.button1, &button))
    {
      if (button && !m->button_down)
	m->pending_enter = 1;
      m->button_down = !!button;
    }
}

static int
grub_usb_mouse_getkey (struct grub_term_input *term)
{
  struct grub_usb_mouse_data *m = term->data;
  grub_usb_err_t err;
  grub_size_t actual;

  if (!m->dead)
    {
      err = grub_usb_check_transfer (m->transfer, &actual);
      if (err != GRUB_USB_ERR_WAIT)
	{
	  m->last_err = err;
	  if (!err && actual > 0)
	    {
	      m->report_len = actual > MAX_REPORT ? MAX_REPORT : actual;
	      handle_report (m);
	    }
	  m->transfer = grub_usb_bulk_read_background (m->usbdev, m->endp,
						       m->report_len_max,
						       m->report);
	  if (!m->transfer)
	    {
	      grub_errno = GRUB_ERR_NONE;
	      m->dead = 1;
	    }
	}
    }

  if (m->pending > 0)
    {
      m->pending--;
      return GRUB_TERM_KEY_UP;
    }
  if (m->pending < 0)
    {
      m->pending++;
      return GRUB_TERM_KEY_DOWN;
    }
  if (m->pending_enter)
    {
      m->pending_enter = 0;
      return '\r';
    }
  return GRUB_TERM_NO_KEY;
}

static int
grub_usb_mouse_getkeystatus (struct grub_term_input *term __attribute__ ((unused)))
{
  return 0;
}

static struct grub_term_input grub_usb_mouse_term =
  {
    .getkey = grub_usb_mouse_getkey,
    .getkeystatus = grub_usb_mouse_getkeystatus,
    .next = 0
  };

static int
grub_usb_mouse_attach (grub_usb_device_t usbdev, int configno, int interfno)
{
  struct grub_usb_desc_if *descif
    = usbdev->config[configno].interf[interfno].descif;
  struct grub_usb_mouse_data *data;
  struct grub_usb_desc_endp *endp = NULL;
  unsigned curnum, rlen;
  int j, is_boot_mouse;

  for (curnum = 0; curnum < ARRAY_SIZE (grub_usb_mice); curnum++)
    if (!grub_usb_mice[curnum].data)
      break;
  if (curnum == ARRAY_SIZE (grub_usb_mice))
    return 0;

  /* Leave keyboards to usb_keyboard.  */
  if (descif->subclass == USB_HID_BOOT_SUBCLASS
      && descif->protocol != USB_HID_MOUSE_PROTOCOL)
    return 0;
  is_boot_mouse = (descif->subclass == USB_HID_BOOT_SUBCLASS
		   && descif->protocol == USB_HID_MOUSE_PROTOCOL);

  for (j = 0; j < descif->endpointcnt; j++)
    {
      endp = &usbdev->config[configno].interf[interfno].descendp[j];
      if ((endp->endp_addr & 128)
	  && grub_usb_get_ep_type (endp) == GRUB_USB_EP_INTERRUPT)
	break;
    }
  if (j == descif->endpointcnt)
    return 0;

  data = grub_zalloc (sizeof (*data));
  if (!data)
    {
      grub_errno = GRUB_ERR_NONE;
      return 0;
    }
  data->usbdev = usbdev;
  data->interfno = interfno;
  data->endp = endp;
  data->report_len_max = grub_le_to_cpu16 (endp->maxpacket) & 0x7ff;
  if (data->report_len_max == 0 || data->report_len_max > MAX_REPORT)
    data->report_len_max = MAX_REPORT;

  grub_usb_set_configuration (usbdev, configno + 1);

  /* Read and parse the report descriptor.  */
  rlen = report_descriptor_length (usbdev, configno, interfno);
  if (rlen > 0 && rlen <= MAX_RDESC)
    {
      grub_uint8_t *rdesc = grub_malloc (rlen);
      if (rdesc)
	{
	  if (grub_usb_control_msg (usbdev,
				    GRUB_USB_REQTYPE_IN
				    | GRUB_USB_REQTYPE_STANDARD
				    | GRUB_USB_REQTYPE_TARGET_INTERF,
				    GRUB_USB_REQ_GET_DESCRIPTOR,
				    USB_DESC_HID_REPORT << 8, interfno,
				    rlen, (char *) rdesc) == GRUB_USB_ERR_NONE)
	    data->layout_from_desc
	      = parse_report_descriptor (rdesc, rlen, &data->layout);
	  grub_free (rdesc);
	}
    }
  grub_errno = GRUB_ERR_NONE;

  if (!data->layout_from_desc)
    {
      /* A non-boot HID interface without a wheel is something else (a
         keyboard's consumer-control interface, a gamepad...).  */
      if (!is_boot_mouse)
	{
	  grub_free (data);
	  return 0;
	}
      default_layout (&data->layout);
    }

  grub_dprintf ("usb_mouse", "%04x:%04x if %d: ids=%d btn1 %d/%u wheel id %d bit %u size %u%s\n",
		grub_le_to_cpu16 (usbdev->descdev.vendorid),
		grub_le_to_cpu16 (usbdev->descdev.prodid), interfno,
		data->layout.uses_ids, data->layout.button1.present,
		data->layout.button1.bit, data->layout.wheel.report_id,
		data->layout.wheel.bit, data->layout.wheel.size,
		data->layout_from_desc ? "" : " (default layout)");

  /* Make sure we get the full report, not the 3-byte boot one.  */
  grub_usb_control_msg (usbdev, GRUB_USB_REQTYPE_CLASS_INTERFACE_OUT,
			USB_HID_SET_PROTOCOL, USB_HID_PROTOCOL_REPORT,
			interfno, 0, 0);
  /* Only report on change.  */
  grub_usb_control_msg (usbdev, GRUB_USB_REQTYPE_CLASS_INTERFACE_OUT,
			USB_HID_SET_IDLE, 0, interfno, 0, 0);
  grub_errno = GRUB_ERR_NONE;

  data->transfer = grub_usb_bulk_read_background (usbdev, endp,
						  data->report_len_max,
						  data->report);
  if (!data->transfer)
    {
      grub_errno = GRUB_ERR_NONE;
      grub_free (data);
      return 0;
    }

  grub_memcpy (&grub_usb_mice[curnum], &grub_usb_mouse_term,
	       sizeof (grub_usb_mice[curnum]));
  grub_usb_mice[curnum].data = data;
  grub_usb_mice[curnum].name = grub_xasprintf ("usb_mouse%d", curnum);
  if (!grub_usb_mice[curnum].name)
    {
      grub_errno = GRUB_ERR_NONE;
      grub_usb_cancel_transfer (data->transfer);
      grub_free (data);
      grub_usb_mice[curnum].data = 0;
      return 0;
    }
  usbdev->config[configno].interf[interfno].detach_hook = grub_usb_mouse_detach;

  grub_term_register_input_active ("usb_mouse", &grub_usb_mice[curnum]);
  return 1;
}

/* ---- usb_mouse_probe command ------------------------------------------- */

/* usb_mouse_probe [VAR]: scan the USB buses, wait up to 5 s for a mouse,
   print what was found and optionally store it in VAR (for save_env).  */
static grub_err_t
grub_cmd_usb_mouse_probe (grub_command_t cmd __attribute__ ((unused)),
			  int argc, char **args)
{
  grub_uint64_t deadline = grub_get_time_ms () + 5000;
  char buf[512];
  char *p = buf, *end = buf + sizeof (buf);
  unsigned i, found = 0;

  do
    {
      grub_usb_poll_devices (1);
      for (i = 0; i < ARRAY_SIZE (grub_usb_mice); i++)
	if (grub_usb_mice[i].data)
	  found++;
      if (found)
	break;
      grub_millisleep (100);
    }
  while (grub_get_time_ms () < deadline);

  if (!found)
    grub_snprintf (p, end - p, "none");
  else
    for (i = 0; i < ARRAY_SIZE (grub_usb_mice) && p < end - 1; i++)
      {
	struct grub_usb_mouse_data *m = grub_usb_mice[i].data;
	if (!m)
	  continue;
	p += grub_snprintf (p, end - p,
			    "%s%04x:%04x speed=%d hub=%d/%d ep=%02x/%u "
			    "wheel=%s:id%d,bit%u,%ubit btn1=%s:bit%u "
			    "reports=%lu err=%d %s",
			    p == buf ? "" : "; ",
			    grub_le_to_cpu16 (m->usbdev->descdev.vendorid),
			    grub_le_to_cpu16 (m->usbdev->descdev.prodid),
			    m->usbdev->speed, m->usbdev->split_hubaddr,
			    m->usbdev->split_hubport, m->endp->endp_addr,
			    m->report_len_max,
			    m->layout_from_desc ? "desc" : "default",
			    m->layout.wheel.report_id, m->layout.wheel.bit,
			    m->layout.wheel.size,
			    m->layout.button1.present ? "yes" : "no",
			    m->layout.button1.bit, m->reports, m->last_err,
			    m->dead ? "dead" : "ok");
      }
  grub_errno = GRUB_ERR_NONE;

  grub_printf ("usb_mouse: %s\n", buf);
  if (argc > 0)
    return grub_env_set (args[0], buf);
  return GRUB_ERR_NONE;
}

static struct grub_usb_attach_desc attach_hook =
{
  .class = GRUB_USB_CLASS_HID,
  .hook = grub_usb_mouse_attach
};

static grub_command_t cmd_probe;

GRUB_MOD_INIT(usb_mouse)
{
  grub_usb_register_attach_hook_class (&attach_hook);
  cmd_probe = grub_register_command ("usb_mouse_probe", grub_cmd_usb_mouse_probe,
				     N_("[VARNAME]"),
				     N_("Detect USB mice for wheel menu navigation."));
}

GRUB_MOD_FINI(usb_mouse)
{
  unsigned i;

  grub_unregister_command (cmd_probe);
  for (i = 0; i < ARRAY_SIZE (grub_usb_mice); i++)
    if (grub_usb_mice[i].data)
      free_mouse (i);
  grub_usb_unregister_attach_hook_class (&attach_hook);
}
