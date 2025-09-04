#!/bin/bash

# make clean
# make moddwarf

sshpass -p 'mod' ssh root@192.168.51.1 hmi-update /usr/share/mod/controller/mod-dwarf-controller.bin
sshpass -p 'mod' ssh root@192.168.51.1 systemctl restart mod-ui

# make clean
