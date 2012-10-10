rsync -av 192.168.0.2::serenity /serenity && LFLAGS=-B/usr/lib/arm-linux-gnueabi CC=clang++ make && debug/test
