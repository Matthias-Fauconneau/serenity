/// \file album.cc Photo album
#include "image-folder.h"
#include "source-view.h"
#include "layout.h"

struct Album {
	Folder folder {"Pictures", home()};
	ImageFolder source { folder };
};

struct AlbumPreview : Album, Application {
	PersistentValue<String> lastName {folder, ".last", [this]{ return source.elementName(index); }};
	const size_t lastIndex = source.keys.indexOf(lastName);
	size_t index = lastIndex != invalid ? lastIndex : 0;

	ImageSourceView view {source, &index};
	Window window {&view, -1, [this]{ return view.title(); }};
};
registerApplication(AlbumPreview);
