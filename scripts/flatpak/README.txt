
- This was started after Inkscape's https://github.com/flathub/org.inkscape.Inkscape

- OliveSet's flathub repo is at https://github.com/flathub/net.giuspen.oliveset

- This is not meant to stay at pace with the official net.giuspen.oliveset.json but will be upgraded only when changes other than url/sha256 are issued



- Build and run Flatpak locally:

sudo apt install flatpak flatpak-builder gnome-software-plugin-flatpak

flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo

flatpak install flathub org.gnome.Platform//45 org.gnome.Sdk//45

flatpak-builder --force-clean --arch=x86_64 build-dir net.giuspen.oliveset.json

# NOTE: access to the file system will not work unless you also install it once (see below)
flatpak-builder --run build-dir net.giuspen.oliveset.json oliveset



- Build redistributable flatpak and install it

flatpak-builder --force-clean --arch=x86_64 --repo=repo build-dir net.giuspen.oliveset.json

flatpak build-bundle --arch=x86_64 repo oliveset-0.99.55.x86_64.flatpak net.giuspen.oliveset

flatpak install oliveset-0.99.55.x86_64.flatpak

flatpak run net.giuspen.oliveset
