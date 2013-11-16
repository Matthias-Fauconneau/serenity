/// \file player.cc Music player
#include "thread.h"
#include "file.h"
#include "ffmpeg.h"
#include "resample.h"
#include "audio.h"
#include "interface.h"
#include "layout.h"
#include "window.h"
#include "text.h"
#include "time.h"

/// Music player with a two-column interface (albums/track), gapless playback and persistence of last track+position
struct Player {
// Gapless playback
    static constexpr uint channels = 2;
    AudioFile file;
    Resampler resampler;
    bool resamplerFlushed = false;
    Resampler nextResampler;
    AudioOutput audio{{this,&Player::read}, 48000, 32768};
    int32* lastPeriod = 0, lastPeriodSize = 0;
    uint read(int32* output, uint outputSize) {
        uint readSize = 0;
        for(int32* chunk=output;;) {
            if(!file) break;
            int need = outputSize-readSize;
            int read = 0;
            if(resampler.sourceRate*audio.rate != file.rate*resampler.targetRate && !resamplerFlushed) {
                if(file.rate != audio.rate) new (&nextResampler) Resampler(audio.channels, file.rate, audio.rate, audio.periodSize);
                if(resampler) { // Flushes previous resampler using start of next file
                    uint previousNeed = resampler.need(resampler.N/2*resampler.sourceRate/resampler.targetRate);
                    Resampler nextToPrevious(resampler.channels, file.rate, resampler.sourceRate, previousNeed);
                    uint sourceNeed = nextToPrevious.need(previousNeed);
                    {float source[sourceNeed*2];
                        uint sourceRead = file.read(source, sourceNeed); // Ignores the corner case of file smaller than a resampler half filter size
                        nextToPrevious.write(source, sourceRead);
                        if(nextResampler) nextResampler.write(source, sourceRead);} // Also writes into next resampler
                    uint read = nextToPrevious.available();
                    float nextToPreviousBuffer[read*2];
                    nextToPrevious.read(nextToPreviousBuffer, read); // Resamples file rate to previous resampler source rate
                    resampler.write(nextToPreviousBuffer, read); // And feeds to the previous resampler in order to flush it
                }
                resamplerFlushed = true;
            }
            if(resampler) {
                if(resampler.sourceRate*audio.rate == file.rate*resampler.targetRate) {
                    uint sourceNeed = resampler.need(need);
                    float source[sourceNeed*2];
                    uint sourceRead = file.read(source, sourceNeed);
                    resampler.write(source, sourceRead);
                }
                read = min(need, resampler.available());
                if(read) {
                    float target[read*2];
                    resampler.read(target, read);
                    for(uint i: range(read*2)) {
                        int64 s = target[i]*(1<<29); // 3dB headroom
                        if(s<-(1<<30) || s >= (1<<30)) error("Clip", s);
                        chunk[i] = s;
                    }
                }
            } else if(!nextResampler) read = file.read(chunk, need);
            assert(read<=need, read, need);
            chunk += read*channels; readSize += read;
            if(readSize == outputSize) { update(file.position/file.rate,file.duration/file.rate); break; } // Complete chunk
            else if(resampler.sourceRate*audio.rate == file.rate*resampler.targetRate) next(); // End of file
            else { resampler = move(nextResampler); resamplerFlushed=false; }  // Previous resampler can be replaced once properly flushed
        }
        if(!lastPeriod) for(uint i: range(outputSize)) { // Fades in
            float level = exp(12. * ((float) i / outputSize - 1) ); // Linear perceived sound level
            output[i*2] *= level;
            output[i*2+1] *= level;
        }
        lastPeriod = output; lastPeriodSize = outputSize;
        return readSize;
    }

// Interface
    ICON(play) ICON(pause) ToggleButton playButton{playIcon(), pauseIcon()};
    ICON(next) TriggerButton nextButton{nextIcon()};
    Text elapsed = "00:00"_;
    Slider slider;
    Text remaining = "00:00"_;
    HBox toolbar {{&playButton, &nextButton, &elapsed, &slider, &remaining}};
    Scroll< List<Text>> albums;
    Scroll< List<Text>> titles;
    HBox main {{ &albums.area(), &titles.area() }};
    VBox layout {{ &toolbar, &main }};
    Window window {&layout, int2(-600,-1120), "Player"_, pauseIcon()};

// Content
    array<String> folders;
    array<String> files;
    array<String> randomSequence;

    Player() {
        albums.always=titles.always=true;
        elapsed.minSize.x=remaining.minSize.x=64;

        albums.expanding=true; titles.expanding=true; titles.main=Linear::Center;
        window.localShortcut(Escape).connect([]{exit();});
        window.localShortcut(Key(' ')).connect(this, &Player::togglePlay);
        window.globalShortcut(Play).connect(this, &Player::togglePlay);
        //randomButton.toggled.connect(this, &Player::setRandom);
        playButton.toggled.connect(this, &Player::setPlaying);
        nextButton.triggered.connect(this, &Player::next);
        slider.valueChanged.connect(this, &Player::seek);
        albums.activeChanged.connect(this, &Player::playAlbum);
        titles.activeChanged.connect(this, &Player::playTitle);

        folders = Folder("Music"_).list(Folders|Sorted);
        assert(folders);
        for(String& folder : folders) albums << String(section(folder,'/',-2,-1));
        setRandom(true);

        if(existsFile("Music/.last"_)) {
            String mark = readFile("Music/.last"_);
            string last = section(mark,0);
            string folder = section(last,'/',0,1);
            string file = section(last,'/',1,-1);
            if(existsFolder(folder,"Music"_)) {
                /*albums.index = folders.indexOf(folder);
                array<String> files = Folder(folder,"Music"_).list(Recursive|Files|Sorted);
                uint i=0; for(;i<files.size;i++) if(files[i]==file) break;
                for(;i<files.size;i++) queueFile(folder, files[i]);
                if(files)*/
                queueFile(folder, file);   // FIXME: Assumes random play
                {
                    next();
                    seek(toInteger(section(mark,0,1,2)));
                }
            }
        } else {
            updatePlaylist();
            next();
        }
        window.show();
        mainThread.setPriority(-20);
    }
    ~Player() { writeFile("/Music/.last"_,String(files[titles.index]+"\0"_+dec(file.position/file.rate))); }
    void queueFile(const string& folder, const string& file, bool withAlbumName=true) {
        String title = String(section(section(file,'/',-2,-1),'.',0,-2));
        uint i=title.indexOf('-'); i++; //skip album name
        while(i<title.size && title[i]>='0'&&title[i]<='9') i++; //skip track number
        while(i<title.size && (title[i]==' '||title[i]=='.'||title[i]=='-'||title[i]=='_')) i++; //skip whitespace
        title = replace(title.slice(i),"_"_," "_);
        if(withAlbumName) title = folder + " - "_ + title;
        titles << Text(title, 16);
        files <<  folder+"/"_+file;
    }
    void playAlbum(const string& album) {
        assert(existsFolder(album,"Music"_),album);
        array<String> files = Folder(album,"Music"_).list(Recursive|Files|Sorted);
        for(const String& file: files) queueFile(album, file);
        window.setSize(int2(-512,-512));
        titles.index=-1; next();
    }
    void playAlbum(uint index) {
        files.clear(); titles.clear();
        window.setTitle(toUTF8(albums[index].text));
        playAlbum(folders[index]);
    }
    void playTitle(uint index) {
        window.setTitle(toUTF8(titles[index].text));
        if(!file.openPath(String("/Music/"_+files[index]))) return;
        assert(audio.channels==file.channels);
        setPlaying(true);
    }
    void next() {
        if(titles.index+1<titles.count()) playTitle(++titles.index);
        else if(albums.index+1<albums.count()) playAlbum(++albums.index);
        else if(albums.count()) playAlbum(albums.index=0);
        else { setPlaying(false); file.close(); return; }
        updatePlaylist();
    }
    void setRandom(bool random) {
        main.clear();
        randomSequence.clear();
        if(random) {
            main << &titles.area(); // Hide albums
            // Explicits random sequence to: resume the sequence from the last played file, ensure files are played once in the sequence.
            array<String> files = Folder("Music"_).list(Recursive|Files|Sorted); // Lists all files
            randomSequence.reserve(files.size);
            Random random; // Unseeded so that the random sequence only depends on collection
            while(files) randomSequence << files.take(random%files.size);
        } else main<<&albums.area()<<&titles.area(); // Show albums
    }
    void updatePlaylist() {
        if(!randomSequence) return;
        uint randomIndex = 0, listIndex = 0;
        if(titles.index < files.size) {
            listIndex=titles.index;
            for(uint i: range(randomSequence.size)) if(randomSequence[i]==files[titles.index]) { randomIndex=i; break; }
        }
        randomIndex += files.size-listIndex; // Assumes already queued tracks are from randomSequence
        while(titles.count() < listIndex + 16) { // Schedules at least 16 tracks drawing from random sequence as needed
            string path = randomSequence[randomIndex];
            string folder = section(path,'/',0,1), file = section(path,'/',1,-1);
            queueFile(folder, file, true);
            randomIndex++;
        }
        while(titles.count() > 64 && titles.index > 0) { titles.take(0); files.take(0); titles.index--; } // Limits total size
    }
    void togglePlay() { setPlaying(!playButton.enabled); }
    void setPlaying(bool play) {
        if(play) { audio.start(); window.setIcon(playIcon()); }
        else {
            // Fades out the last period (assuming the hardware is not playing it (false if swap occurs right after pause))
            for(uint i: range(lastPeriodSize)) {
                float level = exp2(-12. * i / lastPeriodSize); // Linear perceived sound level
                lastPeriod[i*2] *= level;
                lastPeriod[i*2+1] *= level;
            }
            lastPeriod=0, lastPeriodSize=0;
            audio.stop();
            window.setIcon(pauseIcon());
            file.seek(max(0, (int)file.position-lastPeriodSize));
        }
        playButton.enabled=play;
        window.render();
        writeFile("/Music/.last"_,String(files[titles.index]+"\0"_+dec(file.position/file.rate)));
    }
    void seek(int position) {
        if(file) { file.seek(position*file.rate); update(file.position/file.rate,file.duration/file.rate); resampler.clear(); audio.cancel(); }
    }
    void update(uint position, uint duration) {
        if(slider.value == (int)position) return;
        slider.value = position; slider.maximum=duration;
        elapsed.setText(String(dec(position/60,2)+":"_+dec(position%60,2)));
        remaining.setText(String(dec((duration-position)/60,2)+":"_+dec((duration-position)%60,2)));
        window.render();
    }
} application;
