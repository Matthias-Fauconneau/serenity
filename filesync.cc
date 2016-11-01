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
    while(s.whileInteger()) s.match(' ');
    s.match('-');
    s.match(' ');
    if(find(s.slice(s.index)," - ")) s.until(" - ");
    if(s.slice(s.index).contains('(')) return trim(s.until('('));
    if(s.slice(s.index).contains('[')) return trim(s.until('['));
    return s.until(".mp3");
}

struct Test {
    Test() {
        Folder store("Music", home());
        array<String> storeFiles = store.list(Files|Recursive);
#if 0
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
            if(!existsFile(simplify(basename(storeFile))+".mp3", store)) rename(storeFile, simplify(basename(storeFile))+".mp3", store);
        }
#endif
#if 0
        for(size_t i=0; i<storeFiles.size; i++) { string storeFile = storeFiles[i]; // files mutates
            for(string other: storeFiles) {
                if(storeFile == other) continue;
                String a = toLower(simplify(basename(storeFile)));
                String b = toLower(simplify(basename(other)));
                if(a == b) {
                    log(storeFile, other);
                    error(""); //remove(storeFile, store);
                    storeFiles.removeAt(i);
                    break;
                }
            }
        }
        return;
#endif
        assert_(arguments());
        Folder device(arguments()[0]);
        array<String> deviceFiles = device.list(Files);

#if 0
        for(size_t i=0; i<deviceFiles.size; i++) { string deviceFile = deviceFiles[i]; // files mutates
            if(!endsWith(deviceFile,".mp3")) continue;
            for(string other: deviceFiles) {
                if(!endsWith(other,".mp3")) continue;
                if(deviceFile == other) continue;
                string a = simplify(deviceFile);
                string b = simplify(other);
                if(a == b) {
                    log(deviceFile, other);
                    //error("E");
                    remove(deviceFile, device);
                    deviceFiles.removeAt(i);
                    break;
                }
            }
        }
        //return;
#endif
#if 0
        for(string deviceFile: deviceFiles) {
            //if(!endsWith(deviceFile,".mp3")) continue;
            if(endsWith(deviceFile,".LIB")) continue;
#if 0
            if(basename(deviceFile) == simplify(basename(deviceFile))) continue;
            log(basename(deviceFile), simplify(basename(deviceFile)));
            rename(deviceFile, simplify(basename(deviceFile)), device);
#else
            if(basename(deviceFile) == simplify(basename(deviceFile))+".mp3") continue;
            log(basename(deviceFile), simplify(basename(deviceFile))+".mp3");
            rename(deviceFile, simplify(basename(deviceFile))+".mp3", device);
#endif
        }
#endif
#if 1
        buffer<String> storeNames = apply(storeFiles, [](string path) { return copyRef(basename(path)); });
        for(string deviceFile: deviceFiles) {
            if(!storeNames.contains(deviceFile)) {
                log(deviceFile);
                //remove(deviceFile, device);
            }
        }
#endif
#if 0
        size_t totalCount = 0, totalSize = 0;
        for(string storeFile: storeFiles) {
            if(!endsWith(storeFile,".mp3")) continue;
            if(deviceFiles.contains(basename(storeFile))) {
                //assert_(File(storeFile, store).size() == File(basename(storeFile), device).size(), storeFile, File(storeFile, store).size(), File(basename(storeFile), device).size());
                if(File(storeFile, store).size() == File(basename(storeFile), device).size()) continue;
                log("Size mismatch", File(storeFile, store).size(), File(basename(storeFile), device).size());
                //continue;
            } else log("Missing file", basename(storeFile));
            size_t size = File(storeFile, store).size();
            log(basename(storeFile,".mp3"), (int)round(size/1024/1024.f), "MB");
            totalCount += 1;
            totalSize += size;
        }
        if(totalCount) log(totalCount, "files", (int)round(totalSize/1024/1024.f),"MB");
#endif
#if 0
        size_t progressCount = 0, progressSize = 0;
        for(string storeFile: storeFiles) {
            if(!endsWith(storeFile,".mp3")) continue;
            if(deviceFiles.contains(basename(storeFile))) {
                //assert_(File(storeFile, store).size() == File(basename(storeFile), device).size(), storeFile, File(storeFile, store).size(), File(basename(storeFile), device).size());
                if(File(storeFile, store).size() == File(basename(storeFile), device).size()) continue;
                //continue;
            }
            size_t size = File(storeFile, store).size();
            log(str(progressCount,2u,' '),"/",totalCount, (int)round(progressSize/1024/1024.f),"/",(int)round(totalSize/1024/1024.f),"MB",":",basename(storeFile),
                (int)round(size/1024/1024.f), "MB");
            progressCount += 1;
            progressSize += size;
            copy(store, storeFile, device, basename(storeFile), true);
        }
#endif
#if 0
        {
                    array<String> deviceFiles = device.list(Files);
                    array<String> randomSequence (deviceFiles.size);
                    Random random; // Unseeded so that the random sequence only depends on collection
                    while(deviceFiles) randomSequence.append(deviceFiles.take(random%deviceFiles.size));
                    for(size_t i: range(randomSequence.size)) {
                        log(str(i, 3u)+" "+randomSequence[i]);
                        rename(randomSequence[i], str(i, 3u)+" "+randomSequence[i], device);
                    }

        }
#endif
    }
} test;
