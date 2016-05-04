#include "http.h"
#include "json.h"

struct Nine {
 Nine() {
  Map document = getURL(URL("http://"+arguments()[0]));
  //Element root = parseHTML(document);
  Variant root = parseJSON(document);
  for(const Variant& item: root.dict.at("data").list) {
   log(item.dict.at("votes").dict.at("count").integer(), item.dict.at("caption").data, item.dict.at("images").dict.at("large").data);
  }
 }
} app;
