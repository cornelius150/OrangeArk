
- This was largely copied from Inkscape's https://gitlab.com/inkscape/inkscape/-/blob/master/snap/snapcraft.yaml

- OliveSet's snap repo is at https://snapcraft.io/oliveset



- Build (https://snapcraft.io/docs/snapcraft-overview):

sudo snap install snapcraft --classic

sudo usermod -aG lxd giuspen
newgrp lxd

snapcraft



- Install

sudo snap install oliveset_1.2.0_amd64.snap --dangerous --devmode



- Upload

snapcraft login
snapcraft upload --release=stable oliveset_1.2.0_amd64.snap
snapcraft logout

NOTE: if it fails login with craft-store error: Credentials found for 'snapcraft' on 'dashboard.snapcraft.io'
      it's likely that the last time you didn't logout, so you are simply already logged in!
