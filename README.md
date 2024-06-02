# Fan control for TerraMaster on Linux

Tested with F4-424 Pro. This a direct port of the Xpenology fancontrol script by Eudean to work on OMV/Debian.

Original author: https://xpenology.com/forum/topic/14007-terramaster-f4-220-fan-control/?ct=1559481439

This fork implements changes for it to work with NAS devices containing the IT8613E chipset, instead of the original IT8772E.
These changes are based on this post: https://xpenology.com/forum/topic/14007-terramaster-f4-220-fan-control/?do=findComment&comment=264172

You can verify which chip you have using ``sensors-detect``.

It also no longer uses the creation of files in ``/opt/disks``, named after the disks you want to monitor.
Instead, you give it a list of drive names as an argument.

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
