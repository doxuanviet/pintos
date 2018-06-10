# pintos

# Project 4: File system
Tests to pass:
git pull && make && pintos -v -k -T 60 --qemu  --filesys-size=2 -p build/tests/userprog/args-none -a args-none -- -q  -f run args-none
git pull && make && pintos -v -k -T 60 --qemu  --disk=tmp.dsk -p build/tests/filesys/extended/grow-sparse -a grow-sparse -p tests/filesys/extended/tar -a tar -- -q  -f run grow-sparse

git pull && make && pintos -v -k -T 60 --qemu  --filesys-size=2 -p build/tests/userprog/open-normal -a open-normal -p ../tests/userprog/sample.txt -a sample.txt -- -q  -f run open-normal

git pull && make && pintos -v -k -T 60 --qemu  --filesys-size=2 -p build/tests/userprog/open-empty -a open-empty -- -q  -f run open-empty

pintos-mkdisk tmp.dsk --filesys-size=2
pintos -v -k -T 60 --qemu  --disk=tmp.dsk -p tests/filesys/extended/syn-rw -a syn-rw -p tests/filesys/extended/tar -a tar -p tests/filesys/extended/child-syn-rw -a child-syn-rw -- -q  -f run syn-rw < /dev/null 2> tests/filesys/extended/syn-rw.errors > tests/filesys/extended/syn-rw.output

pintos -v -k -T 60  --qemu --disk=tmp.dsk -g fs.tar -a tests/filesys/extended/syn-rw.tar -- -q  run 'tar fs.tar /' < /dev/null 2> tests/filesys/extended/syn-rw-persistence.errors > tests/filesys/extended/syn-rw-persistence.output

rm -f tmp.dsk && git pull && make && pintos-mkdisk tmp.dsk --filesys-size=2 && \
pintos -v -k -T 150 --qemu  --disk=tmp.dsk -p build/tests/filesys/extended/dir-vine -a dir-vine -p build/tests/filesys/extended/tar -a tar -- -q  -f run dir-vine

rm -f tmp.dsk && git pull && make && pintos-mkdisk tmp.dsk --filesys-size=2 && \
pintos -v -k -T 60 --qemu  --disk=tmp.dsk -p build/tests/filesys/extended/dir-rm-tree -a dir-rm-tree -p build/tests/filesys/extended/tar -a tar -- -q  -f run dir-rm-tree \
&& pintos -v -k -T 60  --qemu --disk=tmp.dsk -g fs.tar -a build/tests/filesys/extended/dir-rm-tree.tar -- -q  run 'tar fs.tar /'

rm -f tmp.dsk && git pull && make && pintos-mkdisk tmp.dsk --filesys-size=2 && \
pintos -v -k -T 60 --qemu  --disk=tmp.dsk -p build/tests/filesys/extended/dir-rm-cwd -a dir-rm-cwd -p build/tests/filesys/extended/tar -a tar -- -q  -f run dir-rm-cwd