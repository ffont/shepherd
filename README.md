# Shepherd

Shepherd is (will be) an open source MIDI looper which runs on a Raspberry Pi and uses Ableton's Push2 as a user interface. Shepherd is formed by a JUCE C++ application which handles all the MIDI processing (the backend app), and a Python script which handles the communication with Push and UI. 

**NOTE**: All instructions below assume you have ssh connection with the Rpasberry Pi. Here are the instructions for [enabling ssh](https://www.raspberrypi.org/documentation/remote-access/ssh/) on the Pi. Here are instructions for [setting up wifi networks](https://www.raspberrypi.org/documentation/configuration/wireless/wireless-cli.md). Also, here are instructions for [changing the hostname](https://thepihut.com/blogs/raspberry-pi-tutorials/19668676-renaming-your-raspberry-pi-the-hostname) of the Pi so for example you can access it like `ssh pi@shepherd`.


## Shepherd (backend)

### Build instructions

Clone respository (with submodules):

```
git clone https://github.com/ffont/shepherd.git && cd shepherd && git submodule update --init
```

On macOS, open the XCode project file in `Builds/MacOSX` folder and compile as a normal JUCE project.

On a Raspberry Pi, you'll need to [install JUCE linux dependencies]( https://github.com/juce-framework/JUCE/blob/master/docs/Linux%20Dependencies.md
), and also `xvfb`, which is used to run Shepherd headlessly without errors:

```
sudo apt update
sudo apt install libasound2-dev libjack-jackd2-dev \
    libcurl4-openssl-dev  \
    libfreetype6-dev \
    libx11-dev libxcomposite-dev libxcursor-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev \
    libwebkit2gtk-4.0-dev \
    libglu1-mesa-dev mesa-common-dev
sudo apt-get install xvfb
```

Then, you can compile using `make`:

```
cd /home/pi/shepherd/Shepherd/Builds/LinuxMakefile
make CONFIG=Release -j4
```


### Running on Raspberry Pi

To run Shepherd on the Rapsberry Pi you need yo use `xvfb` to avoid window errors. This needs to be revised as this dependency can probably be removed quite easily. Use the following command to use Shepherd:

```
xvfb-run -a /home/pi/shepherd/Shepherd/Builds/LinuxMakefile/build/Shepherd
```

Alternatively, install following `systemd` service at `/lib/systemd/system/shepherd.service` (change paths is accordingly):

```
[Unit]
Description=shepherd
After=network-online.target

[Service]
Type=simple
WorkingDirectory=/home/pi/shepherd/
ExecStart=xvfb-run -a /home/pi/shepherd/Shepherd/Builds/LinuxMakefile/build/Shepherd
User=pi

[Install]
WantedBy=multi-user.target
```

Then you can enable the service...

```
sudo systemctl enable shepherd
```

...and start/stop/restart it with:

```
sudo systemctl start shepherd
sudo systemctl stop shepherd
sudo systemctl restart shepherd
```

Using that service configured, Shepherd will automatically be started after Raspberry Pi boots.

When running as a servie, check stdout with:

```
sudo journalctl -fu shepherd
```

## Shepherd Controller (frontend)

The Shepherd Controller app is a Python 3 script which interacts with Shepherd and a Push2 devices to provide the the UI.

To run Shepherd Controller you'll need to install the Python requirements and simply run the app:

```
pip install -r requirements.txt
python app.py
```

When not running in the Rapsberry Pi, the controller app initializes a Push2 simulator that can be used for development without the Push2 device being connected. You can open the simulator by poiting your browser at `localhost:6128` while running the controller app.

The Shepherd Controller app is based on [push2-python](https://github.com/ffont/push2-python). `push2-python` requires [pyusb](https://github.com/pyusb/pyusb) which is based in [libusb](https://libusb.info/). You'll most probably need to manually install `libusb` for your operative system if `pip install -r requirements.txt` does not do it for you. Moreover, to draw on Push2's screen, Shepherd Controller uses [`pycairo`](https://github.com/pygobject/pycairo) Python package. You'll most probably also need to install [`cairo`](https://www.cairographics.org/) if `pip install -r requirements.txt` does not do it for you (see [this page](https://pycairo.readthedocs.io/en/latest/getting_started.html) for info on that).


### Running on Raspberry Pi

To run Shepherd Controller on the Rapsberry Pi you need to install Python requirements as described above. Then, a `systemd` service should be configured so that Shepherd Controller is run together with Shepherd. Create a file at `/lib/systemd/system/shepherd_cotroller.service` (change paths is accordingly):

These are instructions to get Shepherd Controller running on a Rapsberry Pi and load at startup. 

1. Install system dependencies
```
sudo apt-get update && sudo apt-get install -y libusb-1.0-0-dev libcairo2-dev python3 python3-pip git libasound2-dev libatlas-base-dev
```

2. Clone the app repository
```
git clone https://github.com/ffont/shepherd.git
```

3. Install Python dependencies
```
cd shepherd
pip3 install -r requirements.txt
```

4. Configure permissions for using libusb without sudo (untested with these specific commands, but should work)

Create a file in `/etc/udev/rules.d/50-push2.rules`...

    sudo nano /etc/udev/rules.d/50-push2.rules

...with these contents:

    add file contents: SUBSYSTEM=="usb", ATTR{idVendor}=="2982", ATTR{idProduct}=="1967", GROUP="audio"

Then run:

    sudo udevadm control --reload-rules
    sudo udevadm trigger


5. Configure Python script to run at startup:

Create file...

    sudo nano /lib/systemd/system/shepherd_controller.service

...with these contents:

```
[Unit]
Description=shepherd_controller
After=network-online.target

[Service]
WorkingDirectory=/home/pi/shepherd/Controller
ExecStart=/usr/bin/python3 app.py                                                
StandardOutput=syslog
User=pi
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
```

Then you can enable the service...

```
sudo systemctl enable shepherd_controller
```

...and start/stop/restart it with:

```
sudo systemctl start shepherd_controller
sudo systemctl stop shepherd_controller
sudo systemctl restart shepherd_controller
```

Using that service configured, Shepherd Controller will automatically be started after Raspberry Pi boots.

When running as a servie, check stdout with:

```
sudo journalctl -fu shepherd_controller
```


## License

Shepherd is released under the **GPLv3** open source software license (see [LICENSE](https://github.com/ffont/shepherd/blob/master/LICENSE) file) with the code being available at  [https://github.com/ffont/shepherd](https://github.com/ffont/shepherd). Source uses the following open source software libraries: 

* [juce](https://juce.com), available under GPLv3 license ([@46ea879](https://github.com/juce-framework/JUCE/tree/46ea879739533ca0cdc689b967edfc5390c46ef7), v6.1.2)
* [Simple-Web-Server](https://gitlab.com/eidheim/Simple-Web-Server), available under MIT license ([@bdb1057](https://gitlab.com/eidheim/Simple-Web-Server/-/tree/bdb105712bc4cebc993de89b62e382b92102b347))
