#include "process.h"
#include "file.h"
#include "flac.h"
#include "resample.h"
#include "asound.h"
#include "interface.h"
#include "layout.h"
#include "window.h"
#include "text.h"
#include "simd.h"

extern "C" {
enum mpg123_flags { MPG123_ADD_FLAGS=2, MPG123_FORCE_FLOAT  = 0x400 };
struct mpg123;
int mpg123_init();
mpg123* mpg123_new(const char* decoder, int* error);
int mpg123_param(mpg123* mh, enum mpg123_flags type, long value, double fvalue);
int mpg123_open_fd(mpg123* mh, int fd);
int mpg123_getformat(mpg123* mh, long* rate, int* channels, int* encoding);
int mpg123_read(mpg123* mh, float* buffer, long bufferSizeInBytes, long* decodedSizeInBytes);
long mpg123_tell(mpg123* mh);
long mpg123_length(mpg123* mh);
long mpg123_timeframe(mpg123* mh, double sec);
long mpg123_seek_frame(mpg123* mh, long frameoff, int whence);
int mpg123_close(mpg123* mh);
void mpg123_delete(mpg123* mh);
}

/// Virtual audio decoder interface
struct AudioMedia {
    uint rate=0,channels=0;
    /// Returns elapsed time in seconds
    virtual uint position()=0;
    /// Returns media duration in seconds
    virtual uint duration()=0;
    /// Seeks media to \a position (in seconds)
    virtual void seek(uint position)=0;
    /// Reads \a size frames into \a output
    virtual int read(float2* output, uint size)=0;
    virtual ~AudioMedia(){}
};

/// MP3 audio decoder (using mpg123)
struct MP3Media : AudioMedia {
    Handle file = 0;
    mpg123* mh = 0;
    MP3Media(){}
    MP3Media(Handle&& file):file(move(file)){
        static int unused once=mpg123_init();
        mh = mpg123_new(0,0);
        mpg123_param(mh, MPG123_ADD_FLAGS, MPG123_FORCE_FLOAT, 0.);
        if(mpg123_open_fd(mh,this->file.fd)) { log("mpg123_open_fd failed"); mh=0; return; }
        long rate; int channels,encoding;
        if(mpg123_getformat(mh,&rate,&channels,&encoding)) { log("mpg123_getformat failed"); mh=0; return; }
        this->rate=rate; this->channels=channels; assert(channels==2);
    }
    uint position() { return int(mpg123_tell(mh)/rate); }
    uint duration() { return int(mpg123_length(mh)/rate); }
    void seek(uint unused position) { mpg123_seek_frame(mh,mpg123_timeframe(mh,position),0); }
    int read(float2* output, uint size) {
        long done;
        mpg123_read(mh,(float*)output,size*sizeof(float2),&done);
        for(uint i: range(size)) output[i] *= (float2){32768,32768};
        return done>0?done/(channels*sizeof(float)):done;
    }
    ~MP3Media() { mpg123_close(mh); mpg123_delete(mh); }
};

/// FLAC audio decoder (using \a FLAC)
struct FLACMedia : AudioMedia {
    Map map;
    FLAC flac;
    FLACMedia(){}
    FLACMedia(File&& file):map(file),flac(map){ AudioMedia::rate=flac.rate; AudioMedia::channels=2; }
    uint position() { return flac.position/rate; }
    uint duration() { return flac.duration/rate; }
    void seek(uint position) {
        if(position>=duration()) return;
        if(position<this->position()) { flac.~FLAC(); flac=FLAC(map); }
        while(this->position()<position) { flac.decodeFrame(); flac.position+=flac.buffer.size; flac.readIndex=(flac.readIndex+flac.buffer.size)%flac.buffer.capacity; flac.buffer.size=0; }
    }
    int read(float2* out, uint size) { return flac.read(out,size); }
};

/// Music player with a two-column interface (albums/track), gapless playback and persistence of last track+position
struct Player {
// Pipeline: File -> AudioMedia -> [Resampler] -> AudioOutput
    static constexpr int channels=2;
    AudioMedia* media=0; MP3Media mp3; FLACMedia flac;
    Resampler resampler;
    AudioOutput audio __({this,&Player::read});
    bool read(ptr& swPointer, int16* output, uint size) {
        float2 buffer[size];
        uint inputSize = resampler?resampler.need(size):size;
        {int size=inputSize; for(float2* input=buffer;;) {
            if(!media) return false;
            int read=media->read(input,size);
            if(read==size) break;
            assert(read<size);
            if(read>0) input+=read, size-=read;
            next();
        }}
        if(resampler) resampler.filter<false>((float*)buffer,inputSize,(float*)buffer,size);
        assert(size%4==0);
        for(uint i: range(size/4)) ((half8*)output)[i] = packs(cvtps(load(buffer+i*4+0)), cvtps(load(buffer+i*4+2))); //8 samples = 4 frames
        swPointer += size;
        update(media->position(),media->duration());
        return true;
    }

// Interface
    ICON(play) ICON(pause) ToggleButton playButton __(share(playIcon()), share(pauseIcon()));
    ICON(next) TriggerButton nextButton __(share(nextIcon()));
    Text elapsed __(string("00:00"_));
    Slider slider;
    Text remaining __(string("00:00"_));
    HBox toolbar;//__(&playButton, &nextButton, &elapsed, &slider, &remaining);
    Scroll< List<Text> > albums;
    Scroll< List<Text> > titles;
    HBox main;// __( &albums.area(), &titles.area() );
    VBox layout;// __( &toolbar, &main );
    Window window __(&layout, int2(-512,-512), "Player"_, pauseIcon());

// Content
    array<string> folders;
    array<string> files;

    Player() {
        albums.always=titles.always=true;
        elapsed.minSize.x=remaining.minSize.x=64;
        toolbar<<&playButton<<&nextButton<<&elapsed<<&slider<<&remaining;
        main<<&albums.area()<<&titles.area();
        layout<<&toolbar<<&main;

        albums.expanding=true; titles.main=Linear::Center;
        window.localShortcut(Escape).connect(&exit);
        window.localShortcut(Key(' ')).connect(this, &Player::togglePlay);
        window.globalShortcut(Play).connect(this, &Player::togglePlay);
        playButton.toggled.connect(this, &Player::setPlaying);
        nextButton.triggered.connect(this, &Player::next);
        slider.valueChanged.connect(this, &Player::seek);
        albums.activeChanged.connect(this, &Player::playAlbum);
        titles.activeChanged.connect(this, &Player::playTitle);

        folders = listFiles("Music"_,Sort|Folders);
        assert(folders);
        for(string& folder : folders) albums << string(section(folder,'/',-2,-1));

        int time=0;
        if(!files && existsFile("Music/.last"_)) {
            string mark = readFile("Music/.last"_);
            ref<byte> last = section(mark,0);
            time = toInteger(section(mark,0,1,2));
            string folder = string(section(last,'/',0,2));
            if(existsFolder(folder)) {
                albums.index = folders.indexOf(folder);
                array<string> files = listFiles(folder,Recursive|Sort|Files);
                uint i=0; for(;i<files.size();i++) if(files[i]==last) break;
                for(;i<files.size();i++) queueFile(move(files[i]));
            }
        }
        window.setSize(int2(-512,-512));
        if(files) next();
        if(time) seek(time);
        window.show();
        mainThread().priority=-19;
    }
    void queueFile(string&& path) {
        string title = string(section(section(path,'/',-2,-1),'.',0,-2));
        uint i=title.indexOf('-'); i++; //skip album name
        while(i<title.size() && title[i]>='0'&&title[i]<='9') i++; //skip track number
        while(i<title.size() && (title[i]==' '||title[i]=='.'||title[i]=='-'||title[i]=='_')) i++; //skip whitespace
        titles << Text(replace(title.slice(i),"_"_," "_), 16);
        files << move(path);
    }
    void play(const string& path) {
        assert(existsFolder(path));
        array<string> files = listFiles(path,Recursive|Sort|Files);
        for(string& file: files) queueFile(move(file));
        window.setSize(int2(-512,-512));
        next();
    }
    void playAlbum(uint index) {
        stop(); files.clear(); titles.clear();
        window.setTitle(albums[index].text);
        play(folders[index]);
    }
    void playTitle(uint index) {
        window.setTitle(titles[index].text);
        ref<byte> path = files[index];
        if(media) media->~AudioMedia(), media=0;
        if(endsWith(path,".mp3"_)||endsWith(path,".MP3"_)) media=new (&mp3) MP3Media(File(path));
        else if(endsWith(path,".flac"_)) media=new (&flac) FLACMedia(File(path));
        else warn("Unsupported format",path);
        assert(audio.channels==media->channels);
        if(audio.rate!=media->rate) new (&resampler) Resampler(audio.channels, media->rate, audio.rate, audio.periodSize);
        setPlaying(true);
        writeFile("/Music/.last"_,string(files[index]+"\0"_+dec(0)));
    }
    void next() {
        if(titles.index+1<titles.count()) playTitle(++titles.index);
        else if(albums.index+1<albums.count()) playAlbum(++albums.index);
        else {
            if(albums.index<albums.count()) window.setTitle(albums.active().text);
            stop(); return;
        }
        if(!playButton.enabled) setPlaying(true);
        //titles.ensureVisible(titles.active());
    }
    void togglePlay() { setPlaying(!playButton.enabled); }
    void setPlaying(bool play) {
        if(play) { audio.start(); window.setIcon(playIcon()); }
        else { audio.stop(); window.setIcon(pauseIcon()); }
        playButton.enabled=play; window.render();
    }
    void stop() {
        setPlaying(false);
        if(media) media->~AudioMedia(), media=0;
        elapsed.setText(string("00:00"_));
        slider.value = -1;
        remaining.setText(string("00:00"_));
        titles.index=-1;
    }
    void seek(int position) { media->seek(position); }
    void update(int position, int duration) {
        if(slider.value == position) return;
        writeFile("/Music/.last"_,string(files[titles.index]+"\0"_+dec(position)));
        if(!window.mapped) return;
        slider.value = position; slider.maximum=duration;
        elapsed.setText(dec(uint64(position/60),2)+":"_+dec(uint64(position%60),2));
        remaining.setText(dec(uint64((duration-position)/60),2)+":"_+dec(uint64((duration-position)%60),2));
        window.render();
    }
} application;
