#include "process.h"
#include "file.h"
#include "flac.h"
#include "resample.h"
#include "asound.h"
#include "interface.h"
#include "layout.h"
#include "window.h"
#include "text.h"

extern "C" {
enum mpg123_flags { MPG123_ADD_FLAGS=2, MPG123_FORCE_FLOAT  = 0x400 };
struct mpg123;
int mpg123_init();
mpg123* mpg123_new(const char* decoder, int* error);
int mpg123_param(mpg123 *mh, enum mpg123_flags type, long value, double fvalue);
int mpg123_open_fd(mpg123 *mh, int fd);
int mpg123_getformat(mpg123 *mh, long *rate, int *channels, int *encoding);
int mpg123_read(mpg123 *mh, float* buffer, long bufferSizeInBytes, long* decodedSizeInBytes);
long mpg123_tell(mpg123 *mh);
long mpg123_length(mpg123 *mh);
long mpg123_timeframe(mpg123 *mh, double sec);
long mpg123_seek_frame(mpg123 *mh, long frameoff, int whence);
int mpg123_close(mpg123 *mh);
void mpg123_delete(mpg123 *mh);
}

/// Virtual audio decoder interface
struct AudioMedia {
    uint rate=0,channels=0;
    /// Returns elapsed time in seconds
    virtual int position()=0;
    /// Returns media duration in seconds
    virtual int duration()=0;
    /// Seeks media to \a position (in seconds)
    virtual void seek(int position)=0;
    /// Reads \a size frames into \a output
    virtual bool read(float* output, uint size)=0;
    virtual ~AudioMedia(){}
};

/// MP3 audio decoder (using mpg123)
struct MP3Media : AudioMedia {
    mpg123* file = 0;
    MP3Media(){}
    MP3Media(int fd) {
        static int unused once=mpg123_init();
        file = mpg123_new(0,0);
        mpg123_param(file, MPG123_ADD_FLAGS, MPG123_FORCE_FLOAT, 0.);
        if(mpg123_open_fd(file,fd)) { log("mpg123_open_fd failed"); file=0; return; }
        long rate; int channels,encoding;
        if(mpg123_getformat(file,&rate,&channels,&encoding)) { log("mpg123_getformat failed"); file=0; return; }
        this->rate=rate; this->channels=channels;
    }
    int position() { return int(mpg123_tell(file)/rate); }
    int duration() { return int(mpg123_length(file)/rate); }
    void seek(int position) { mpg123_seek_frame(file,mpg123_timeframe(file,position),0); }
    bool read(float* output, uint size) { long done; return !mpg123_read(file,output,size*channels*sizeof(float),&done) && done==size*channels*sizeof(float); }
    ~MP3Media() { mpg123_close(file); mpg123_delete(file); file=0; }
};

/// FLAC audio decoder
struct FLACMedia : AudioMedia, FLAC {
    Map file;
    uint blockPosition=0; //position of last decoded FLAC frame in samples
    float* block=0;
    FLACMedia(){}
    FLACMedia(int fd) { file=mapFile(fd); start(file); AudioMedia::rate=FLAC::rate; AudioMedia::channels=FLAC::channels; }
    int position() { return blockPosition/FLAC::rate; }
    int duration() { return FLAC::duration/FLAC::rate; }
    void seek(int position) { if(position<this->position()) start(file); while(position>this->position()) readBlock(); }
    bool read(float* out, uint size) {
        while(size > blockSize) {
            if(blockPosition+blockSize >= FLAC::duration) return false;
            for(float* end=out+2*blockSize; out<end; block++, out++) out[0]=block[0];
            size -= blockSize;
            blockPosition+=blockSize; readBlock(); block=buffer;
        }
        for(float* end=out+2*size; out<end; block++, out++) out[0]=block[0];
        blockSize -= size;
        return true;
    }
};

struct Player : Application {
/// Pipeline: File -> AudioMedia -> [Resampler] -> AudioOutput
    static const int channels=2;
    File file=File(0);
    AudioMedia* media=0; MP3Media mp3; FLACMedia flac;
    Resampler resampler;
    AudioOutput audio __({this,&Player::read});
    bool read(int16* output, uint size) {
        float buffer[size*channels];
        uint inputSize = size*media->rate/audio.rate; assert(size>=inputSize);
        if(!media || !media->read(buffer,inputSize)) {
            next();
            if(!media || !media->read(buffer,inputSize)) return false;
        }
        if(resampler) resampler.filter(buffer,inputSize,buffer,size);
        for(uint i=0;i<size*channels;i++) output[i] = clip(-32768,int(buffer[i]*32768),32767);
        update(media->position(),media->duration());
        return true;
    }

/// Interface
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

/// Content
    array<string> folders;
    array<string> files;

    Player() {
        albums.always=titles.always=true;
        elapsed.minSize.x=remaining.minSize.x=64;
        toolbar<<&playButton<<&nextButton<<&elapsed<<&slider<<&remaining;
        main<<&albums.area()<<&titles.area();
        layout<<&toolbar<<&main;

        albums.expanding=true; titles.main=Linear::Center;
        window.localShortcut(Escape).connect(this, &Player::quit);
        window.localShortcut(Key(' ')).connect(this, &Player::togglePlay);
        window.globalShortcut(Play).connect(this, &Player::togglePlay);
        playButton.toggled.connect(this, &Player::setPlaying);
        nextButton.triggered.connect(this, &Player::next);
        slider.valueChanged.connect(this, &Player::seek);
        albums.activeChanged.connect(this, &Player::playAlbum);
        titles.activeChanged.connect(this, &Player::playTitle);

        folders = listFiles("Music"_,Sort|Folders);
        assert(folders);
        for(string& folder : folders) albums << Text(string(section(folder,'/',-2,-1)), 16);

        int time=0;
        if(!files && existsFile("Music/.last"_)) {
            string mark = readFile("Music/.last"_);
            ref<byte> last = section(mark,0);
            time = toInteger(section(mark,0,1,2));
            string folder = string(section(last,'/',0,2));
            albums.index = folders.indexOf(folder);
            array<string> files = listFiles(folder,Recursive|Sort|Files);
            uint i=0; for(;i<files.size();i++) if(files[i]==last) break;
            for(;i<files.size();i++) appendFile(move(files[i]));
        }
        window.setSize(int2(-512,-512)); window.show();
        if(files) next();
        if(time) seek(time);
    }
    void appendFile(string&& path) {
        string title = string(section(section(path,'/',-2,-1),'.',0,-2));
        uint i=title.indexOf('-'); i++; //skip album name
        while(i<title.size() && title[i]>='0'&&title[i]<='9') i++; //skip track number
        while(i<title.size() && (title[i]==' '||title[i]=='.'||title[i]=='-'||title[i]=='_')) i++; //skip whitespace
        titles << Text(replace(title.slice(i),"_"_," "_), 16);
        files << move(path);
    }
    void play(const string& path) {
        assert(isFolder(path));
        array<string> files = listFiles(path,Recursive|Sort|Files);
        for(string& file: files) appendFile(move(file));
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
        file=openFile(path);
        if(media) media->~AudioMedia(), media=0;
        if(endsWith(path,".mp3"_)) media=new (&mp3) MP3Media(file);
        else if(endsWith(path,".flac"_)) media=new (&flac) FLACMedia(file);
        else warn("Unsupported format",path);
        audio.start();
        assert(audio.channels==media->channels);
        if(audio.rate!=media->rate) new (&resampler) Resampler(audio.channels,audio.periodSize*media->rate/audio.rate,audio.periodSize);
        setPlaying(true);
        writeFile("/Music/.last"_,string(files[index]+"\0"_+dec(0)));
    }
    void next() {
        if(!titles.count()) return;
        if(!playButton.enabled) setPlaying(true);
        if(titles.index+1<titles.count()) playTitle(++titles.index);
        else if(albums.index<albums.count()) window.setTitle(albums.active().text);
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
};
Application(Player)
