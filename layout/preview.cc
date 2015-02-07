#include "layout/parse.h"
#include "layout/solve.h"
#include "layout/render.h"
#include "thread.h"
#include "interface.h"
#include "window.h"

struct LayoutPreview {
	String path = arguments() ? (endsWith(arguments()[0],"layout") ? copyRef(arguments()[0]) : arguments()[0]+".layout") :
		move(Folder(".").list(Files).filter([](string name){return !endsWith(name,"layout");})[0]);
	FileWatcher watcher{path, [this](string){ update(); }};
	ImageView view;
	unique<Window> window = nullptr;
	LayoutPreview() { update(); }
	void update() {
		LayoutParse parse("."_, readFile(path), {}, &watcher);
		LayoutSolve solve(move(parse));
		float mmPx = min(1050/solve.size.x, (1680-32-24)/solve.size.y);
		view.image = move(LayoutRender(move(solve), mmPx).target);
		if(!window) window = unique<Window>(&view, -1, [this](){return copyRef(path);});
		window->render();
		window->show();
	}
} app;
