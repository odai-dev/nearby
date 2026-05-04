# Google Nearby Sharing & Connections for Linux (Unofficial)

<img width="982" height="760" alt="image" src="https://github.com/user-attachments/assets/9533ea09-81d9-4162-b90f-e0bd8b714d1d" />



## Demo


https://github.com/user-attachments/assets/048afa1e-40a4-4351-a859-c81b642fc6e3

## What
This repo consists of the entirety of google's open source nearby library. Currently, it is seperated into 3 sections. 
- Sharing
- Connections
- ~Presence~ ( Build fails for some reason. Haven't had time to investigate. Maybe something minor. Check the validate.yaml for build steps and see why its faiing )

    
Linux specific implementation and compatibility shims are provided for building **Sharing** and **Connections**. 
Nearby presence may or may not build. I haven't tested it.

## Why
This repo could've been a PR on the official repo. All I've done is implement the platform abstraction layer google has provided for a linux specific environment.
It's not like I haven't made a PR towards the official repo. I got tired of begging the official nearby maintainers for a PR review. So here we are. 


Moreover, all I've wanted was seamless file sharing between my android devices and my linux workstation + laptop. With this repo and the example application provided here,
it accomplishes that goal perfectly. This repo wasn't created out of any altruistic goals or out of the goodness of my heart. I had a problem. I solved it. Simple as that. 


## How
### How to use
Proper documentation is coming I swear. University's getting pretty hectic so docs is on the backburner. The wiki has some more information but it's nowhere near a proper documentation. It has some good brief
overviews and where generally everything is located if you're thinking about contributing. 

If you want any clarification on anything, feel free to open an issue. I'll get back to you ASAP.

As a consolation prize, I've indexed this project using [Deepwiki](https://deepwiki.com/kidfromjupiter/nearby). You might have strong feeling about AI use. But I feel like documenting very large codebases is a perfect usecase for such models. (They are called Large Language Models for a reason )

### How to install
This repo provides prebuilt binaries of the Quick Share application and also builds cleanly from source on Ubuntu-family systems such as Pop!_OS 24.04.
Fedora remains supported, and the release bundle now carries the `libsdbus-c++.so.2` runtime so it does not depend on a manual `/usr/local` install of `sdbus-c++`.


#### Prerequisites

- `bluez`
- `NetworkManager`
- `Avahi`
- Qt 6 runtime libraries
- `libqrencode`

**On Fedora**

```bash
sudo dnf install -y \
  bluez bluez-libs bluez-libs-devel \
  sdbus-cpp sdbus-cpp-devel \
  NetworkManager avahi avahi-tools \
  qt6-qtbase qt6-qtdeclarative qrencode-libs
```

**On Pop!_OS / Ubuntu 24.04**

```bash
sudo apt install -y \
  bluez network-manager avahi-daemon avahi-utils \
  qt6-base-dev qt6-declarative-dev qt6-wayland \
  qt6-tools-dev qt6-tools-dev-tools libqrencode-dev
```
---

**To install the Quick Share application,**

1. Go to [releases](https://github.com/kidfromjupiter/nearby/releases)
2. Download the latest `nearby-file-share-linux-*.tar.gz`
3. `mkdir -p nearby && tar -xf nearby-file-share-linux-*.tar.gz -C nearby`
4. `cd nearby`
5. `chmod +x install_nearby_file_share.sh`
6. `./install_nearby_file_share.sh`

The installer now checks for missing Qt / qrencode / Bluetooth / NetworkManager / Avahi prerequisites up front and prints Pop!_OS / Ubuntu-friendly remediation hints instead of installing a bundle that cannot start.

**To install the actual library and headers,**

If you want the Linux shared library and public headers for development, build them from source:

```bash
./sharing/linux/install_nearby_sharing_service.sh
```

That helper now installs the matching `libsdbus-c++.so.2` beside `libnearby_sharing_api_shared.so` and patches the library runpath so the QML tray app prefers the colocated runtime copy instead of an unrelated global one.

Source builds still need a compatible `sdbus-c++` development install discoverable via `pkg-config` (or installed under `/usr` or `/usr/local`) so Bazel can compile the Linux shared library.

### How to build
Check the [wiki](https://github.com/kidfromjupiter/nearby/wiki/Development-Environment-and-Building)
### How to contribute
Check the [wiki](https://github.com/kidfromjupiter/nearby/wiki/Development-Environment-and-Building)


## TODO
- Investigate why bluetooth connection requests pairing ( both l2cap socket and bluetooth profile should be unauthenticated )
- When transferring (android to linux) over bluetooth classic, android shows 100% transferred ( but still `Sending...` ) while linux lags behind. Some kind of bottleneck or android bug?
- Tests for basically everything.
- Cleanup of `implementation/linux`. Currently all the linux specific implementation files are in the same directory. Even though thats how the other platforms have their stuff structured, I personally hate the visual bloat.
- Documentation basically everything ( honestly this would be a massive project in itself )
- Resolving random crashes of quick share application
- Support for fast initiation
- When receiving, if file already exists, does not overwrite currently. Decide what to do then

## Apologies
I may have done things in *incredibly* stupid and overcomplicated ways. It doesn't certainly help that this was the way I decided to learn C++. Blessed be my naive soul. I also do not have much experience working with such 
enormous codebases. 

If you see any such stupidities, feel free to berate me in the most shameless of manners in an issue. I look forward to learning how to do it the proper way and to improve my atrocious code quality.

## Special thanks

https://github.com/proatgram and https://github.com/vibhavp

*they were the original authors of the linux platform support [PR](https://github.com/google/nearby/pull/2098) that I have based much of this codebase upon. I am standing on the shoulders
of giants*
