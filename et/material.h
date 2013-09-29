#pragma once
#include "string.h"
#include "shader.h"

map<String,unique<Shader>> parseMaterialFile(string data);
