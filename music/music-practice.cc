/// \file music.cc Keyboard (piano) practice application
#include "thread.h"
#include "midi-input.h"
#include "sampler.h"
#include "asound.h"
#include "window.h"
#include "rle.h"

/// SFZ sampler and PDF renderer
struct Music : Widget {
 const Folder folder = "."_;
 array<String> files = folder.list(Files|Sorted);
 String title;

 Thread decodeThread {19};
 unique<Sampler> sampler = nullptr;
 Thread audioThread{-20};

 //array<unique<FontData>> fonts;
 //unique<Sheet> sheet = nullptr;
 //unique<Scroll<HList<GraphicsWidget>>> pages;
 Image8 image;
 unique<Window> window = ::window(/*&pages->area()*/this, int2(1366, 768));

 AudioOutput audio {window?audioThread:mainThread};
 MidiInput input {window?audioThread:mainThread};

 Music() {
  if(window) {
   //window->actions[Key('s')] = [this]{ writeFile("keyboard", str(input.max), Folder(".config"_, home()), true); };
   window->actions[UpArrow] = {this, &Music::previousTitle};
   window->actions[DownArrow] = [this]{ writeFile("keyboard", str(input.max), Folder(".config"_, home()), true); nextTitle(); }; //{this, &Music::nextTitle};
   window->actions[Return] = {this, &Music::nextTitle};
   if(files) setTitle(arguments() ? arguments()[0] : files[0]);
  }

  setInstrument("Piano");

  //AudioControl("Master Playback Switch") = 1;
  //AudioControl("Headphone Playback Switch") = 1;
  //AudioControl("Master Playback Volume") = 100;
  audio.start(sampler->rate, sampler->periodSize, 32, 2, true);
  //sampler->noteEvent(60, 64);
 }
 ~Music() {
  //audio.stop();
  //decodeThread.wait(); // ~Thread
  //if(audioThread) audioThread.wait(); // ~Thread
 }

 void setInstrument(string name) {
  if(audioThread) audioThread.wait();
  if(decodeThread) decodeThread.wait(); // ~Thread
  sampler = unique<Sampler>(name+'/'+name+".autocorrected.sfz"_, 128, [this](uint){ input.event(); }, decodeThread); // Ensures all events are received right before mixing
  input.noteEvent = {sampler.pointer, &Sampler::noteEvent};
  input.ccEvent = {sampler.pointer, &Sampler::ccEvent};
  audio.read32 = {sampler.pointer, &Sampler::read32};
  if(window) audioThread.spawn();
  decodeThread.spawn();
 }
 void setTitle(string title) {
  assert_(window);
  if(endsWith(title,".pdf"_)||endsWith(title,".xml"_)||endsWith(title,".mid"_)) title=title.slice(0,title.size-4);
  this->title = copyRef(title);
#if 0
  buffer<Graphics> pages;
  /**/ if(existsFile(title+".xml"_, folder)) { // MusicXML
   MusicXML musicXML (readFile(title+".xml"_, folder));
   sheet = unique<Sheet>(musicXML.signs, musicXML.divisions, window->size);
   pages = move(sheet->pages);
  }
  else if(existsFile(title+".pdf"_, folder)) { // PDF
   //pages = decodePDF(readFile(title+".pdf"_, folder), fonts);
  }
  else if(existsFile(title+".mid"_, folder)) { // MIDI
   MidiFile midi (readFile(title+".mid"_, folder));
   sheet = unique<Sheet>(midi.signs, midi.ticksPerBeat, window->size);
   pages = move(sheet->pages);
  }
  else
#endif
  {
   Time time{true};
   string name = title;
   String imageFile; int2 size;
   auto list = folder.list(Files);
   for(const String& file: list) {
    TextData s (file);
    if(!s.match(name)) continue;
    if(!s.match(".")) continue;
    if(!s.isInteger()) continue;
    const uint w = s.integer(false);
    if(!s.match("x")) continue;
    if(!s.isInteger()) continue;
    const uint h = s.integer(false);
    if(!s.match(".rle")) continue;
    if(s) continue;
    size = int2(w, h);
    imageFile = copyRef(file);
   }
   if(!imageFile) return;
   image = Image8(decodeRunLength(cast<uint8>(readFile(imageFile))), int2(size.y, size.x));
   log(time, size, float(image.ref::size)/1024/1024,"M");
   //for(int x: range(image.size.x)) for(int y: range(image.size.y)) image[y*image.stride+x] = transpose[x*image.size.y+y];
   //log(time);
  }
  //else error(title);
  //this->pages = unique<Scroll<HList<GraphicsWidget>>>( apply(pages, [](Graphics& o) { return GraphicsWidget(move(o)); }) );
  //this->pages->vertical = false;
  //this->pages->horizontal = true;
  //window->widget = window->focus = &this->pages->area();
  window->render();
  window->setTitle(title);
 }
 vec2 sizeHint(vec2) override { return vec2(1366, 768); }
 shared<Graphics> graphics(vec2) override {
  window->backgroundColor = __builtin_nanf("");
  const Image& target = window->target;
  assert_(target.size.x < image.size.y && target.size.y < image.size.x, target.size, image.size);
  for(int x: range(target.size.x)) for(int y: range(target.size.y)) target[y*target.stride+x] = byte4(byte3(image[x*image.size.x+y]), 0xFF);
  return shared<Graphics>();
 }
 void previousTitle() {
  for(size_t index: range(1, files.size)) if(startsWith(files[index], title) && !startsWith(files[index-1], title)) {
   setTitle(section(files[index-1],'.', 0, 1));
   break;
  }
 }
 void nextTitle() {
  for(size_t index: range(files.size-1)) if(startsWith(files[index], title) && !startsWith(files[index+1], title)) {
   setTitle(section(files[index+1],'.', 0, 1));
   break;
  }
 }
} app;
