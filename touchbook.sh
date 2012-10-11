rsync -av 192.168.0.2::serenity /serenity && LFLAGS=-B/usr/lib/arm-linux-gnueabi CC="g++ -mapcs" make && DISPLAY=:0 debug/test
