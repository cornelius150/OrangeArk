# Build from source code
- [Debian (+Ubuntu/Linux Mint)](#building-orangeark-on-debian-ubuntulinux-mint)
- [Arch Linux (+Manjaro Linux)](#building-orangeark-on-arch-linux-manjaro-linux)
- [Gentoo](#building-orangeark-on-gentoo)
- [Fedora](#building-orangeark-on-fedora)
- [Opensuse](#building-orangeark-on-opensuse)
- [MacOs](#building-orangeark-on-macos)
- [Windows](#building-orangeark-on-windows)


## Build/Debug with Visual Studio Code on Linux
https://code.visualstudio.com/docs/setup/linux
required installation of Extension "C/C++"
```sh
cd orangeark
code .
```
Build with: Ctrl+Shift+B
Debug with: F5

## Build/Debug with Visual Studio Code using a container
It is possible to use a container as a full-featured development environment from VS Code.
This works on any operating system that supports Docker.

1. Install the [system requirements](https://code.visualstudio.com/docs/remote/containers#_system-requirements).
2. Open the project in VS Code.
3. (optional) Edit `.devcontainer/devcontainer.json` to set your `DISPLAY` environment variable and/or edit other settings.
   It is also possible to run the container on a remote Docker host, see the comment at the end.
4. Run the *Remote-Containers: Open Folder in Container...* command.
5. See previous section for Build and Debug instructions.

## To build using the bundled spdlog and fmt libraries
```sh
./build.sh bundledspdfmt
```

## To create an AppImage bundle
```sh
./build.sh appimage
```

## To generate a backtrace for a crash bug report
```sh
./build.sh debug
gdb ./build/orangeark
(gdb) r
```
...after reproducing the crash
```sh
(gdb) bt
```

## Building OrangeArk on Debian (+Ubuntu/Linux Mint)
Install dependencies:
```sh
sudo apt install build-essential cmake ninja-build libgtkmm-3.0-dev libgtksourceview-4-dev libxml++2.6-dev libsqlite3-dev gettext libgspell-1-dev libcurl4-openssl-dev libuchardet-dev libfribidi-dev libvte-2.91-dev libfmt-dev libspdlog-dev file libxml2-utils
sudo apt install texlive-latex-base dvipng # optional for LatexBoxes support
```
Note: On Debian10 / Ubuntu 18.04 libfmt-dev and libspdlog-dev are not used since too old; bundled source code is built instead. Furthermore libgtksourceview-3.0-dev is to be used instead of libgtksourceview-4-dev (not available)
Get orangeark source, compile and run:
```sh
git clone https://github.com/giuspen/orangeark.git
cd orangeark
git submodule update --init
./build.sh
./build/orangeark
```

To create a debian package
```sh
./build.sh deb
```

Install documentation:
```sh
sudo apt install devhelp libgtkmm-3.0-doc libgtksourceview-4-doc libglibmm-2.4-doc libpangomm-1.4-doc libxml++2.6-doc libgspell-1-doc libvte-2.91-doc
```
devhelp
```sh
xdg-open /usr/share/doc/libgtkmm-3.0-doc/reference/html/index.html
xdg-open /usr/share/gtk-doc/html/gtksourceview-4.0/index.html
xdg-open /usr/share/doc/libglibmm-2.4-doc/reference/html/index.html
xdg-open /usr/share/doc/libpangomm-1.4-doc/reference/html/index.html
xdg-open /usr/share/doc/libxml++2.6-doc/reference/html/index.html
xdg-open /usr/share/doc/libgspell-1-dev/html/index.html
```

## Building OrangeArk on Arch Linux (+Manjaro Linux)
Install dependencies:
```sh
sudo pacman -S gtkmm3 gtksourceview4 libxml++2.6 vte3 gspell uchardet fmt spdlog
```

Get orangeark source, compile and run:
```sh
git clone https://github.com/giuspen/orangeark.git
cd orangeark
git submodule update --init
./build.sh
./build/orangeark
```

## Building OrangeArk on Gentoo
Build and Install orangeark:
```sh
sudo emerge orangeark
```

## Building OrangeArk on Fedora
Install dependencies:
```sh
sudo dnf install cmake ninja-build gcc-c++ gtkmm30-devel gtksourceview4-devel gspell-devel libxml++-devel libcurl-devel uchardet-devel fmt-devel spdlog-devel vte291-devel sqlite-devel
sudo dnf install texlive-scheme-basic texlive-dvipng # optional for LatexBoxes support
```

Get orangeark source, compile and run:
```sh
git clone https://github.com/giuspen/orangeark.git
cd orangeark
git submodule update --init
./build.sh
./build/orangeark
```

To create an rpm package
```sh
sudo dnf install rpm-build
./build.sh rpm
```

(OPTIONAL) Download Documentation
```sh
sudo dnf install gtkmm30-doc gtksourceview4-doc glibmm24-doc glibmm24-doc libxml++-doc
```

(OPTIONAL) Open Documentation
```sh
xdg-open /usr/share/doc/gtkmm-3.0/reference/html/index.html
xdg-open /usr/share/gtk-doc/html/gtksourceview-4.0/index.html
xdg-open /usr/share/doc/glibmm-2.4/reference/html/index.html
xdg-open /usr/share/doc/pangomm-1.4/reference/html/index.html
xdg-open /usr/share/doc/libxml++2.6/reference/html/index.html
```

## Building OrangeArk on OpenSuse
Install dependencies:
```sh
sudo zypper install cmake ninja gcc-c++ gtkmm3-devel gtksourceview4-devel gspell-devel libxml++26-devel sqlite3-devel libcurl-devel libuchardet-devel fmt-devel spdlog-devel vte-devel
```

Get orangeark source, compile and run:
```sh
git clone https://github.com/giuspen/orangeark.git
cd orangeark
git submodule update --init
./build.sh
./build/orangeark
```

To create an rpm package
```sh
sudo zypper install rpm-build
./build.sh rpm
```

## Building OrangeArk on MacOS
NOTE: OrangeArk is available as an [Installer](https://gitlab.com/dehesselle/orangeark_macos/-/releases) or in [Homebrew](https://formulae.brew.sh/formula/orangeark) or [Mac Ports](https://ports.macports.org/port/orangeark)
[Homebrew formula code](https://github.com/Homebrew/homebrew-core/blob/master/Formula/c/orangeark.rb)

In order build it yourself in [Homebrew](https://brew.sh/):

Install dependencies:
```sh
brew install cmake ninja pkg-config python adwaita-icon-theme fmt gspell gtkmm3 gtksourceview4 libxml++ spdlog uchardet fribidi curl vte3
brew link icu4c --force
brew install --cask basictex
sudo tlmgr update --self
sudo tlmgr install dvipng
```

Get orangeark source, compile and run:
```sh
git clone https://github.com/giuspen/orangeark.git
cd orangeark
git submodule update --init
export PKG_CONFIG_PATH="/usr/local/opt/icu4c/lib/pkgconfig"
./build.sh
./build/orangeark
```

## Building OrangeArk on Windows
Install MSYS2: https://www.msys2.org/ (we cover here the packages for 64 bit installation)

Launch 'MSYS2 UCRT64' terminal

Run the following command multiple times there until there are no more updates:
```sh
pacman -Syuu
```

Install required packages to build orangeark:
```sh
# toolchain, cmake, ninja
pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
# gtkmm3, gtksourceview4, libxml++2.6, sqlite3
pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-gtkmm3 mingw-w64-ucrt-x86_64-gtksourceview4 mingw-w64-ucrt-x86_64-libxml++2.6 mingw-w64-ucrt-x86_64-sqlite3
# gspell, curl, uchardet, fribidi, fmt, spdlog, webp-pixbuf-loader
pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-gspell mingw-w64-ucrt-x86_64-curl mingw-w64-ucrt-x86_64-uchardet mingw-w64-ucrt-x86_64-fribidi mingw-w64-ucrt-x86_64-fmt mingw-w64-ucrt-x86_64-spdlog mingw-w64-ucrt-x86_64-webp-pixbuf-loader
# latex, dvipng, gettext, git, nano
pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-texlive-core mingw-w64-ucrt-x86_64-gettext git nano
```

use native windows theme
```sh
mkdir /etc/gtk-3.0
nano /etc/gtk-3.0/settings.ini
```
```ini
[Settings]
gtk-theme-name=win32
```
console settings
```sh
nano ~/.bashrc
```
```sh
ORANGEARK_CONFIG_FOLDER="C:/Users/${USER}/AppData/Local/orangeark"
[ -d ${ORANGEARK_CONFIG_FOLDER} ] || mkdir -p ${ORANGEARK_CONFIG_FOLDER}
alias l="ls -lah --color"
alias g=git
bind '"\e[A":history-search-backward'
bind '"\e[B":history-search-forward'
```

Get orangeark source, compile and run:
```sh
git clone https://github.com/giuspen/orangeark.git
cd orangeark
git submodule update --init
./build.sh
./build/orangeark.exe
```

Troubleshooting:
- Cannot build: make sure to start 64-bit terminal
- Cannot build: remove `orangeark/build` folder and start `build.sh` script again
- Cannot start orangeark: you either have to run orangeark from the msys2 ucrt64 terminal or copy and replace orangeark in `orangeark_0.99.X_win64_portable` folder (downloaded from the site) by the new one, so dependencies are fulfilled
