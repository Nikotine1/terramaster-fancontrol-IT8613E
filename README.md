# Fan control for TerraMaster on Linux

Tested with F4-424 Pro. This a direct port of the Xpenology fancontrol script by Eudean to work on OMV/Debian.

Original author: https://xpenology.com/forum/topic/14007-terramaster-f4-220-fan-control/?ct=1559481439

This fork implements changes for it to work with NAS devices containing the IT8613E chipset, instead of the original IT8772E.
These changes are based on this post: https://xpenology.com/forum/topic/14007-terramaster-f4-220-fan-control/?do=findComment&comment=264172

You can verify which chip you have using ``sensors-detect``.

It also no longer uses the creation of files in ``/opt/disks``, named after the disks you want to monitor.
Instead, you give it a list of drive names as an argument.

I have also added reporting to a Graphite server.
Enable it by adding ``--graphite_server=<ip address>:<port>``.

## Installation:
1. Clone the repo
``git clone https://github.com/Nikotine1/terramaster-fancontrol-IT8613E``

2. Build with GCC.

   Using Docker:

   - Pull the image:

``docker pull gcc``

   - Compile fancontrol.cpp.

``docker run --rm -v "$PWD":/usr/src/myapp -w /usr/src/myapp gcc gcc -o fancontrol fancontrol.cpp``

   Using Kubernetes:
   See https://www.niek.be/2024/06/02/compiling-c-with-gcc-in-kubernetes-container

3. Run the compiled program.

``sudo ./fancontrol --drive_list="sda,sdb,sdc,sdd" --debug=1 --setpoint=37``

This will run it in debug mode (1), monitoring drives /dev/sda to d, with temperature setpoint 37°C. Make sure to run with sudo.

4. Alternatively you can use the included systemd service.
Change the location of the fancontrol application.
Make sure you also add the list of drives there.
Copy it to `/etc/systemd/system`.

```
sudo systemctl start fancontrol.service
sudo systemctl enable fancontrol.service
```

## Parameters:
```
 fancontrol --drive_list=<drive_list> [--debug=<value>] [--setpoint=<value>] [--pwminit=<value>] [--interval=<value>] [--overheat=<value>] [--pwmmin=<value>] [--kp=<value>] [--ki=<value>] [--imax=<value>] [--kd=<value>] [--graphite_server=<ip:port>]

drive_list        A comma-separated list of drive names between quotes e.g. 'sda,sdc' (required)
debug             Enable (1) or disable (0) debug logs (default: 0)
setpoint          Target maximum hard drive operating temperature in
                  degrees Celsius (default: 37)
pwminit           Initial PWM value to write (default: 128)
interval          How often we poll for temperatures in seconds (default: 10)
overheat          Overheat temperature threshold in degrees Celsius above
                  which we drive the fans at maximum speed (default: 50)
pwmmin            Never drive the fans below this PWM value (default: 80)
kp                Proportional coefficient (default: 1.0)
ki                Integral coefficient (default: 0.0)
imax              Maximum integral value (default: 10.0)
kd                Derivative coefficient (default: 0.0)
graphite_server   Graphite server IP address and port in the format <ip:port> (optional)
```
