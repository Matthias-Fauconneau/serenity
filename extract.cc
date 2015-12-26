#include "zip.h"
#include "thread.h"
#include "math.h"

struct Test {
 string base = "Piano";
 Map zip {base+".zip"};
 Test() {
  log(str(sum(listZIPFile(zip).values)/1024./1024., 3u));
  Folder folder (base);
  for(auto entry: listZIPFile(zip)) {
   string name = section(entry.key,'/',-2,-1);
   if(!existsFile(name, folder)) {
    log(name, str(entry.value/1024./1024.,1u));
    writeFile(name, extractZIPFile(zip, entry.key), folder);
   } else {
    size_t size = File(name,folder).size();
    assert_(size == entry.value || size == extractZIPFile(zip, entry.key).size, size, entry.value, extractZIPFile(zip, entry.key).size, extractZIPFile(zip, entry.key).slice(entry.value));
   }
  }
 }
} test;
