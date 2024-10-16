# EV3 Simple Stopwatch
A barebones stopwatch that runs on the Lego Mindstorms EV3 using ev3dev.\
It polls for button press events from `/dev/input/by-path/platform-gpio_keys-event` and displays text by writing individual pixels to a memory-mapped frame buffer in `/dev/fb0`.

## Compilation (Linux)
1. Get [ev3dev](https://www.ev3dev.org/docs/getting-started/) running on your EV3 brick.
2. Clone the [ev3dev-c repository](https://github.com/in4lio/ev3dev-c). 
Follow the instructions for setting up Docker cross compilation.
3. `cd PATH/TO/ev3dev-c/eg/` and clone the ev3-simple-stopwatch repository while inside the `eg` directory.
4. Create a Docker container using `docker run --rm -it -h ev3 -v PATH/TO/ev3dev-c/:/home/robot/ev3dev-c -w /home/robot/ev3dev-c ev3cc /bin/bash`.
5. While inside the container, `cd eg/ev3-simple-stopwatch` to enter the stopwatch directory. 
Compile using `sudo make`. The executable will be located in the `Debug` folder. 
6. Transfer the executable to the EV3 brick. It can then be executed straight from Brickman's file explorer.
It may be necessary to edit execution permissions.

## Instructions
- Center button for starting/stopping time.
- Left button for resetting the timer while stopped.
- Right button for recording a split.
