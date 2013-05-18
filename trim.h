#pragma once
#include "volume.h"

/// Trims the maximum balls results (which were rounded up) so that rock space stays zeroed
void trim(Volume16& target, const Volume32& pore, const Volume16& maximum);
