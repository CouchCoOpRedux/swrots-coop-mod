# SPDX-License-Identifier: GPL-3.0-or-later
import struct,sys,subprocess
d=open('/mnt/user-data/attachments/ce8bf908-d549-5ec7-9158-8c7dde56e228/ccbfcec0-default_original.xbe','rb').read()
n=struct.unpack_from('<I',d,0x11C)[0]; sh=struct.unpack_from('<I',d,0x120)[0]-0x10000
secs=[struct.unpack_from('<IIIII',d,sh+i*56) for i in range(n)]
def off(v):
    for f,va,vs,ro,rs in secs:
        if va<=v<va+rs: return ro+v-va
va=int(sys.argv[1],16); ln=int(sys.argv[2],16)
o=off(va); open('t.bin','wb').write(d[o:o+ln])
print(subprocess.run(['objdump','-D','-b','binary','-mi386','--adjust-vma=0x%x'%va,'t.bin'],capture_output=True,text=True).stdout.split('\n',7)[-1])
