#include "data.h"
#include "file.h"
#include "math.h"

string dirname(string x) { return x.contains('/') ? section(x,'/',0, -2) : ""_; }
string basename(string x, string suffix="") {
    string name = x.contains('/') ? section(x,'/',-2,-1) : x;
    string basename = endsWith(name, suffix) ? name.slice(0, name.size-suffix.size) : name;
    assert_(basename, name, basename);
    return basename;
}

string simplify(string x) {
    assert_(x);
    TextData s (x);
    s.whileInteger();
    s.match(' ');
    s.match('-');
    s.match(' ');
    if(find(s.slice(s.index)," - ")) s.until(" - ");
    assert_(find(s.slice(s.index),".mp3"), x);
    return s.until(".mp3");
}

struct Test {
    Test() {
        Folder store("Music", home());
        array<String> storeFiles = store.list(Files|Recursive);

        for(string storeFile: storeFiles) {
            Map file(storeFile, store);
            if(!(startsWith(file, "ID3") || startsWith(file, "\xff\xfb"))) {
                log(storeFile, hex(file.slice(0,3)));
                error("A");//remove(storeFile, store);
            }
            assert_(storeFile);
            if(!endsWith(storeFile,".mp3")) continue;
            if(basename(storeFile) == simplify(basename(storeFile))+".mp3") continue;
            log(basename(storeFile), dirname(storeFile)+"/"+simplify(basename(storeFile))+".mp3");
            error("B"); //rename(storeFile, dirname(storeFile)+"/"+simplify(basename(storeFile))+".mp3", store);
        }

        for(size_t i=0; i<storeFiles.size; i++) { string storeFile = storeFiles[i]; // files mutates
            for(string other: storeFiles) {
                if(storeFile == other) continue;
                String a = toLower(simplify(basename(storeFile)));
                String b = toLower(simplify(basename(other)));
                if(a == b) {
                    log(storeFile, other);
                    error(""); remove(storeFile, store);
                    storeFiles.removeAt(i);
                    break;
                }
            }
            //assert_(endsWith(storeFile,".mp3"));
        }

        Folder device(arguments()[0]);
        array<String> deviceFiles = device.list(Files);

        for(size_t i=0; i<deviceFiles.size; i++) { string deviceFile = deviceFiles[i]; // files mutates
            for(string other: deviceFiles) {
                if(deviceFile == other) continue;
                string a = simplify(deviceFile);
                string b = simplify(other);
                if(a == b) {
                    log(deviceFile, other);
                    error("E"); remove(deviceFile, device);
                    deviceFiles.removeAt(i);
                    break;
                }
            }
            assert_(endsWith(deviceFile,".mp3"));
        }

        for(string deviceFile: deviceFiles) {
            if(basename(deviceFile) == simplify(basename(deviceFile))+".mp3") continue;
            log(basename(deviceFile), simplify(basename(deviceFile))+".mp3");
            rename(deviceFile, simplify(basename(deviceFile))+".mp3", device);
        }

        buffer<String> storeNames = apply(storeFiles, [](string path) { return copyRef(basename(path)); });
        for(string deviceFile: deviceFiles) {
            if(!storeNames.contains(deviceFile)) {
                log(deviceFile);
                remove(deviceFile, device);
            }
        }

        size_t totalCount = 0, totalSize = 0;
        for(string storeFile: storeFiles) {
            if(!endsWith(storeFile,".mp3")) continue;
            if(deviceFiles.contains(basename(storeFile))) {
                //assert_(File(storeFile, store).size() == File(basename(storeFile), device).size(), storeFile, File(storeFile, store).size(), File(basename(storeFile), device).size());
                if(File(storeFile, store).size() == File(basename(storeFile), device).size()) continue;
                log("Size mismatch", File(storeFile, store).size(), File(basename(storeFile), device).size());
            } else log("Missing file", basename(storeFile));
            size_t size = File(storeFile, store).size();
            log(basename(storeFile,".mp3"), (int)round(size/1024/1024.f), "MB");
            totalCount += 1;
            totalSize += size;
        }
        if(totalCount) log(totalCount, "files", (int)round(totalSize/1024/1024.f),"MB");

        size_t progressCount = 0, progressSize = 0;
        for(string storeFile: storeFiles) {
            if(!endsWith(storeFile,".mp3")) continue;
            if(deviceFiles.contains(basename(storeFile))) {
                //assert_(File(storeFile, store).size() == File(basename(storeFile), device).size(), storeFile, File(storeFile, store).size(), File(basename(storeFile), device).size());
                if(File(storeFile, store).size() == File(basename(storeFile), device).size()) continue;
            }
            size_t size = File(storeFile, store).size();
            log(str(progressCount,2u,' '),"/",totalCount, (int)round(progressSize/1024/1024.f),"/",(int)round(totalSize/1024/1024.f),"MB",":",basename(storeFile),
                (int)round(size/1024/1024.f), "MB");
            progressCount += 1;
            progressSize += size;
            copy(store, storeFile, device, basename(storeFile), true);
        }
    }
} test;
