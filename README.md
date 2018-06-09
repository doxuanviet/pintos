# pintos

# Project 4: File system
Tests to pass:
git pull && make && pintos -v -k -T 60 --qemu  --filesys-size=2 -p build/tests/userprog/args-none -a args-none -- -q  -f run args-none
git pull && make && pintos -v -k -T 60 --qemu  --disk=tmp.dsk -p build/tests/filesys/extended/grow-sparse -a grow-sparse -p tests/filesys/extended/tar -a tar -- -q  -f run grow-sparse