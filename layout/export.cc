#include "parse.h"
#include "solve.h"
#include "render.h"
#include "text.h"
#include "time.h"
#include "jpeg.h"

struct LayoutExport {
	// UI
	Text text;
	//Window window {&text};
	Timer autoclose { [this]{
			//window.unregisterPoll(); // Lets process termination close window to assert no windowless process remains
			mainThread.post(); // Lets main UI thread notice window unregistration and terminate
			requestTermination(); // FIXME: should not be necessary
					  }};
	//LayoutExport() { window.actions[Space] = [this]{ autoclose.timeout = {}; }; } // Disables autoclose on input
	// Work
	Thread workerThread {0, true}; // Separate thread to render layouts while main thread handles UI
	void setText(string logText){ text = logText; Locker lock(mainThread.runLock); /*window.setSize(int2(ceil(text.sizeHint(0)))); window.render();*/ }
	Job job {workerThread, [this]{
			String fileName;
			if(::arguments() && existsFile(::arguments()[0])) fileName = copyRef(::arguments()[0]);
			else if(::arguments() && existsFile(::arguments()[0]+".layout"_)) fileName = ::arguments()[0]+".layout"_;
			else {
				array<String> files = Folder(".").list(Files|Sorted);
				files.filter([](string fileName){return !endsWith(fileName,"layout");});
				if(files.size == 1) fileName = move(files[0]);
			}
			array<String> folders = Folder(".").list(Folders|Sorted);
			if(folders && (!fileName || File(fileName).size()<=2)) { // Batch processes subfolders and write to common output folder
				log("Collection", fileName);
				Time total, render, encode; total.start();
				for(string folderName: folders) {
					Folder folder = folderName;
					array<String> files = folder.list(Files|Sorted);
					files.filter([](string name){return !endsWith(name,"layout");});
					if(files.size == 0 && folderName=="Output") continue;
					else if(files.size==0) { error("No layouts in", folderName); continue; }
					else if(files.size>1) { error("Several layouts in", folderName); }
					String fileName = move(files[0]);
					string name = endsWith(fileName, ".layout") ? section(fileName,'.',0,-2) : fileName;
					log(name);
					//window.setTitle(name); // FIXME: thread safety
					setText(name);
					if(File(fileName, folder).size()<=2) error("Empty file", fileName); // TODO: Nested collections
					LayoutParse parse {folder, readFile(fileName, folder), {this, &LayoutExport::setText}};
					if(parse.errors) return;
					LayoutSolve solve(move(parse));
					LayoutRender render(move(solve), 0, 300 /*pixel per inch*/);
					buffer<byte> file = encodeJPEG(render.target);
					writeFile(name+'.'+strx(int2(round(render.size/10.f)))+".jpg"_, file, Folder("Output"_, currentWorkingDirectory(), true), true);
				}
				total.stop();
				log(total, render, encode);
			}
			else if(existsFile(fileName)) {
				string name = endsWith(fileName, ".layout") ? section(fileName,'.',0,-2) : fileName;
				setText(name);
				if(File(fileName).size()<=2) error("Empty file", fileName);  // TODO: Collection argument
				LayoutParse parse {currentWorkingDirectory(), readFile(fileName), {this, &LayoutExport::setText}};
				if(parse.errors) return;
				LayoutSolve solve(move(parse));
				LayoutRender render(move(solve), 0, 300 /*pixel per inch*/);
				buffer<byte> file = encodeJPEG(render.target);
				writeFile(name+'.'+strx(int2(round(render.size/10.f)))+".jpg"_, file, currentWorkingDirectory(), true);
			}
			autoclose.setRelative(1000); // Automatically close after one second of inactivity unless space bar is pressed
	}};
} app;
