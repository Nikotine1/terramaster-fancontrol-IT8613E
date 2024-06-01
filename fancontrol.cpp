// MIT License

// Copyright (c) 2019 Eudean Sun

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/io.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>

// These defaults can be overridden at the CLI
static bool debug = false; // Turn on/off logging
static int setpoint = 37;  // Default target hard drive operating temperature
static int pwminit = 128;  // Initial PWM value (50%)
static int interval = 10;  // How often we poll for temperatures
static int overheat = 50;  // Overheat limit where we drive the fans to 100%
static int pwmmin = 80;    // Never drive the fans below this PWM value (30%)
static double kp = 1.0;
static double ki = 0.0;
static double imax = 10.0;
static double kd = 0.0;

const static int pwmmax = 255; // Max PWM value, do not change
const static uint8_t port = 0x2e;
const static uint8_t fanspeed = 200;
static uint16_t ecbar = 0x00;

void iowrite(uint8_t reg, uint8_t val)
{
  outb(reg, port);
  outb(val, port + 1);
}

uint8_t ioread(uint8_t reg)
{
  outb(reg, port);
  return inb(port + 1);
}

void ecwrite(uint8_t reg, uint8_t val)
{
  outb(reg, ecbar + 5);
  outb(val, ecbar + 6);
}

uint8_t ecread(uint8_t reg)
{
  outb(reg, ecbar + 5);
  return inb(ecbar + 6);
}

int split_drive_names(const char *drive_list, char ***drives)
{
  int count = 1;
  for (const char *p = drive_list; *p; ++p)
  {
    if (*p == ',')
    {
      ++count;
    }
  }

  *drives = (char **)malloc(count * sizeof(char *));
  char *list_copy = strdup(drive_list);
  char *token = strtok(list_copy, ",");
  int i = 0;

  while (token != NULL)
  {
    (*drives)[i] = strdup(token);
    token = strtok(NULL, ",");
    ++i;
  }

  free(list_copy);
  return count;
}

void print_usage() {
    printf("Usage:\n"
           "\n"
           " fancontrol --drive_list=<drive_list> [--debug] [--setpoint] [--pwminit] [--interval] [--overheat] [--pwmmin] [--kp] [--ki] [--imax] [--kd]\n"
           "\n"
           "Arguments must be specified in order. Arguments that are not \n"
           "specified will take their default value.\n"
           "\n"
           "drive_list A comma-separated list of drive names between quotes e.g. 'sda,sdc' (required)\n"
           "debug     Enable (1) or disable (0) debug logs (default: 0)\n"
           "setpoint  Target maximum hard drive operating temperature in\n"
           "          degrees Celsius (default: 37)\n"
           "pwminit   Initial PWM value to write (default: 128)\n"
           "interval  How often we poll for temperatures in seconds (default: 10)\n"
           "overheat  Overheat temperature threshold in degrees Celsius above \n"
           "          which we drive the fans at maximum speed (default: 50)\n"
           "pwmmin    Never drive the fans below this PWM value (default: 80)\n"
           "kp        Proportional coefficient (default: 1.0)\n"
           "ki        Integral coefficient (default: 0.0)\n"
           "imax      Maximum integral value (default: 10.0)\n"
           "kd        Derivative coefficient (default: 0.0)\n");
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        print_usage();
        return 1;
    }

    const char *drive_list = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--drive_list=", 13) == 0) {
            drive_list = argv[i] + 13;
        } else if (strncmp(argv[i], "--debug=", 8) == 0) {
            debug = atoi(argv[i] + 8);
        } else if (strncmp(argv[i], "--setpoint=", 11) == 0) {
            setpoint = atoi(argv[i] + 11);
        } else if (strncmp(argv[i], "--pwminit=", 10) == 0) {
            pwminit = atoi(argv[i] + 10);
        } else if (strncmp(argv[i], "--interval=", 11) == 0) {
            interval = atoi(argv[i] + 11);
        } else if (strncmp(argv[i], "--overheat=", 11) == 0) {
            overheat = atoi(argv[i] + 11);
        } else if (strncmp(argv[i], "--pwmmin=", 9) == 0) {
            pwmmin = atoi(argv[i] + 9);
        } else if (strncmp(argv[i], "--kp=", 5) == 0) {
            kp = atof(argv[i] + 5);
        } else if (strncmp(argv[i], "--ki=", 5) == 0) {
            ki = atof(argv[i] + 5);
        } else if (strncmp(argv[i], "--imax=", 7) == 0) {
            imax = atof(argv[i] + 7);
        } else if (strncmp(argv[i], "--kd=", 5) == 0) {
            kd = atof(argv[i] + 5);
        } else {
            printf("Unknown parameter: %s\n", argv[i]);
            print_usage();
            return 1;
        }
    }

    if (drive_list == NULL)
    {
        printf("Error: drive_list is required.\n");
        print_usage();
        return 1;
    }

    char **drives = NULL;
    int count = split_drive_names(drive_list, &drives);

    if (count == 0)
    {
        return 1;
    }

    // Obtain access to IO ports
    iopl(3);

    // Initialize the IT8613E
    outb(0x87, port);
    outb(0x01, port);
    outb(0x55, port);
    outb(0x55, port);

    // Sanity check that this is the IT8613E
    assert(ioread(0x20) == 0x86);
    assert(ioread(0x21) == 0x13);

    // Set LDN = 4 to access environment registers
    iowrite(0x07, 0x04);

    // Activate environment controller (EC)
    iowrite(0x30, 0x01);

    // Read EC bar
    ecbar = (ioread(0x60) << 8) + ioread(0x61);

    // Initialize the PWM value
    uint8_t pwm = pwminit;
    ecwrite(0x6b, pwm);
    ecwrite(0x73, pwm);

    // Set software operation
    ecwrite(0x16, 0x00);
    ecwrite(0x17, 0x00);

    double integral = 0;
    double error = 0;
    double prev_error = 0;
    double pout = 0;
    double iout = 0;
    double dout = 0;
    double timediff = 0;
    int maxtemp = 0;
    double pwmtemp = pwm;
    struct timespec curtime;
    struct timespec lasttime;

    clock_gettime(CLOCK_MONOTONIC, &lasttime);

    while (true)
    {
        maxtemp = 0;
        char smartcmd[200];
        char tempstring[20];

        // Execute the smartctl command for each drive in the list
        for (int i = 0; i < count; ++i)
        {
            snprintf(smartcmd, sizeof(smartcmd),
                     "smartctl -A -d sat /dev/%s | "
                     "grep Temperature_Celsius | "
                     "awk '{print $10}'",
                     drives[i]);
            FILE *pipe = popen(smartcmd, "r");
            if (!pipe)
            {
                continue;
            }
            fgets(tempstring, sizeof(tempstring), pipe);
            pclose(pipe);
            int temp = atoi(tempstring);
            if (temp > maxtemp)
            {
                maxtemp = temp;
            }
            if (debug)
            {
                printf("Drive: /dev/%s has temperature %d\n", drives[i], temp);
            }
        }

        // Calculate time since last poll
        clock_gettime(CLOCK_MONOTONIC, &curtime);
        timediff = ((1000000000 * (curtime.tv_sec - lasttime.tv_sec) +
                     (curtime.tv_nsec - lasttime.tv_nsec))) /
                   1000000000.0;
        if (timediff == 0)
        {
            goto endloop;
        }
        lasttime.tv_sec = curtime.tv_sec;
        lasttime.tv_nsec = curtime.tv_nsec;

        // Calculate PID values
        error = maxtemp - setpoint;
        pout = kp * error;

        iout += ki * error * timediff;
        if (iout > imax)
        {
            iout = imax;
        }
        else if (iout < -imax)
        {
            iout = -imax;
        }

        dout = kd * (error - prev_error) / timediff;
        prev_error = error;

        // Calculate new PWM value
        pwmtemp += pout + iout + dout;
        if ((maxtemp > overheat) || (pwmtemp > pwmmax))
        {
            pwmtemp = pwmmax;
        }
        else if (pwmtemp < pwmmin)
        {
            pwmtemp = pwmmin;
        }
        pwm = pwmtemp;

        if (debug)
        {
            printf("maxtemp = %d, error = %f, pout = %f, iout = %f, dout = %f, "
                   "pwmtemp = %f, pwm = %d\n",
                   maxtemp, error, pout, iout, dout, pwmtemp, pwm);
        }
        clock_gettime(CLOCK_MONOTONIC, &lasttime);

        // Write new PWM
        ecwrite(0x6b, pwm);
        ecwrite(0x73, pwm);

    endloop:
        sleep(interval);
    }

    for (int i = 0; i < count; ++i)
    {
        free(drives[i]);
    }
    free(drives);
    iopl(0);
    return 0;
}
