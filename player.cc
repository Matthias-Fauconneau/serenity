/// \file player.cc Music player
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

/// Generic audio decoder (using ffmpeg)
extern "C" {
#define __STDC_CONSTANT_MACROS
#include <stdint.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}
struct FFmpegMedia : AudioMedia {
    AVFormatContext* file=0;
    AVStream* audioStream=0;
    AVCodecContext* audio=0;
    int audioPTS=0;

    FFmpegMedia(){}
    FFmpegMedia(const ref<byte>& path){
        static int unused once=(av_register_all(), 0);
        if(avformat_open_input(&file, strz(path), 0, 0)) { file=0; return; }
        avformat_find_stream_info(file, 0);
        if(file->duration <= 0) return;
        for(uint i=0; i<file->nb_streams; i++) {
            if(file->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO) {
                audioStream = file->streams[i];
                audio = audioStream->codec;
                audio->request_sample_fmt = audio->sample_fmt = AV_SAMPLE_FMT_FLT;
                AVCodec* codec = avcodec_find_decoder(audio->codec_id);
                if(codec && avcodec_open2(audio, codec, 0) >= 0 ) {
                    this->rate = audio->sample_rate; this->channels = audio->channels;
                    break;
                }
            }
        }
        assert(audio, path);
        assert(audio->sample_fmt == AV_SAMPLE_FMT_FLT || audio->sample_fmt == AV_SAMPLE_FMT_S16);
        assert(this->rate);
        assert(this->channels == 2);
    }
    uint position() { return audioPTS/1000;  }
    uint duration() { return file->duration/1000/1000; }
    void seek(uint unused position) { av_seek_frame(file,-1,position*1000*1000,0); }
    Buffer<float2> buffer __(8192);
    int read(float2* output, uint outputSize) {
        uint readSize = outputSize;

        uint size = min(outputSize,buffer.size);
        for(uint i: range(size)) output[i] = buffer[i];
        for(uint i: range(buffer.size-size)) buffer[i] = buffer[size+i]; //FIXME: ring buffer
        buffer.size -= size, outputSize -= size; output+=size;

        while(outputSize) {
            AVPacket packet;
            if(av_read_frame(file, &packet) < 0) return 0;
            if(file->streams[packet.stream_index]==audioStream) {
                AVFrame frame; avcodec_get_frame_defaults(&frame); int gotFrame=0;
                int used = avcodec_decode_audio4(audio, &frame, &gotFrame, &packet);
                if(used < 0 || !gotFrame) continue;
                uint inputSize = frame.nb_samples;
                assert(int(inputSize-outputSize)<int(buffer.capacity));
                if(audio->sample_fmt == AV_SAMPLE_FMT_FLT) {
                    float2* input = (float2*)frame.data[0];
                    uint size = min(inputSize,outputSize);
                    for(uint i: range(size)) output[i] = input[i]*(float2){0x1p31,0x1p31};
                    if(inputSize > outputSize) {
                        for(uint i: range(inputSize-outputSize)) buffer[i] = input[outputSize+i]*(float2){0x1p31,0x1p31};
                        buffer.size += inputSize-outputSize;
                    }
                    outputSize -= size; output+=size;
                } else {
                    int16* input = (int16*)frame.data[0];
                    uint size = min(inputSize,outputSize);
                    for(uint i: range(size)) output[i] = (float2){0x1p16f*input[2*i+0], 0x1p16f*input[2*i+1]};
                    if(inputSize > outputSize) {
                        for(uint i: range(inputSize-outputSize))
                            buffer[i] = (float2){0x1p16f*input[2*(outputSize+i)+0], 0x1p16f*input[2*(outputSize+i)+1]};
                        buffer.size += inputSize-outputSize;
                    }
                    outputSize -= size; output+=size;

                }
                audioPTS = packet.dts*audioStream->time_base.num*1000/audioStream->time_base.den;
            }
            av_free_packet(&packet);
        }
        return readSize;
    }
    ~FFmpegMedia() { avformat_close_input(&file); }
};

/// Music player with a two-column interface (albums/track), gapless playback and persistence of last track+position
struct Player {
// Pipeline: File -> AudioMedia -> [Resampler] -> AudioOutput
    static constexpr int channels=2;
    AudioMedia* media=0;
    FFmpegMedia ffmpeg;
    Resampler resampler;
    AudioOutput audio __({this,&Player::read});
    bool read(ptr& swPointer, int32* output, uint size) {
        float buffer[2*size];
        uint inputSize = resampler?resampler.need(size):size;
        {int size=inputSize; for(float2* input=(float2*)buffer;;) {
            if(!media) return false;
            int read=media->read(input,size);
            if(read==size) break;
            assert(read<size);
            if(read>0) input+=read, size-=read;
            next();
        }}
        if(resampler) resampler.filter<false>(buffer,inputSize,buffer,size);
        assert(size%4==0);
        for(uint i: range(size*2)) output[i] = buffer[i];
        swPointer += size;
        update(media->position(),media->duration());
        return true;
    }

// Interface
    ICON(play) ICON(pause) ToggleButton playButton __(playIcon(), pauseIcon());
    ICON(next) TriggerButton nextButton __(nextIcon());
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

        folders = Folder("Music"_).list(Folders);
        assert(folders);
        for(string& folder : folders) albums << string(section(folder,'/',-2,-1));

        if(existsFile("Music/.last"_)) {
            string mark = readFile("Music/.last"_);
            ref<byte> last = section(mark,0);
            ref<byte> album = section(last,'/',0,1);
            ref<byte> title = section(last,'/',1,-1);
            if(existsFolder(album,"Music"_)) {
                albums.index = folders.indexOf(string(album));
                array<string> files = Folder(album,"Music"_).list(Recursive|Files);
                uint i=0; for(;i<files.size();i++) if(files[i]==title) break;
                for(;i<files.size();i++) queueFile(files[i], album);
                if(files) {
                    next();
                    seek(toInteger(section(mark,0,1,2)));
                }
            }
        }
        window.setSize(int2(-512,-512));
        mainThread().priority=-19;
    }
    void queueFile(const ref<byte>& file, const ref<byte>& folder) {
        string title = string(section(section(file,'/',-2,-1),'.',0,-2));
        uint i=title.indexOf('-'); i++; //skip album name
        while(i<title.size() && title[i]>='0'&&title[i]<='9') i++; //skip track number
        while(i<title.size() && (title[i]==' '||title[i]=='.'||title[i]=='-'||title[i]=='_')) i++; //skip whitespace
        titles << Text(replace(title.slice(i),"_"_," "_), 16);
        files <<  folder+"/"_+file;
    }
    void playAlbum(const ref<byte>& album) {
        assert(existsFolder(album,"Music"_),album);
        array<string> files = Folder(album,"Music"_).list(Recursive|Files);
        for(const string& file: files) queueFile(file, album);
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
        ref<byte> path = files[index];
        if(media) media->~AudioMedia(), media=0;
        media = new (&ffmpeg) FFmpegMedia(string("/Music/"_+path));
        assert(audio.channels==media->channels);
        if(audio.rate!=media->rate) new (&resampler) Resampler(audio.channels, media->rate, audio.rate, audio.periodSize);
        setPlaying(true);
    }
    void next() {
        if(titles.index+1<titles.count()) playTitle(++titles.index);
        else if(albums.index+1<albums.count()) playAlbum(++albums.index);
        else if(albums.count()) playAlbum(albums.index=0);
        else { window.setTitle("Player"_); stop(); return; }
        //titles.ensureVisible(titles.active());
    }
    void togglePlay() { setPlaying(!playButton.enabled); }
    void setPlaying(bool play) {
        if(playButton.enabled==play) return;
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
    void seek(int position) { if(media) { media->seek(position); update(media->position(),media->duration()); } }
    void update(int position, int duration) {
        if(slider.value == position) return;
        writeFile("/Music/.last"_,string(files[titles.index]+"\0"_+dec(position)));
        if(!window.mapped) return;
        slider.value = position; slider.maximum=duration;
        elapsed.setText(string(dec(uint64(position/60),2)+":"_+dec(uint64(position%60),2)));
        remaining.setText(string(dec(uint64((duration-position)/60),2)+":"_+dec(uint64((duration-position)%60),2)));
        window.render();
    }
} application;
