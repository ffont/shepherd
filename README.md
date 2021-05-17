# Shepherd

Shepherd is (will be) an open source MIDI looper.


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
sudo journalctl -fu sushi
```


### License

Shepherd is released under the **GPLv3** open source software license (see [LICENSE](https://github.com/ffont/shepherd/blob/master/LICENSE) file) with the code being available at  [https://github.com/ffont/shepherd](https://github.com/ffont/shepherd). Source uses the following open source software libraries: 

* [juce](https://juce.com), available under GPLv3 license ([@90e8da0](https://github.com/juce-framework/JUCE/tree/90e8da0cfb54ac593cdbed74c3d0c9b09bad3a9f), v6.0.8-dev)
