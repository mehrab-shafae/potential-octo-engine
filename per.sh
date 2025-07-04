#!/bin/bash

if [ "$EUID" -ne 0 ]; then
    echo "Please run as root"
    exit 1
fi

sudo chown -R root:wheel $(pwd)
sudo chmod -R 770 $(pwd)/**/
# sudo chmod -R 2664 $(pwd)/**/*.{c*,h*,md,sh}
echo "Done"