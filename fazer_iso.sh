#!/bin/sh
# Uso (depois do make): sh fazer_iso.sh
set -e
rm -rf iso && mkdir iso
cp ultima_aula.elf iso/SLUS_000.01
printf 'BOOT2 = cdrom0:\\SLUS_000.01;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n' > iso/SYSTEM.CNF
genisoimage -o "SLUS_000.01.A Ultima Aula.iso" -V ULTIMA_AULA -l -iso-level 1 iso
echo "Pronto: SLUS_000.01.A Ultima Aula.iso"
