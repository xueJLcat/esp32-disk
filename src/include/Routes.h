#pragma once
#include <ESPAsyncWebServer.h>
#include "FSManager.h"

void registerRoutes(AsyncWebServer &server, FSManager &fs);
