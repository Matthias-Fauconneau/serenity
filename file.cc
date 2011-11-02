#include "file.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <time.h>
#include <dirent.h>

bool exists(const string& path) {
	int fd = open(strz(path).data, O_RDONLY);
	if(fd >= 0) { close(fd); return true; }
	return false;
}

struct stat statFile(const string& path) { struct stat file; stat(strz(path).data, &file); return file; }
bool isDirectory(const string& path) { return statFile(path).st_mode&S_IFDIR; }

array<string> listFiles(const string& folder, bool recursive) {
	array<string> list;
	DIR* dir = opendir(strz(folder).data);
	assert(dir);
	for(dirent* dirent; (dirent=readdir(dir));) {
		string name = strz(dirent->d_name);
		if(name!=_(".") && name!=_("..")) {
			string path = folder+_("/")+name;
			if(recursive && isDirectory(path)) list << move(listFiles(path)); else list << move(path);
		}
	}
	closedir(dir);
	return list;
}

int createFile(const string& path) {
	return open(strz(path).data,O_CREAT|O_WRONLY|O_TRUNC,0666);
}

string mapFile(const string& path) {
	int fd = open(strz(path).data, O_RDONLY);
	assert(fd >= 0,"File not found",path);
	struct stat sb; fstat(fd, &sb);
	const void* data = mmap(0,(size_t)sb.st_size,PROT_READ,MAP_PRIVATE,fd,0); //|MAP_HUGETLB/*avoid seeks*/
	close(fd);
	return string((const char*)data,(int)sb.st_size);
}
