# Fan control for TerraMaster on Linux 

Tested with F4-424 Pro. This a direct port of the Xpenology fancontrol script by Eudean to work on OMV/Debian.

Original author: https://xpenology.com/forum/topic/14007-terramaster-f4-220-fan-control/?ct=1559481439

This fork implements changes for it to work with NAS devices containing the IT8613E chipset, instead of the original IT8772E.
These changes are based on this post: https://xpenology.com/forum/topic/14007-terramaster-f4-220-fan-control/?do=findComment&comment=264172

## Installation:
1. Clone the repo
``git clone https://github.com/Nikotine1/terramaster-fancontrol-IT8613E``

2. Build with GCC. I'm using the docker image for ease of use.

   - Pull the image:

``docker pull gcc``

   - Compile fancontrol.cpp (you must be in the same directory)

``docker run --rm -v "$PWD":/usr/src/myapp -w /usr/src/myapp gcc gcc -o fancontrol fancontrol.cpp``


3. Create a directory containing all the drives. I created a directory in ``/opt/disks/``  and just empty files inside (as root):
```
mkdir /opt/disks
cd /opt/disks
touch sda sdb sdc
```

This far from perfect and if you're a Linux wizard you probably can do something better with regex, e.g.  ``/dev/sd*[a-z]``

4. Run the compiled program (command descriptions in the author's thread).

``sudo ./fancontrol 1 37 ``

This will run it in debug mode (1) with temperature setpoint= 37c. Make sure run with fancontrol with sudo.

5. Alternatively you can use the included systemd service. Copy it to `/etc/systemd/system`. Also, copy the binary to `/usr/local/bin/fancontrol`.

```
sudo systemctl start fancontrol.service
sudo systemctl enable fancontrol.service
```
