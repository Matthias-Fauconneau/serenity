rsync -av 192.168.0.2::serenity /serenity && LFLAGS=-B/usr/lib/arm-linux-gnueabi CFLAGS=-ferror-limit=1 make && debug/test
