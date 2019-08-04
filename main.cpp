/*
 * Copyright © 2013 Red Hat, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include "libevdev/config.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <linux/input.h>

#include "libevdev/libevdev/libevdev.h"

#define JETSON

static void
print_abs_bits(struct libevdev *dev, int axis)
{
	const struct input_absinfo *abs;

	if (!libevdev_has_event_code(dev, EV_ABS, axis))
		return;

	abs = libevdev_get_abs_info(dev, axis);

	printf("	Value	%6d\n", abs->value);
	printf("	Min	%6d\n", abs->minimum);
	printf("	Max	%6d\n", abs->maximum);
	if (abs->fuzz)
		printf("	Fuzz	%6d\n", abs->fuzz);
	if (abs->flat)
		printf("	Flat	%6d\n", abs->flat);
	if (abs->resolution)
		printf("	Resolution	%6d\n", abs->resolution);
}

static void
print_code_bits(struct libevdev *dev, unsigned int type, unsigned int max)
{
	unsigned int i;
	for (i = 0; i <= max; i++) {
		if (!libevdev_has_event_code(dev, type, i))
			continue;

		printf("    Event code %i (%s)\n", i, libevdev_event_code_get_name(type, i));
		if (type == EV_ABS)
			print_abs_bits(dev, i);
	}
}

static void
print_bits(struct libevdev *dev)
{
	unsigned int i;
	printf("Supported events:\n");

	for (i = 0; i <= EV_MAX; i++) {
		if (libevdev_has_event_type(dev, i))
			printf("  Event type %d (%s)\n", i, libevdev_event_type_get_name(i));
		switch(i) {
			case EV_KEY:
				print_code_bits(dev, EV_KEY, KEY_MAX);
				break;
			case EV_REL:
				print_code_bits(dev, EV_REL, REL_MAX);
				break;
			case EV_ABS:
				print_code_bits(dev, EV_ABS, ABS_MAX);
				break;
			case EV_LED:
				print_code_bits(dev, EV_LED, LED_MAX);
				break;
		}
	}
}

static void
print_props(struct libevdev *dev)
{
	unsigned int i;
	printf("Properties:\n");

	for (i = 0; i <= INPUT_PROP_MAX; i++) {
		if (libevdev_has_property(dev, i))
			printf("  Property type %d (%s)\n", i,
					libevdev_property_get_name(i));
	}
}

static int
print_event(struct input_event *ev)
{
	if (ev->type == EV_SYN)
		printf("Event: time %ld.%06ld, ++++++++++++++++++++ %s +++++++++++++++\n",
			#ifdef JETSON
				ev->time.tv_sec,
				ev->time.tv_usec,
			#else
				ev->input_event_sec,
				ev->input_event_usec,
			#endif
				libevdev_event_type_get_name(ev->type));
	else
		printf("Event: time %ld.%06ld, type %d (%s), code %d (%s), value %d\n",
		#ifdef JETSON
				ev->time.tv_sec,
				ev->time.tv_usec,
		#else
			ev->input_event_sec,
			ev->input_event_usec,
		#endif
			ev->type,
			libevdev_event_type_get_name(ev->type),
			ev->code,
			libevdev_event_code_get_name(ev->type, ev->code),
			ev->value);
	return 0;
}
static int
print_sync_event(struct input_event *ev)
{
	printf("SYNC: ");
	print_event(ev);
	return 0;
}

int
main(int argc, char **argv)
{
	struct libevdev *dev = NULL;
	const char *file;
	int fd;
	int rc = 1;

	if (argc < 2)
		goto out;

	file = argv[1];
	fd = open(file, O_RDONLY);
	if (fd < 0) {
		perror("Failed to open device");
		goto out;
	}

	rc = libevdev_new_from_fd(fd, &dev);
	if (rc < 0) {
		fprintf(stderr, "Failed to init libevdev (%s)\n", strerror(-rc));
		goto out;
	}

	printf("Input device ID: bus %#x vendor %#x product %#x\n",
			libevdev_get_id_bustype(dev),
			libevdev_get_id_vendor(dev),
			libevdev_get_id_product(dev));
	printf("Evdev version: %x\n", libevdev_get_driver_version(dev));
	printf("Input device name: \"%s\"\n", libevdev_get_name(dev));
	printf("Phys location: %s\n", libevdev_get_phys(dev));
	printf("Uniq identifier: %s\n", libevdev_get_uniq(dev));
	print_bits(dev);
	print_props(dev);

	do {
		struct input_event ev;
		rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL|LIBEVDEV_READ_FLAG_BLOCKING, &ev);
		if (rc == LIBEVDEV_READ_STATUS_SYNC) {
			printf("::::::::::::::::::::: dropped ::::::::::::::::::::::\n");
			while (rc == LIBEVDEV_READ_STATUS_SYNC) {
				print_sync_event(&ev);
				rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_SYNC, &ev);
			}
			printf("::::::::::::::::::::: re-synced ::::::::::::::::::::::\n");
		} else if (rc == LIBEVDEV_READ_STATUS_SUCCESS)
			print_event(&ev);
	} while (rc == LIBEVDEV_READ_STATUS_SYNC || rc == LIBEVDEV_READ_STATUS_SUCCESS || rc == -EAGAIN);

	if (rc != LIBEVDEV_READ_STATUS_SUCCESS && rc != -EAGAIN)
		fprintf(stderr, "Failed to handle events: %s\n", strerror(-rc));

	rc = 0;
out:
	libevdev_free(dev);

	return rc;
}




 def __init__(self):
        self.gamepad = InputDevice('/dev/input/event2')
        self.auto_mode = False
        self.max_speed = 20

    def switch_mode(self, mode=None):
        if mode is None:
            if self.auto_mode is True:
                self.auto_mode = False
            else:
                self.auto_mode = True
        else:
            if mode is "auto":
                self.auto_mode = True
            elif mode is "manual":
                self.auto_mode = False
        return self.auto_mode
    def go_forward(self, speed=self.max_speed):
        print ("going forward speed ", speed)

    def go_backward(self, speed=self.max_speed):
        print ("going backward speed ", speed)

    def turn_left(self, angle=0, speed=self.max_speed):
        print ("turning left speed ", speed, " angle: ", angle )

    def turn_right(self, angle=0, speed=self.max_speed):
        print ("turning right speed ", speed, " angle: ", angle )


    def drive(self):
        for event in self.gamepad.read_loop():
            print (event)
            #cat_event = categorize(event)
            #print (cat_event)
            if event.type == ecodes.EV_KEY:
                keyevent = categorize(event)
                # print (keyevent)
                if keyevent.keystate == KeyEvent.key_down:
                    if keyevent.keycode[0] == 'BTN_A':
                        r.go_backward()
                    elif keyevent.keycode[1] == 'BTN_Y':
                        r.go_forward()
                    elif keyevent.keycode[0] == 'BTN_B':
                        r.turn_right()
                    elif keyevent.keycode[1] == 'BTN_X':
                        r.turn_left()




if __name__ == "__main__":
    r = Robot()
    r.drive()

