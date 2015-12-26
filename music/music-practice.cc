/// \file music.cc Keyboard (piano) practice application
#include "thread.h"
#include "midi-input.h"
#include "sampler.h"
#include "asound.h"
#include "pdf-renderer.h"
#include "MusicXML.h"
#include "midi.h"
#include "sheet.h"
#include "ui/layout.h"
#include "interface.h"
#include "window.h"

/// SFZ sampler and PDF renderer
struct Music {
 const Folder& folder = currentWorkingDirectory();
 array<String> files = folder.list(Files|Sorted);
 String title;

 const uint rate = 44100;
 Thread decodeThread;
 unique<Sampler> sampler = nullptr;
 Thread audioThread{-20};
 AudioOutput audio {audioThread};
 MidiInput input {audioThread};

 array<unique<FontData>> fonts;
 unique<Sheet> sheet = nullptr;
 unique<Scroll<HList<GraphicsWidget>>> pages;
 unique<Window> window = ::window(&pages->area(), 0);

 Music() {
  window->actions[UpArrow] = {this, &Music::previousTitle};
  window->actions[DownArrow] = {this, &Music::nextTitle};
  window->actions[Return] = {this, &Music::nextTitle};

  if(files) setTitle(arguments() ? arguments()[0] : files[0]);

  setInstrument("Piano");

  AudioControl("Master Playback Switch") = 1;
  AudioControl("Headphone Playback Switch") = 1;
  AudioControl("Master Playback Volume") = 100;
  audio.start(sampler->rate, sampler->periodSize, 32, 2);
  //assert_(audioThread);
 }
 ~Music() {
  decodeThread.wait(); // ~Thread
  audioThread.wait(); // ~Thread
 }

 void setInstrument(string name) {
  if(audioThread) audioThread.wait();
  if(decodeThread) decodeThread.wait(); // ~Thread
  sampler = unique<Sampler>(name+'/'+name+".sfz"_, 512, [this](uint){ input.event(); }, decodeThread); // Ensures all events are received right before mixing
  input.noteEvent = {sampler.pointer, &Sampler::noteEvent};
  input.ccEvent = {sampler.pointer, &Sampler::ccEvent};
  audio.read32 = {sampler.pointer, &Sampler::read32};
  audioThread.spawn();
  decodeThread.spawn();
 }
 void setTitle(string title) {
  if(endsWith(title,".pdf"_)||endsWith(title,".xml"_)||endsWith(title,".mid"_)) title=title.slice(0,title.size-4);
  this->title = copyRef(title);
  buffer<Graphics> pages;
  /**/ if(existsFile(title+".xml"_, folder)) { // MusicXML
   MusicXML musicXML (readFile(title+".xml"_, folder));
   sheet = unique<Sheet>(musicXML.signs, musicXML.divisions, window->size);
   pages = move(sheet->pages);
  }
  else if(existsFile(title+".pdf"_, folder)) { // PDF
   pages = decodePDF(readFile(title+".pdf"_, folder), fonts);
  }
  else if(existsFile(title+".mid"_, folder)) { // MIDI
   MidiFile midi (readFile(title+".mid"_, folder));
   sheet = unique<Sheet>(midi.signs, midi.divisions, window->size);
   pages = move(sheet->pages);
  }
  else error(title);
  this->pages = unique<Scroll<HList<GraphicsWidget>>>( apply(pages, [](Graphics& o) { return GraphicsWidget(move(o)); }) );
  this->pages->horizontal = true;
  window->widget = window->focus = &this->pages->area();
  window->render();
  window->setTitle(title);
 }
 void previousTitle() {
  for(size_t index: range(1, files.size)) if(startsWith(files[index], title) && !startsWith(files[index-1], title)) {
   setTitle(section(files[index-1],'.', 0, -2));
   break;
  }
 }
 void nextTitle() {
  for(size_t index: range(files.size-1)) if(startsWith(files[index], title) && !startsWith(files[index+1], title)) {
   setTitle(section(files[index+1],'.', 0, -2));
   break;
  }
 }
} app;
