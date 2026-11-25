# ENSC 351 Final Project: Autonomous Photo Booth

This is a working project based off the CMPT433 CMAKE file structure.

## Physical Start and Stop controls

Connect normally open buttons from physical pin 7 (GPIO4, Start) and physical
pin 31 (GPIO6, Stop) to ground, for example physical pin 9. The button HAL enables
internal pull-ups. The app also uses GPIO24, physical pin 18, to communicate with
the R5 firmware.

Build and deploy both the Linux app and the R5 firmware from this branch.
Start reads the light sensor and triggers the three-second LED countdown.
Stop cancels a pending countdown and clears the ring. A flash already started
finishes before the next sequence can begin. Additional Start presses during a
sequence are ignored. The terminal prints countdown and ready messages.

GPIO24 carries the existing 200 ms (flash) or 800 ms (no flash) start pulse.
During a countdown a 50 ms high pulse requests cancellation. The R5 ignores that
short pulse while idle and checks for cancellation between LED frames, every
5 ms during countdown waits. This requires the matching updated R5 firmware.
