# pintos

# Project 4: File system
Tests to pass:
git pull && make && pintos -v -k -T 60 --qemu  --filesys-size=2 -p build/tests/userprog/args-none -a args-none -- -q  -f run args-none
git pull && make && pintos -v -k -T 60 --qemu  --filesys-size=2 -p build/tests/userprog/create-normal -a create-normal -- -q  -f run create-normal
