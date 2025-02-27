/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Wayland Input
 *
 * Copyright 2014 Manuel Bachmann <tarnyko@tarnyko.net>
 * Copyright 2015 David Fort <contact@hardening-consulting.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <float.h>

#include <linux/input.h>

#include <winpr/assert.h>

#include <freerdp/locale/keyboard.h>
#include <freerdp/client/rdpei.h>
#include <uwac/uwac.h>

#include "wlfreerdp.h"
#include "wlf_input.h"

#define TAG CLIENT_TAG("wayland.input")

static BOOL scale_signed_coordinates(rdpContext* context, int32_t* x, int32_t* y,
                                     BOOL fromLocalToRDP)
{
	BOOL rc;
	UINT32 ux;
	UINT32 uy;
	WINPR_ASSERT(context);
	WINPR_ASSERT(x);
	WINPR_ASSERT(y);
	WINPR_ASSERT(*x >= 0);
	WINPR_ASSERT(*y >= 0);

	ux = (UINT32)*x;
	uy = (UINT32)*y;
	rc = wlf_scale_coordinates(context, &ux, &uy, fromLocalToRDP);
	WINPR_ASSERT(ux < INT32_MAX);
	WINPR_ASSERT(uy < INT32_MAX);
	*x = (int32_t)ux;
	*y = (int32_t)uy;
	return rc;
}

BOOL wlf_handle_pointer_enter(freerdp* instance, const UwacPointerEnterLeaveEvent* ev)
{
	uint32_t x, y;

	if (!instance || !ev || !instance->input)
		return FALSE;

	x = ev->x;
	y = ev->y;

	if (!wlf_scale_coordinates(instance->context, &x, &y, TRUE))
		return FALSE;

	WINPR_ASSERT(x <= UINT16_MAX);
	WINPR_ASSERT(y <= UINT16_MAX);
	return freerdp_input_send_mouse_event(instance->input, PTR_FLAGS_MOVE, (UINT16)x, (UINT16)y);
}

BOOL wlf_handle_pointer_motion(freerdp* instance, const UwacPointerMotionEvent* ev)
{
	uint32_t x, y;

	if (!instance || !ev || !instance->input)
		return FALSE;

	x = ev->x;
	y = ev->y;

	if (!wlf_scale_coordinates(instance->context, &x, &y, TRUE))
		return FALSE;

	WINPR_ASSERT(x <= UINT16_MAX);
	WINPR_ASSERT(y <= UINT16_MAX);
	return freerdp_input_send_mouse_event(instance->input, PTR_FLAGS_MOVE, (UINT16)x, (UINT16)y);
}

BOOL wlf_handle_pointer_buttons(freerdp* instance, const UwacPointerButtonEvent* ev)
{
	rdpInput* input;
	UINT16 flags = 0;
	UINT16 xflags = 0;
	uint32_t x, y;

	if (!instance || !ev || !instance->input)
		return FALSE;

	x = ev->x;
	y = ev->y;

	if (!wlf_scale_coordinates(instance->context, &x, &y, TRUE))
		return FALSE;

	input = instance->input;

	if (ev->state == WL_POINTER_BUTTON_STATE_PRESSED)
	{
		flags |= PTR_FLAGS_DOWN;
		xflags |= PTR_XFLAGS_DOWN;
	}

	switch (ev->button)
	{
		case BTN_LEFT:
			flags |= PTR_FLAGS_BUTTON1;
			break;

		case BTN_RIGHT:
			flags |= PTR_FLAGS_BUTTON2;
			break;

		case BTN_MIDDLE:
			flags |= PTR_FLAGS_BUTTON3;
			break;

		case BTN_SIDE:
			xflags |= PTR_XFLAGS_BUTTON1;
			break;

		case BTN_EXTRA:
			xflags |= PTR_XFLAGS_BUTTON2;
			break;

		default:
			return TRUE;
	}

	WINPR_ASSERT(x <= UINT16_MAX);
	WINPR_ASSERT(y <= UINT16_MAX);

	if ((flags & ~PTR_FLAGS_DOWN) != 0)
		return freerdp_input_send_mouse_event(input, flags, (UINT16)x, (UINT16)y);

	if ((xflags & ~PTR_XFLAGS_DOWN) != 0)
		return freerdp_input_send_extended_mouse_event(input, xflags, (UINT16)x, (UINT16)y);

	return FALSE;
}

BOOL wlf_handle_pointer_axis(freerdp* instance, const UwacPointerAxisEvent* ev)
{
	wlfContext* context;
	if (!instance || !instance->context || !ev)
		return FALSE;

	context = (wlfContext*)instance->context;
	return ArrayList_Append(context->events, ev);
}

BOOL wlf_handle_pointer_axis_discrete(freerdp* instance, const UwacPointerAxisEvent* ev)
{
	wlfContext* context;
	if (!instance || !instance->context || !ev)
		return FALSE;

	context = (wlfContext*)instance->context;
	return ArrayList_Append(context->events, ev);
}

static BOOL wlf_handle_wheel(freerdp* instance, uint32_t x, uint32_t y, uint32_t axis,
                             int32_t value)
{
	rdpInput* input;
	UINT16 flags = 0;
	int32_t direction;
	uint32_t avalue = (uint32_t)abs(value);

	WINPR_ASSERT(instance);

	input = instance->input;
	WINPR_ASSERT(input);

	if (!wlf_scale_coordinates(instance->context, &x, &y, TRUE))
		return FALSE;

	input = instance->input;

	direction = value;
	switch (axis)
	{
		case WL_POINTER_AXIS_VERTICAL_SCROLL:
			flags |= PTR_FLAGS_WHEEL;
			if (direction > 0)
				flags |= PTR_FLAGS_WHEEL_NEGATIVE;
			break;

		case WL_POINTER_AXIS_HORIZONTAL_SCROLL:
			flags |= PTR_FLAGS_HWHEEL;
			if (direction < 0)
				flags |= PTR_FLAGS_WHEEL_NEGATIVE;
			break;

		default:
			return FALSE;
	}

	/* Wheel rotation steps:
	 *
	 * positive: 0 ... 0xFF  -> slow ... fast
	 * negative: 0 ... 0xFF  -> fast ... slow
	 */

	while (avalue > 0)
	{
		const UINT16 cval = (avalue > 0xFF) ? 0xFF : (UINT16)avalue;
		UINT16 cflags = flags | cval;
		/* Convert negative values to 9bit twos complement */
		if (flags & PTR_FLAGS_WHEEL_NEGATIVE)
			cflags = (flags & 0xFF00) | (0x100 - cval);
		if (!freerdp_input_send_mouse_event(input, cflags, (UINT16)x, (UINT16)y))
			return FALSE;

		avalue -= cval;
	}
	return TRUE;
}

BOOL wlf_handle_pointer_frame(freerdp* instance, const UwacPointerFrameEvent* ev)
{
	BOOL success = TRUE;
	BOOL handle = FALSE;
	size_t x;
	wlfContext* context;
	enum wl_pointer_axis_source source = WL_POINTER_AXIS_SOURCE_CONTINUOUS;

	if (!instance || !ev || !instance->input || !instance->context)
		return FALSE;

	context = (wlfContext*)instance->context;

	for (x = 0; x < ArrayList_Count(context->events); x++)
	{
		UwacEvent* cev = ArrayList_GetItem(context->events, x);
		if (!cev)
			continue;
		if (cev->type == UWAC_EVENT_POINTER_SOURCE)
		{
			handle = TRUE;
			source = cev->mouse_source.axis_source;
		}
	}

	/* We need source events to determine how to interpret the data */
	if (handle)
	{
		for (x = 0; x < ArrayList_Count(context->events); x++)
		{
			UwacEvent* cev = ArrayList_GetItem(context->events, x);
			if (!cev)
				continue;

			switch (source)
			{
				/* If we have a mouse wheel, just use discrete data */
				case WL_POINTER_AXIS_SOURCE_WHEEL:
#if defined(WL_POINTER_AXIS_SOURCE_WHEEL_TILT_SINCE_VERSION)
				case WL_POINTER_AXIS_SOURCE_WHEEL_TILT:
#endif
					if (ev->type == UWAC_EVENT_POINTER_AXIS_DISCRETE)
					{
						/* Get the number of steps, multiply by default step width of 120 */
						int32_t val = cev->mouse_axis.value * 0x78;
						/* No wheel event received, success! */
						if (!wlf_handle_wheel(instance, cev->mouse_axis.x, cev->mouse_axis.y,
						                      cev->mouse_axis.axis, val))
							success = FALSE;
					}
					break;
					/* If we have a touch pad we get actual data, scale */
				case WL_POINTER_AXIS_SOURCE_FINGER:
				case WL_POINTER_AXIS_SOURCE_CONTINUOUS:
					if (cev->type == UWAC_EVENT_POINTER_AXIS)
					{
						double dval = wl_fixed_to_double(cev->mouse_axis.value);
						int32_t val = (int32_t)(dval * 0x78 / 10.0);
						if (!wlf_handle_wheel(instance, cev->mouse_axis.x, cev->mouse_axis.y,
						                      cev->mouse_axis.axis, val))
							success = FALSE;
					}
					break;
				default:
					break;
			}
		}
	}
	ArrayList_Clear(context->events);
	return success;
}

BOOL wlf_handle_pointer_source(freerdp* instance, const UwacPointerSourceEvent* ev)
{
	wlfContext* context;
	if (!instance || !instance->context || !ev)
		return FALSE;

	context = (wlfContext*)instance->context;
	return ArrayList_Append(context->events, ev);
}

BOOL wlf_handle_key(freerdp* instance, const UwacKeyEvent* ev)
{
	rdpInput* input;
	DWORD rdp_scancode;

	if (!instance || !ev || !instance->input)
		return FALSE;

	if (instance->context->settings->GrabKeyboard && ev->raw_key == KEY_RIGHTCTRL)
		wlf_handle_ungrab_key(instance, ev);

	input = instance->input;
	rdp_scancode = freerdp_keyboard_get_rdp_scancode_from_x11_keycode(ev->raw_key + 8);

	if (rdp_scancode == RDP_SCANCODE_UNKNOWN)
		return TRUE;

	return freerdp_input_send_keyboard_event_ex(input, ev->pressed, rdp_scancode);
}

BOOL wlf_handle_ungrab_key(freerdp* instance, const UwacKeyEvent* ev)
{
	wlfContext* context;
	if (!instance || !instance->context || !ev)
		return FALSE;

	context = (wlfContext*)instance->context;

	return UwacSeatInhibitShortcuts(context->seat, false) == UWAC_SUCCESS;
}

BOOL wlf_keyboard_enter(freerdp* instance, const UwacKeyboardEnterLeaveEvent* ev)
{
	if (!instance || !ev || !instance->input)
		return FALSE;

	((wlfContext*)instance->context)->focusing = TRUE;
	return TRUE;
}

BOOL wlf_keyboard_modifiers(freerdp* instance, const UwacKeyboardModifiersEvent* ev)
{
	rdpInput* input;
	UINT16 syncFlags;

	if (!instance || !ev || !instance->input)
		return FALSE;

	input = instance->input;
	syncFlags = 0;

	if (ev->modifiers & UWAC_MOD_CAPS_MASK)
		syncFlags |= KBD_SYNC_CAPS_LOCK;
	if (ev->modifiers & UWAC_MOD_NUM_MASK)
		syncFlags |= KBD_SYNC_NUM_LOCK;

	if (!((wlfContext*)instance->context)->focusing)
		return TRUE;

	((wlfContext*)instance->context)->focusing = FALSE;

	return freerdp_input_send_focus_in_event(input, syncFlags) &&
	       freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, 0, 0);
}

BOOL wlf_handle_touch_up(freerdp* instance, const UwacTouchUp* ev)
{
	int32_t x = 0, y = 0;
	size_t i;
	int touchId;
	int contactId;
	wlfContext* wlf;

	if (!instance || !ev || !instance->context)
		return FALSE;

	wlf = (wlfContext*)instance->context;
	touchId = ev->id;

	for (i = 0; i < MAX_CONTACTS; i++)
	{
		touchContact* contact = &wlf->contacts[i];
		if (contact->id == touchId)
		{
			contact->id = 0;
			x = (int32_t)contact->pos_x;
			y = (int32_t)contact->pos_y;
			break;
		}
	}

	if (i == MAX_CONTACTS)
		return FALSE;

	WLog_DBG(TAG, "%s called | event_id: %u | x: %u / y: %u", __FUNCTION__, touchId, x, y);

	if (!scale_signed_coordinates(instance->context, &x, &y, TRUE))
		return FALSE;

	RdpeiClientContext* rdpei = wlf->rdpei;

	if (wlf->contacts[i].emulate_mouse == TRUE)
	{
		UINT16 flags = 0;
		flags |= PTR_FLAGS_BUTTON1;

		WINPR_ASSERT(x <= UINT16_MAX);
		WINPR_ASSERT(y <= UINT16_MAX);
		return freerdp_input_send_mouse_event(instance->input, flags, (UINT16)x, (UINT16)y);
	}

	if (!rdpei)
		return FALSE;

	WINPR_ASSERT(rdpei->TouchEnd);
	rdpei->TouchEnd(rdpei, touchId, x, y, &contactId);

	return TRUE;
}

BOOL wlf_handle_touch_down(freerdp* instance, const UwacTouchDown* ev)
{
	int32_t x, y;
	int i;
	int touchId;
	int contactId;
	wlfContext* wlf;

	if (!instance || !ev || !instance->context)
		return FALSE;
	wlf = (wlfContext*)instance->context;
	x = ev->x;
	y = ev->y;
	touchId = ev->id;

	for (i = 0; i < MAX_CONTACTS; i++)
	{
		if (wlf->contacts[i].id == 0)
		{
			wlf->contacts[i].id = touchId;
			wlf->contacts[i].pos_x = x;
			wlf->contacts[i].pos_y = y;
			wlf->contacts[i].emulate_mouse = FALSE;
			break;
		}
	}

	if (i == MAX_CONTACTS)
		return FALSE;

	WLog_DBG(TAG, "%s called | event_id: %u | x: %u / y: %u", __FUNCTION__, touchId, x, y);

	if (!scale_signed_coordinates(instance->context, &x, &y, TRUE))
		return FALSE;

	RdpeiClientContext* rdpei = wlf->rdpei;

	// Emulate mouse click if touch is not possible, like in login screen
	if (!rdpei)
	{
		wlf->contacts[i].emulate_mouse = TRUE;

		UINT16 flags = 0;
		flags |= PTR_FLAGS_DOWN;
		flags |= PTR_FLAGS_MOVE;
		flags |= PTR_FLAGS_BUTTON1;

		WINPR_ASSERT(x <= UINT16_MAX);
		WINPR_ASSERT(y <= UINT16_MAX);
		return freerdp_input_send_mouse_event(instance->input, flags, (UINT16)x, (UINT16)y);
	}

	WINPR_ASSERT(rdpei);

	WINPR_ASSERT(rdpei->TouchBegin);
	rdpei->TouchBegin(rdpei, touchId, x, y, &contactId);

	return TRUE;
}

BOOL wlf_handle_touch_motion(freerdp* instance, const UwacTouchMotion* ev)
{
	int32_t x, y;
	int i;
	int touchId;
	int contactId;
	wlfContext* wlf;

	if (!instance || !ev || !instance->context)
		return FALSE;
	wlf = (wlfContext*)instance->context;
	x = ev->x;
	y = ev->y;
	touchId = ev->id;

	for (i = 0; i < MAX_CONTACTS; i++)
	{
		if (wlf->contacts[i].id == touchId)
		{
			if ((fabs(wlf->contacts[i].pos_x - x) < DBL_EPSILON) &&
			    (fabs(wlf->contacts[i].pos_y - y) < DBL_EPSILON))
			{
				return TRUE;
			}
			wlf->contacts[i].pos_x = x;
			wlf->contacts[i].pos_y = y;
			break;
		}
	}

	if (i == MAX_CONTACTS)
		return FALSE;

	WLog_DBG(TAG, "%s called | event_id: %u | x: %u / y: %u", __FUNCTION__, touchId, x, y);

	if (!scale_signed_coordinates(instance->context, &x, &y, TRUE))
		return FALSE;

	RdpeiClientContext* rdpei = ((wlfContext*)instance->context)->rdpei;

	if (wlf->contacts[i].emulate_mouse == TRUE)
	{
		UINT16 flags = 0;
		flags |= PTR_FLAGS_MOVE;

		WINPR_ASSERT(x <= UINT16_MAX);
		WINPR_ASSERT(y <= UINT16_MAX);
		return freerdp_input_send_mouse_event(instance->input, flags, (UINT16)x, (UINT16)y);
	}

	if (!rdpei)
		return FALSE;

	WINPR_ASSERT(rdpei->TouchUpdate);
	rdpei->TouchUpdate(rdpei, touchId, x, y, &contactId);

	return TRUE;
}
