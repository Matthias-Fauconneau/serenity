#include "data.h"
#include "file.h"
#include "math.h"
#include "time.h"

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
    return s.slice(s.index).contains('(') ? s.until('(') : s.until(".mp3");
}

struct Test {
    Test() {
#if 0
        Folder store("Music", home());
        array<String> storeFiles = store.list(Files|Recursive);

        for(string storeFile: storeFiles) {
#if 0
            Map file(storeFile, store);
            if(!(startsWith(file, "ID3") || startsWith(file, "\xff\xfb"))) {
                log(storeFile, hex(file.slice(0,3)));
                error("A");//remove(storeFile, store);
            }
            if(!endsWith(storeFile,".mp3")) continue;
#endif
            if(basename(storeFile) == simplify(basename(storeFile))+".mp3") continue;
            log(basename(storeFile), simplify(basename(storeFile))+".mp3");
            rename(storeFile, simplify(basename(storeFile))+".mp3", store);
        }
        return;

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
#endif
        for(string deviceFile: deviceFiles) {
            if(basename(deviceFile) == simplify(basename(deviceFile))+".mp3") continue;
            log(basename(deviceFile), simplify(basename(deviceFile))+".mp3");
            rename(deviceFile, simplify(basename(deviceFile))+".mp3", device);
        }
#if 0
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
#endif
        {
                    array<String> deviceFiles = device.list(Files);
                    array<String> randomSequence (deviceFiles.size);
                    Random random; // Unseeded so that the random sequence only depends on collection
                    while(deviceFiles) randomSequence.append(deviceFiles.take(random%deviceFiles.size));
                    for(size_t index: range(randomSequence)) log(randomSequence[i], str(i)+" "+randomSequence[i]);

        }
    }
} test;
