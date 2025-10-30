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
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <sys/io.h>

// These defaults can be overridden at the CLI
static bool debug = false; // Turn on/off logging
static int setpoint = 37;  // Default target hard drive operating temperature
static int pwminit = 128;  // Initial PWM value (50%)
static int interval = 10;  // How often we poll for temperatures
static int overheat = 45;  // Overheat limit where we drive the fans to 100%
static int pwmmin = 80;    // Never drive the fans below this PWM value (30%)
static double kp = 50.0;
static double ki = 0.5;
static double imax = 255.0;
static double kd = 0.0;
const static int pwmmax = 255.0; // Max PWM value, do not change
const static uint8_t port = 0x2e;
const static uint8_t fanspeed = 200;
static uint16_t ecbar = 0x00;
static char *graphite_server = NULL;
static int graphite_port = 0;
static int cputemp_max_values = 10; // Number of values for rolling average of cpu temperature

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
           " fancontrol --drive_list=<drive_list> [--debug=<value>] [--setpoint=<value>] [--pwminit=<value>] [--interval=<value>] [--overheat=<value>] [--pwmmin=<value>] [--kp=<value>] [--ki=<value>] [--imax=<value>] [--kd=<value>] [--cpu_avg=<value>] [--graphite_server=<ip:port>]\n"
           "\n"
           "drive_list        A comma-separated list of drive names between quotes e.g. 'sda,sdc' (required)\n"
           "debug             Enable (1) or disable (0) debug logs (default: 0)\n"
           "setpoint          Target maximum hard drive operating temperature in\n"
           "                  degrees Celsius (default: 37)\n"
           "pwminit           Initial PWM value to write (default: 128)\n"
           "interval          How often we poll for temperatures in seconds (default: 10)\n"
           "overheat          Overheat temperature threshold in degrees Celsius above \n"
           "                  which we drive the fans at maximum speed (default: 45)\n"
           "pwmmin            Never drive the fans below this PWM value (default: 80)\n"
           "kp                Proportional coefficient (default: 50.0)\n"
           "ki                Integral coefficient (default: 0.5)\n"
           "imax              Maximum integral value (default: 255.0)\n"
           "kd                Derivative coefficient (default: 0.0)\n"
           "cpu_avg           Number of CPU temperature measurements for rolling average (default: 10)\n"
           "graphite_server   Graphite server IP address and port in the format <ip:port> (optional)\n");
}

void send_to_graphite(int sockfd, const char *message) {
    send(sockfd, message, strlen(message), 0);
}

int calculate_new_pwm(double error, double timediff, double &integral, double &prev_error, int graphite_sockfd) {
    integral += error * timediff;

    if (integral > imax) integral = imax;
    else if (integral < -imax) integral = -imax;

    double derivative = (error - prev_error) / timediff;
    prev_error = error;

    // Compute the new PWM
    double newPWM_double = pwminit + kp * error + ki * integral + kd * derivative;

    if (newPWM_double > pwmmax) newPWM_double = pwmmax;
    else if (newPWM_double < pwmmin) newPWM_double = pwmmin;

    int newPWM = static_cast<int>(newPWM_double);

    // Send pid values to Graphite
    if (graphite_sockfd > 0) {
        char message[256];

        snprintf(message, sizeof(message), "fancontrol.p %f %ld\n", error * kp, time(NULL));
        send_to_graphite(graphite_sockfd, message);

        snprintf(message, sizeof(message), "fancontrol.i %f %ld\n", integral * ki, time(NULL));
        send_to_graphite(graphite_sockfd, message);

        snprintf(message, sizeof(message), "fancontrol.d %f %ld\n", derivative * kd, time(NULL));
        send_to_graphite(graphite_sockfd, message);
    }

    return newPWM;
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
        } else if (strncmp(argv[i], "--cpu_avg=", 10) == 0) {
            cputemp_max_values = atof(argv[i] + 10);
        } else if (strncmp(argv[i], "--graphite_server=", 18) == 0) {
            char *server_info = argv[i] + 18;
            char *colon_pos = strchr(server_info, ':');
            if (colon_pos) {
                *colon_pos = '\0';
                graphite_server = server_info;
                graphite_port = atoi(colon_pos + 1);
            } else {
                printf("Invalid Graphite server format. Expected <ip:port>\n");
                return 1;
            }
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

    // Sanity checks commented out so that it works for both chips.
    // Sanity check that this is the IT8772E
    //assert(ioread(0x20) == 0x87);
    //assert(ioread(0x21) == 0x72);

    // Sanity check that this is the IT8613E
    //assert(ioread(0x20) == 0x86);
    //assert(ioread(0x21) == 0x13);

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
    double derivative = 0;
    double error = 0;
    double prev_error = 0;
    double timediff = 0;
    int maxtemp = 0;
    struct timespec curtime;
    struct timespec lasttime;

    int *cputemp_values = (int *)calloc(cputemp_max_values, sizeof(int));  // Store last 10 values
    int cputemp_index = 0;  // Circular index
    int cputemp_count = 0;  // Number of values stored
    int cputemp_sum = 0;    // Sum of stored values
    int cpu_avg_temp = 0; // Average CPU temperature

    // Setup graphite socket
    int graphite_sockfd = -1;
    if (graphite_server) {
        struct sockaddr_in servaddr;
        graphite_sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (graphite_sockfd < 0) {
            printf("Error: Could not create socket\n");
        }
        else
        {
            memset(&servaddr, 0, sizeof(servaddr));
            servaddr.sin_family = AF_INET;
            servaddr.sin_port = htons(graphite_port);

            // Convert IPv4 and IPv6 addresses from text to binary form
            if (inet_pton(AF_INET, graphite_server, &servaddr.sin_addr) <= 0) {
                printf("Invalid address/ Address not supported \n");
                close(graphite_sockfd);
                graphite_sockfd = -1;
            }
            else if (connect(graphite_sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
                printf("Connection Failed \n");
                close(graphite_sockfd);
                graphite_sockfd = -1;
            }
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &lasttime);

    while (true)
    {
        maxtemp = 0;
        char smartcmd[200];
        char tempstring[20];

        // Execute the smartctl command for each drive in the list
        for (int i = 0; i < count; ++i)
        {
            snprintf(smartcmd, sizeof(smartcmd), "smartctl -A -d sat /dev/%s | grep Temperature_Celsius | awk '{print $10}'", drives[i]);

            FILE *pipe = popen(smartcmd, "r");
            if (!pipe)
            {
                continue;
            }
            fgets(tempstring, sizeof(tempstring), pipe);
            pclose(pipe);
            int temp = atoi(tempstring);

            if (temp > maxtemp) maxtemp = temp;

            if (debug) printf("Drive: /dev/%s has temperature %d\n", drives[i], temp);

            // Send disk temperature to Graphite
            if (graphite_sockfd > 0) {
                char message[256];

                snprintf(message, sizeof(message), "fancontrol.%s %d %ld\n", drives[i], temp, time(NULL));
                send_to_graphite(graphite_sockfd, message);
            }
        }

        // Get CPU temperature
        FILE *cpupipe = popen("sensors | grep -i 'Package id' | awk -F'[+.°]' '{print $2}'", "r");
        if (cpupipe)
        {
            char cputempstring[10];
            fgets(cputempstring, sizeof(cputempstring), cpupipe);
            pclose(cpupipe);
            int cputemp = atoi(cputempstring);

            // Rolling average logic
            if (cputemp_count < cputemp_max_values) {
                // If not full, just add value
                cputemp_sum += cputemp;
                cputemp_values[cputemp_count] = cputemp;
                cputemp_count++;
            } else {
                // If full, replace oldest value
                cputemp_sum = cputemp_sum - cputemp_values[cputemp_index] + cputemp;
                cputemp_values[cputemp_index] = cputemp;
            }

            // Update circular index
            cputemp_index = (cputemp_index + 1) % cputemp_max_values;

            // Compute rolling average
            cpu_avg_temp = cputemp_sum / cputemp_count;

            if (cpu_avg_temp - 20 > maxtemp) maxtemp = cpu_avg_temp - 20; // Allow for 20 degrees higher temperature than the drives

            if (debug) printf("Current CPU Temperature: %d°C | Rolling Avg (last %d): %d°C\n", cputemp, cputemp_count, cpu_avg_temp);
        }

        if (debug) printf("Max Temperature: %d\n", maxtemp);

        if (graphite_sockfd > 0) {
            char message[256];

            snprintf(message, sizeof(message), "fancontrol.maxtemp %d %ld\n", maxtemp, time(NULL));
            send_to_graphite(graphite_sockfd, message);
        }

        // Calculate time since last poll
        clock_gettime(CLOCK_MONOTONIC, &curtime);
        timediff = ((1000000000LL * (curtime.tv_sec - lasttime.tv_sec) +
                    (curtime.tv_nsec - lasttime.tv_nsec))) / 1000000000.0;

        if (timediff == 0) {
            sleep(interval);
            continue;
        }

        // Update lasttime to the new time
        lasttime.tv_sec = curtime.tv_sec;
        lasttime.tv_nsec = curtime.tv_nsec;

        // Calculate PID values
        error = maxtemp - setpoint;

        // Compute the new PWM using the function
        int newPWM = calculate_new_pwm(error, timediff, integral, prev_error, graphite_sockfd);

        if (debug)
        {
            printf("maxtemp = %d, error = %f, p = %f, i = %f, d = %f, pwm = %d\n",
                   maxtemp, error, error * kp, integral * ki, derivative * kd, newPWM);
        }

        pwm = newPWM;

        // Write new PWM
        ecwrite(0x6b, pwm);
        ecwrite(0x73, pwm);

        // Send PWM value to Graphite if configured
        if (graphite_sockfd > 0) {
            char message[256];

            // Send PWM value
            snprintf(message, sizeof(message), "fancontrol.pwm %d %ld\n", pwm, time(NULL));
            send_to_graphite(graphite_sockfd, message);

            // Send CPU average temperature
            snprintf(message, sizeof(message), "fancontrol.cpu_avg_temp %d %ld\n", cpu_avg_temp, time(NULL));
            send_to_graphite(graphite_sockfd, message);
        }

        // Sleep at end of loop
        sleep(interval);
    }

    free(drives);
    iopl(0);
    free(cputemp_values);
    return 0;
}
