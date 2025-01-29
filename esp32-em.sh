
CMD=$1; shift
BIN=$1; shift

case $CMD in 
 install)
    # sudo apt-get update
    # sudo apt-get install libglib2.0-dev pkg-config
    # sudo apt-get install libslirp-dev

    # git clone https://github.com/espressif/qemu.git esp-qemu
    # cd esp-qemu
    ./configure --target-list=xtensa-softmmu \
        --enable-debug --enable-sanitizers \
        --disable-strip --disable-user \
        --disable-capstone --disable-vnc
    make -j8
    sudo make install
    ;;

 run)
    qemu-system-xtensa -nographic \
        -machine esp32 \
        -drive file=$BIN,if=mtd,format=raw \
        -global driver=timer.esp32.timg,property=wdt_disable,value=true \
        -nic user,model=open_eth,hostfwd=tcp::8080-:80 \
        -d unimp 
    reset
 ;;
esac

exit

Uggh!

qemu-system-xtensa: missing object type 'misc.esp32.rsa'

